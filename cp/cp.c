/*
 * cp.c - cp: copy files (POSIX.1-2024, XCU cp)
 *
 * Synopsis:
 *   cp [-Pfip] source_file target_file
 *   cp [-Pfip] source_file... target
 *   cp -R [-H|-L|-P] [-fip] source_file... target
 *
 * Hierarchies are copied with openat(), mkdirat() and the other *at()
 * functions relative to open directories, in both trees, so pathname
 * length is no limit.  Of each tree at most 32 directory descriptors are
 * kept open; a closed level is reopened as ".." of the level below and its
 * device and inode are checked.  Where that cannot work (a source
 * directory reached through a symbolic link that -L followed) the level
 * above is kept open.
 *
 * Choices where the standard leaves room:
 *  - -R without -H, -L or -P acts as -P: symbolic links are copied.
 *  - Copying a file to itself is a diagnosed error (exit status 1).  So is
 *    copying a directory into itself or below itself, and a directory
 *    cycle met with -L; the cycle is not followed.
 *  - Created FIFOs, devices and symbolic links (-R) get the permissions of
 *    the source modified by the file mode creation mask (or the source's,
 *    with -p).  When such a destination exists already and is not a
 *    directory, it is removed first (after a prompt, with -i), so that a
 *    hierarchy can be copied again over an earlier copy.
 *  - A directory is created with the permissions of the source modified by
 *    the mask, plus S_IRWXU, and then set to the source's permissions
 *    modified by the mask; the contents of a directory that cannot be
 *    created are not copied.
 *  - -p: the owner and group are set first (a failure with EPERM is not
 *    reported; the set-ID bits are then cleared), then the mode, then the
 *    times; directories get their times after their contents.
 *  - -P without -R copies a symbolic link operand as a symbolic link.
 *  - The prompt of -i is "cp: overwrite FILE? ".
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool opt_f, opt_i, opt_p, opt_R;
static char opt_link = 0;       /* 'H', 'L' or 'P'; 0: none given */
static mode_t cmask;
static int status;

static void *xrealloc(void *p, size_t n)
{
    if (!(p = realloc(p, n))) {
        fprintf(stderr, "cp: out of memory\n");
        exit(1);
    }
    return p;
}

/* ---- pathnames for messages ---- */

struct path {
    char *s;
    size_t len, cap;
};
static struct path spath, dpath;

static size_t path_push(struct path *p, const char *name)
{
    size_t old = p->len, n = strlen(name);
    bool slash = p->len > 0 && p->s[p->len - 1] != '/';
    if (p->len + n + 2 > p->cap) {
        p->cap = (p->len + n + 2) * 2;
        p->s = xrealloc(p->s, p->cap);
    }
    if (slash)
        p->s[p->len++] = '/';
    memcpy(p->s + p->len, name, n + 1);
    p->len += n;
    return old;
}

static void path_set(struct path *p, const char *s)
{
    p->len = 0;
    if (!p->s) {
        p->cap = 64;
        p->s = xrealloc(NULL, p->cap);
    }
    p->s[0] = '\0';
    size_t n = strlen(s);
    if (n + 1 > p->cap) {
        p->cap = n + 1;
        p->s = xrealloc(p->s, p->cap);
    }
    memcpy(p->s, s, n + 1);
    p->len = n;
}

static void path_pop(struct path *p, size_t old)
{
    p->len = old;
    p->s[old] = '\0';
}

static void err(const char *file, int e)
{
    fprintf(stderr, "cp: %s: %s\n", file, strerror(e));
    status = 1;
}

static void errmsg(const char *file, const char *msg)
{
    fprintf(stderr, "cp: %s: %s\n", file, msg);
    status = 1;
}

/* ---- prompts ---- */

static bool yes(void)
{
    static regex_t re;
    static int ready;
    if (!ready) {
        const char *y = nl_langinfo(YESEXPR);
        ready = (y && *y && regcomp(&re, y, REG_EXTENDED | REG_NOSUB) == 0) ? 1 : -1;
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, stdin);
    bool r = false;
    if (n > 0) {
        if (line[n - 1] == '\n')
            line[n - 1] = '\0';
        r = ready == 1 ? regexec(&re, line, 0, NULL, 0) == 0 : (line[0] == 'y' || line[0] == 'Y');
    }
    free(line);
    return r;
}

static bool may_overwrite(void)
{
    if (!opt_i)
        return true;
    fprintf(stderr, "cp: overwrite %s? ", dpath.s);
    return yes();
}

/* ---- stacks of open directories ---- */

struct level {
    int fd;                     /* -1 when closed */
    dev_t dev;
    ino_t ino;
    bool pinned;                /* may not be closed: cannot be reopened as ".." */
};

struct dstack {
    struct level *lv;
    size_t depth, cap, nopen, max;
};
static struct dstack src, dst;

static void ds_push(struct dstack *s, int fd, const struct stat *st)
{
    if (s->depth == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->lv = xrealloc(s->lv, s->cap * sizeof *s->lv);
    }
    s->lv[s->depth++] = (struct level){ fd, st->st_dev, st->st_ino, false };
    if (++s->nopen > s->max)
        for (size_t i = 0; i + 1 < s->depth; i++)
            if (s->lv[i].fd >= 0 && !s->lv[i].pinned) {
                close(s->lv[i].fd);
                s->lv[i].fd = -1;
                s->nopen--;
                break;
            }
}

/* leave the top level, reopening the one below as its ".." if needed */
static bool ds_pop(struct dstack *s)
{
    struct level *top = &s->lv[s->depth - 1];
    bool ok = true;
    if (s->depth >= 2 && s->lv[s->depth - 2].fd < 0) {
        struct level *b = &s->lv[s->depth - 2];
        struct stat st;
        int fd = openat(top->fd, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd >= 0 && fstat(fd, &st) == 0 && st.st_dev == b->dev && st.st_ino == b->ino) {
            b->fd = fd;
            s->nopen++;
        } else {
            if (fd >= 0)
                close(fd);
            ok = false;
        }
    }
    close(top->fd);
    s->nopen--;
    s->depth--;
    return ok;
}

/* the directories made by this copy (they are not copied again) */
struct id {
    dev_t dev;
    ino_t ino;
};
static struct id *made;
static size_t nmade, capmade;

static void add_made(const struct stat *st)
{
    if (nmade == capmade) {
        capmade = capmade ? capmade * 2 : 16;
        made = xrealloc(made, capmade * sizeof *made);
    }
    made[nmade++] = (struct id){ st->st_dev, st->st_ino };
}

static bool is_made(const struct stat *st)
{
    for (size_t i = 0; i < nmade; i++)
        if (made[i].dev == st->st_dev && made[i].ino == st->st_ino)
            return true;
    return false;
}

/* ---- copying ---- */

static char **read_names(int fd, size_t *count, int *error)
{
    int fd2 = dup(fd);
    DIR *d = fd2 >= 0 ? fdopendir(fd2) : NULL;
    char **names = NULL;
    size_t n = 0, cap = 0;
    *error = 0;
    *count = 0;
    if (!d) {
        *error = errno;
        if (fd2 >= 0)
            close(fd2);
        return NULL;
    }
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(d);
        if (!e) {
            *error = errno;
            break;
        }
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            names = xrealloc(names, cap * sizeof *names);
        }
        if (!(names[n++] = strdup(e->d_name))) {
            fprintf(stderr, "cp: out of memory\n");
            exit(1);
        }
    }
    closedir(d);
    *count = n;
    return names;
}

/* -p: the owner, the mode and the times of dest (fd, or name in dirfd)
   from st */
static void preserve(int fd, int dirfd, const char *name, const struct stat *st)
{
    mode_t mode = st->st_mode & 07777;
    bool lnk = S_ISLNK(st->st_mode);
    int r = fd >= 0 ? fchown(fd, st->st_uid, st->st_gid)
                    : fchownat(dirfd, name, st->st_uid, st->st_gid, lnk ? AT_SYMLINK_NOFOLLOW : 0);
    if (r != 0) {
        if (errno != EPERM)
            err(dpath.s, errno);
        mode &= ~(mode_t)(S_ISUID | S_ISGID);
    }
    if (!lnk) {
        r = fd >= 0 ? fchmod(fd, mode) : fchmodat(dirfd, name, mode, 0);
        if (r != 0)
            err(dpath.s, errno);
    }
    struct timespec ts[2] = { st->st_atim, st->st_mtim };
    r = fd >= 0 ? futimens(fd, ts) : utimensat(dirfd, name, ts, lnk ? AT_SYMLINK_NOFOLLOW : 0);
    if (r != 0)
        err(dpath.s, errno);
}

static bool write_all(int fd, const char *buf, size_t n)
{
    while (n > 0) {
        ssize_t w = write(fd, buf, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        buf += w;
        n -= (size_t)w;
    }
    return true;
}

/* step 3: the contents of a file (sfd, open) to dest */
static void copy_contents(int sfd, const struct stat *st, int ddirfd, const char *dname)
{
    struct stat dst_st;
    int dfd = -1;
    if (fstatat(ddirfd, dname, &dst_st, 0) == 0) {
        if (!may_overwrite())
            return;
        dfd = openat(ddirfd, dname, O_WRONLY | O_TRUNC | O_CLOEXEC);
        if (dfd < 0 && opt_f) {
            int e = errno;
            if (unlinkat(ddirfd, dname, 0) == 0)
                dfd = openat(ddirfd, dname, O_WRONLY | O_CREAT | O_CLOEXEC,
                             st->st_mode & 0777 & ~cmask);
            else
                errno = e;
        }
    } else {
        dfd = openat(ddirfd, dname, O_WRONLY | O_CREAT | O_CLOEXEC, st->st_mode & 0777 & ~cmask);
    }
    if (dfd < 0) {
        err(dpath.s, errno);
        return;
    }
    static char *buf;
    enum { BUFSZ = 1 << 16 };
    if (!buf)
        buf = xrealloc(NULL, BUFSZ);
    for (;;) {
        ssize_t n = read(sfd, buf, BUFSZ);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            err(spath.s, errno);
            break;
        }
        if (n == 0)
            break;
        if (!write_all(dfd, buf, (size_t)n)) {
            err(dpath.s, errno);
            break;
        }
    }
    if (opt_p)
        preserve(dfd, -1, NULL, st);
    if (close(dfd) != 0)
        err(dpath.s, errno);
}

/* step 4: a file of another type (with -R, or a symbolic link with -P) */
static void copy_special(int sdirfd, const char *sname, const struct stat *st,
                         int ddirfd, const char *dname)
{
    struct stat dst_st;
    bool exists = fstatat(ddirfd, dname, &dst_st, AT_SYMLINK_NOFOLLOW) == 0;
    if (exists) {
        if (!may_overwrite())
            return;
        if (!S_ISDIR(dst_st.st_mode) && unlinkat(ddirfd, dname, 0) != 0) {
            err(dpath.s, errno);
            return;
        }
    }
    mode_t perm = st->st_mode & 0777 & ~cmask;
    int r;
    if (S_ISLNK(st->st_mode)) {
        size_t cap = st->st_size > 0 ? (size_t)st->st_size + 1 : 256;
        char *target;
        ssize_t n;
        for (;;) {
            target = xrealloc(NULL, cap);
            n = readlinkat(sdirfd, sname, target, cap);
            if (n < 0 || (size_t)n < cap)
                break;
            free(target);
            cap *= 2;
        }
        if (n < 0) {
            err(spath.s, errno);
            free(target);
            return;
        }
        target[n] = '\0';
        r = symlinkat(target, ddirfd, dname);
        free(target);
    } else if (S_ISFIFO(st->st_mode)) {
        r = mkfifoat(ddirfd, dname, perm);
    } else if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode) || S_ISSOCK(st->st_mode)) {
        r = mknodat(ddirfd, dname, (st->st_mode & S_IFMT) | perm, st->st_rdev);
    } else {
        errmsg(spath.s, "cannot copy a file of this type");
        return;
    }
    if (r != 0) {
        err(dpath.s, errno);
        return;
    }
    if (opt_p)
        preserve(-1, ddirfd, dname, st);
}

static void copy_file(int sdirfd, const char *sname, int ddirfd, const char *dname, bool operand);

/* step 2: a directory, with -R; sfd is open on it */
static void copy_dir(int sfd, const struct stat *st, int ddirfd, const char *dname, bool via_link)
{
    struct stat dst_st;
    bool created = false;
    if (fstatat(ddirfd, dname, &dst_st, dst.depth ? AT_SYMLINK_NOFOLLOW : 0) == 0) {
        if (!S_ISDIR(dst_st.st_mode)) {
            errmsg(dpath.s, "exists and is not a directory");
            close(sfd);
            return;
        }
    } else {
        if (mkdirat(ddirfd, dname, ((st->st_mode & 0777) & ~cmask) | S_IRWXU) != 0) {
            err(dpath.s, errno);
            close(sfd);
            return;
        }
        created = true;
    }
    int dfd = openat(ddirfd, dname, O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                     (dst.depth ? O_NOFOLLOW : 0));
    struct stat dfst;
    if (dfd < 0 || fstat(dfd, &dfst) != 0) {
        err(dpath.s, errno);
        if (dfd >= 0)
            close(dfd);
        close(sfd);
        return;
    }
    if (created)
        add_made(&dfst);
    /* copying a directory into itself */
    if (dfst.st_dev == st->st_dev && dfst.st_ino == st->st_ino) {
        errmsg(spath.s, "cannot copy a directory into itself");
        close(dfd);
        close(sfd);
        return;
    }
    size_t n;
    int rerr;
    char **names = read_names(sfd, &n, &rerr);
    if (via_link && src.depth > 0)
        src.lv[src.depth - 1].pinned = true;    /* ".." of sfd is elsewhere */
    ds_push(&src, sfd, st);
    ds_push(&dst, dfd, &dfst);
    if (rerr)
        err(spath.s, rerr);
    for (size_t i = 0; i < n; i++) {
        int s = src.lv[src.depth - 1].fd, d = dst.lv[dst.depth - 1].fd;
        size_t so = path_push(&spath, names[i]), dold = path_push(&dpath, names[i]);
        if (s >= 0 && d >= 0)
            copy_file(s, names[i], d, names[i], false);
        path_pop(&spath, so);
        path_pop(&dpath, dold);
        free(names[i]);
    }
    free(names);
    dfd = dst.lv[dst.depth - 1].fd;
    if (dfd >= 0) {
        if (opt_p)
            preserve(dfd, -1, NULL, st);
        else if (created && fchmod(dfd, st->st_mode & 0777 & ~cmask) != 0)
            err(dpath.s, errno);
    }
    bool ok = ds_pop(&dst);
    ok = ds_pop(&src) && ok;
    if (!ok)
        errmsg(spath.s, "a directory above it has moved");
}

/* the steps for one file: an operand, or a file in a hierarchy */
static void copy_file(int sdirfd, const char *sname, int ddirfd, const char *dname, bool operand)
{
    /* follow the symbolic link? */
    bool follow;
    if (!opt_R)
        follow = opt_link != 'P';
    else
        follow = opt_link == 'L' || (opt_link == 'H' && operand);
    struct stat st, lst;
    if (fstatat(sdirfd, sname, &lst, AT_SYMLINK_NOFOLLOW) != 0) {
        err(spath.s, errno);
        return;
    }
    st = lst;
    bool via_link = false;
    if (S_ISLNK(lst.st_mode) && follow) {
        if (fstatat(sdirfd, sname, &st, 0) != 0) {
            err(spath.s, errno);
            return;
        }
        via_link = true;
    }
    /* step 1: the same file */
    struct stat dst_st;
    if (fstatat(ddirfd, dname, &dst_st, S_ISLNK(st.st_mode) ? AT_SYMLINK_NOFOLLOW : 0) == 0 &&
        dst_st.st_dev == st.st_dev && dst_st.st_ino == st.st_ino) {
        fprintf(stderr, "cp: %s and %s are the same file\n", spath.s, dpath.s);
        status = 1;
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        if (!opt_R) {
            errmsg(spath.s, "is a directory (not copied)");
            return;
        }
        if (is_made(&st))
            return;             /* a copy made by this cp */
        for (size_t i = 0; i < src.depth; i++)
            if (src.lv[i].dev == st.st_dev && src.lv[i].ino == st.st_ino) {
                errmsg(spath.s, "a directory cycle (not followed)");
                return;
            }
        int sfd = openat(sdirfd, sname, O_RDONLY | O_DIRECTORY | O_CLOEXEC | (follow ? 0 : O_NOFOLLOW));
        struct stat fst;
        if (sfd < 0) {
            err(spath.s, errno);
            return;
        }
        if (fstat(sfd, &fst) != 0 || fst.st_dev != st.st_dev || fst.st_ino != st.st_ino) {
            errmsg(spath.s, "changed while being copied");
            close(sfd);
            return;
        }
        copy_dir(sfd, &st, ddirfd, dname, via_link);
        return;
    }
    if (S_ISLNK(st.st_mode) || (opt_R && !S_ISREG(st.st_mode))) {
        copy_special(sdirfd, sname, &st, ddirfd, dname);
        return;
    }
    /* step 3: a regular file, or (without -R) anything that can be read */
    int sfd = openat(sdirfd, sname, O_RDONLY | O_CLOEXEC | (follow ? 0 : O_NOFOLLOW));
    if (sfd < 0) {
        err(spath.s, errno);
        return;
    }
    copy_contents(sfd, &st, ddirfd, dname);
    close(sfd);
}

/* the last component of a pathname, without trailing slashes (malloc'ed) */
static char *last_component(const char *s)
{
    size_t n = strlen(s);
    while (n > 1 && s[n - 1] == '/')
        n--;
    size_t i = n;
    while (i > 0 && s[i - 1] != '/')
        i--;
    char *r = xrealloc(NULL, n - i + 1);
    memcpy(r, s + i, n - i);
    r[n - i] = '\0';
    return r;
}

/* would the directory dest_parent (and so a new entry in it) lie in the
   hierarchy rooted at the directory st? */
static bool inside(const char *dest_parent, const struct stat *st)
{
    int fd = open(dest_parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return false;
    for (;;) {
        struct stat cur;
        if (fstat(fd, &cur) != 0)
            break;
        if (cur.st_dev == st->st_dev && cur.st_ino == st->st_ino) {
            close(fd);
            return true;
        }
        int up = openat(fd, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        struct stat ust;
        if (up < 0 || fstat(up, &ust) != 0 || (ust.st_dev == cur.st_dev && ust.st_ino == cur.st_ino)) {
            if (up >= 0)
                close(up);
            break;
        }
        close(fd);
        fd = up;
    }
    close(fd);
    return false;
}

static void copy_operand(const char *source, const char *dest)
{
    path_set(&spath, source);
    path_set(&dpath, dest);
    nmade = 0;
    struct stat st;
    bool follow = !opt_R ? opt_link != 'P' : (opt_link == 'L' || opt_link == 'H');
    if (opt_R && (follow ? stat(source, &st) : lstat(source, &st)) == 0 && S_ISDIR(st.st_mode)) {
        /* the parent directory of dest */
        char *parent = xrealloc(NULL, strlen(dest) + 4);
        strcpy(parent, dest);
        size_t n = strlen(parent);
        while (n > 1 && parent[n - 1] == '/')
            n--;
        while (n > 0 && parent[n - 1] != '/')
            n--;
        if (n == 0)
            strcpy(parent, ".");
        else
            parent[n] = '\0';
        bool in = inside(parent, &st);
        free(parent);
        if (in) {
            struct stat dst_st;
            if (stat(dest, &dst_st) != 0 || dst_st.st_dev != st.st_dev || dst_st.st_ino != st.st_ino) {
                fprintf(stderr, "cp: %s: cannot copy a directory into itself (%s)\n", source, dest);
                status = 1;
                return;
            }
        }
    }
    copy_file(AT_FDCWD, source, AT_FDCWD, dest, true);
}

static void usage(void)
{
    fprintf(stderr, "usage: cp [-Pfip] source_file target_file\n"
                    "       cp [-Pfip] source_file... target\n"
                    "       cp -R [-H|-L|-P] [-fip] source_file... target\n");
    exit(2);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "fHiLPpR")) != -1) {
        switch (c) {
        case 'f': opt_f = true; break;
        case 'i': opt_i = true; break;
        case 'p': opt_p = true; break;
        case 'R': opt_R = true; break;
        case 'H': case 'L': case 'P': opt_link = (char)c; break;
        default: usage();
        }
    }
    argc -= optind;
    argv += optind;
    if (argc < 2)
        usage();
    if (!opt_R && (opt_link == 'H' || opt_link == 'L'))
        opt_link = 0;           /* -H and -L only matter with -R */
    cmask = umask(0);           /* applied by hand from here on */
    long om = sysconf(_SC_OPEN_MAX);
    src.max = dst.max = 32;
    if (om > 0 && (om - 10) / 4 < 32)
        src.max = dst.max = om > 18 ? (size_t)(om - 10) / 4 : 2;

    const char *target = argv[argc - 1];
    struct stat tst;
    bool target_dir = stat(target, &tst) == 0 && S_ISDIR(tst.st_mode);
    if (argc == 2 && !target_dir) {
        copy_operand(argv[0], target);      /* the first form, or -R to a new name */
        return status;
    }
    if (!target_dir) {
        if (stat(target, &tst) != 0)
            err(target, errno);
        else
            errmsg(target, "is not a directory");
        return 1;
    }
    for (int i = 0; i < argc - 1; i++) {
        char *last = last_component(argv[i]);
        size_t tl = strlen(target);
        char *dest = xrealloc(NULL, tl + strlen(last) + 2);
        strcpy(dest, target);
        if (tl == 0 || target[tl - 1] != '/')
            strcat(dest, "/");
        strcat(dest, last);
        copy_operand(argv[i], dest);
        free(dest);
        free(last);
    }
    return status;
}
