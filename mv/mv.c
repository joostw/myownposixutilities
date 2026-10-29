/*
 * mv.c - mv: move files (POSIX.1-2024, XCU mv)
 *
 * Synopsis:
 *   mv [-if] source_file target_file
 *   mv [-if] source_file... target_dir
 *
 * A file is moved with rename().  When that fails with EXDEV, the
 * hierarchy is duplicated (symbolic links as links; the times, owner,
 * group and mode of every file), and only if all of it was duplicated is
 * the source removed.  Both walks use openat() and the other *at()
 * functions relative to open directories, so pathname length is no limit;
 * at most 32 directory descriptors per tree are kept open, a closed level
 * being reopened as ".." of the level below and checked.
 *
 * Choices where the standard leaves room:
 *  - A source and destination that are the same directory entry are a
 *    diagnosed error (exit status 1); different directory entries for the
 *    same file (hard links): the source entry is removed.
 *  - Duplicated to another file system, hard links become separate files.
 *  - FIFOs, devices and sockets are duplicated as such (mkfifo, mknod);
 *    other types cannot be.
 *  - The prompt is "mv: overwrite FILE? " ("mv: overwrite write-protected
 *    FILE? " when it is that); a symbolic link is never write-protected.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <locale.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool opt_f, opt_i, stdin_tty;
static int status;

static void *xrealloc(void *p, size_t n)
{
    if (!(p = realloc(p, n))) {
        fprintf(stderr, "mv: out of memory\n");
        exit(1);
    }
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(xrealloc(NULL, n), s, n);
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
    if (p->s)
        p->s[0] = '\0';
    path_push(p, s);
}

static void path_pop(struct path *p, size_t old)
{
    p->len = old;
    p->s[old] = '\0';
}

static void err(const char *file, int e)
{
    fprintf(stderr, "mv: %s: %s\n", file, strerror(e));
    status = 1;
}

static void errmsg(const char *file, const char *msg)
{
    fprintf(stderr, "mv: %s: %s\n", file, msg);
    status = 1;
}

/* a failure to duplicate a characteristic: reported, the status unchanged */
static void warn(const char *file, int e)
{
    fprintf(stderr, "mv: %s: %s\n", file, strerror(e));
}

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

/* ---- stacks of open directories ---- */

struct level {
    int fd;
    dev_t dev;
    ino_t ino;
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
    s->lv[s->depth++] = (struct level){ fd, st->st_dev, st->st_ino };
    if (++s->nopen > s->max)
        for (size_t i = 0; i + 1 < s->depth; i++)
            if (s->lv[i].fd >= 0) {
                close(s->lv[i].fd);
                s->lv[i].fd = -1;
                s->nopen--;
                break;
            }
}

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
        names[n++] = xstrdup(e->d_name);
    }
    closedir(d);
    *count = n;
    return names;
}

/* ---- duplicating a hierarchy (to another file system) ---- */

/* the owner, the mode and the times of the copy (fd, or name in dirfd) */
static void characteristics(int fd, int dirfd, const char *name, const struct stat *st)
{
    mode_t mode = st->st_mode & 07777;
    bool lnk = S_ISLNK(st->st_mode);
    int r = fd >= 0 ? fchown(fd, st->st_uid, st->st_gid)
                    : fchownat(dirfd, name, st->st_uid, st->st_gid, lnk ? AT_SYMLINK_NOFOLLOW : 0);
    if (r != 0) {
        warn(dpath.s, errno);
        mode &= ~(mode_t)(S_ISUID | S_ISGID);
    }
    if (!lnk) {
        r = fd >= 0 ? fchmod(fd, mode) : fchmodat(dirfd, name, mode, 0);
        if (r != 0) {
            warn(dpath.s, errno);
            /* without the mode, the set-ID bits must not stay */
            if (mode & (S_ISUID | S_ISGID)) {
                mode &= ~(mode_t)(S_ISUID | S_ISGID);
                if ((fd >= 0 ? fchmod(fd, mode) : fchmodat(dirfd, name, mode, 0)) != 0)
                    warn(dpath.s, errno);
            }
        }
    }
    struct timespec ts[2] = { st->st_atim, st->st_mtim };
    r = fd >= 0 ? futimens(fd, ts) : utimensat(dirfd, name, ts, lnk ? AT_SYMLINK_NOFOLLOW : 0);
    if (r != 0)
        warn(dpath.s, errno);
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

/* duplicate one file (name in sdirfd) as name in ddirfd, which does not
   exist; true if it and everything below it was duplicated */
static bool dup_file(int sdirfd, const char *sname, int ddirfd, const char *dname)
{
    struct stat st;
    if (fstatat(sdirfd, sname, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        err(spath.s, errno);
        return false;
    }
    mode_t perm = st.st_mode & 0777;
    if (S_ISDIR(st.st_mode)) {
        int sfd = openat(sdirfd, sname, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat fst;
        if (sfd < 0 || fstat(sfd, &fst) != 0 || fst.st_dev != st.st_dev || fst.st_ino != st.st_ino) {
            err(spath.s, sfd < 0 ? errno : EAGAIN);
            if (sfd >= 0)
                close(sfd);
            return false;
        }
        if (mkdirat(ddirfd, dname, perm | S_IRWXU) != 0) {
            err(dpath.s, errno);
            close(sfd);
            return false;
        }
        int dfd = openat(ddirfd, dname, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat dfst;
        if (dfd < 0 || fstat(dfd, &dfst) != 0) {
            err(dpath.s, errno);
            if (dfd >= 0)
                close(dfd);
            close(sfd);
            return false;
        }
        size_t n;
        int rerr;
        char **names = read_names(sfd, &n, &rerr);
        ds_push(&src, sfd, &st);
        ds_push(&dst, dfd, &dfst);
        bool ok = true;
        if (rerr) {
            err(spath.s, rerr);
            ok = false;
        }
        for (size_t i = 0; i < n; i++) {
            int s = src.lv[src.depth - 1].fd, d = dst.lv[dst.depth - 1].fd;
            if (ok && s >= 0 && d >= 0) {
                size_t so = path_push(&spath, names[i]), dold = path_push(&dpath, names[i]);
                ok = dup_file(s, names[i], d, names[i]);
                path_pop(&spath, so);
                path_pop(&dpath, dold);
            } else
                ok = false;
            free(names[i]);
        }
        free(names);
        if (ok && dst.lv[dst.depth - 1].fd >= 0)
            characteristics(dst.lv[dst.depth - 1].fd, -1, NULL, &st);
        bool popped = ds_pop(&dst);
        popped = ds_pop(&src) && popped;
        if (!popped) {
            errmsg(spath.s, "a directory above it has moved");
            ok = false;
        }
        return ok;
    }
    if (S_ISREG(st.st_mode)) {
        int sfd = openat(sdirfd, sname, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (sfd < 0) {
            err(spath.s, errno);
            return false;
        }
        int dfd = openat(ddirfd, dname, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, perm & 0700);
        if (dfd < 0) {
            err(dpath.s, errno);
            close(sfd);
            return false;
        }
        static char *buf;
        enum { BUFSZ = 1 << 16 };
        if (!buf)
            buf = xrealloc(NULL, BUFSZ);
        bool ok = true;
        for (;;) {
            ssize_t n = read(sfd, buf, BUFSZ);
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0) {
                err(spath.s, errno);
                ok = false;
                break;
            }
            if (n == 0)
                break;
            if (!write_all(dfd, buf, (size_t)n)) {
                err(dpath.s, errno);
                ok = false;
                break;
            }
        }
        close(sfd);
        if (ok)
            characteristics(dfd, -1, NULL, &st);
        if (close(dfd) != 0 && ok) {
            err(dpath.s, errno);
            ok = false;
        }
        return ok;
    }
    int r;
    if (S_ISLNK(st.st_mode)) {
        size_t cap = st.st_size > 0 ? (size_t)st.st_size + 1 : 256;
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
            return false;
        }
        target[n] = '\0';
        r = symlinkat(target, ddirfd, dname);
        free(target);
    } else if (S_ISFIFO(st.st_mode)) {
        r = mkfifoat(ddirfd, dname, perm);
    } else if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode) || S_ISSOCK(st.st_mode)) {
        r = mknodat(ddirfd, dname, (st.st_mode & S_IFMT) | perm, st.st_rdev);
    } else {
        errmsg(spath.s, "cannot move a file of this type to another file system");
        return false;
    }
    if (r != 0) {
        err(dpath.s, errno);
        return false;
    }
    characteristics(-1, ddirfd, dname, &st);
    return true;
}

/* ---- removing a hierarchy ---- */

static struct dstack rmst;

static bool remove_tree(int dirfd, const char *name)
{
    struct stat st;
    if (fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        err(spath.s, errno);
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        if (unlinkat(dirfd, name, 0) != 0) {
            err(spath.s, errno);
            return false;
        }
        return true;
    }
    int fd = openat(dirfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat fst;
    if (fd < 0 || fstat(fd, &fst) != 0 || fst.st_dev != st.st_dev || fst.st_ino != st.st_ino) {
        err(spath.s, fd < 0 ? errno : EAGAIN);
        if (fd >= 0)
            close(fd);
        return false;
    }
    size_t n;
    int rerr;
    char **names = read_names(fd, &n, &rerr);
    ds_push(&rmst, fd, &fst);
    bool ok = true;
    if (rerr) {
        err(spath.s, rerr);
        ok = false;
    }
    for (size_t i = 0; i < n; i++) {
        int me = rmst.lv[rmst.depth - 1].fd;
        size_t old = path_push(&spath, names[i]);
        if (me < 0 || !remove_tree(me, names[i]))
            ok = false;
        path_pop(&spath, old);
        free(names[i]);
    }
    free(names);
    if (!ds_pop(&rmst)) {
        errmsg(spath.s, "the directory above it has moved");
        return false;
    }
    if (rmst.depth > 0)
        dirfd = rmst.lv[rmst.depth - 1].fd;
    if (ok && unlinkat(dirfd, name, AT_REMOVEDIR) != 0) {
        err(spath.s, errno);
        ok = false;
    }
    return ok;
}

/* ---- moving ---- */

/* split a pathname into its directory and last component, trailing
   slashes removed (malloc'ed) */
static void split(const char *p, char **dir, char **last)
{
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/')
        n--;
    size_t i = n;
    while (i > 0 && p[i - 1] != '/')
        i--;
    *last = xrealloc(NULL, n - i + 1);
    memcpy(*last, p + i, n - i);
    (*last)[n - i] = '\0';
    if (i == 0)
        *dir = xstrdup(".");
    else {
        size_t d = i;
        while (d > 1 && p[d - 1] == '/')
            d--;
        *dir = xrealloc(NULL, d + 1);
        memcpy(*dir, p, d);
        (*dir)[d] = '\0';
    }
}

/* do two pathnames name the same directory entry? */
static bool same_entry(const char *a, const char *b)
{
    char *da, *la, *db, *lb;
    struct stat sa, sb;
    split(a, &da, &la);
    split(b, &db, &lb);
    bool same = strcmp(la, lb) == 0 && stat(da, &sa) == 0 && stat(db, &sb) == 0 &&
                sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
    free(da);
    free(la);
    free(db);
    free(lb);
    return same;
}

static void move(const char *source, const char *dest)
{
    struct stat sst, dst_st;
    if (lstat(source, &sst) != 0) {
        err(source, errno);
        return;
    }
    bool exists = lstat(dest, &dst_st) == 0;
    if (exists && !opt_f) {
        bool unwritable = !S_ISLNK(dst_st.st_mode) && access(dest, W_OK) != 0 && errno == EACCES;
        if (opt_i || (unwritable && stdin_tty)) {
            fprintf(stderr, unwritable ? "mv: overwrite write-protected %s? " : "mv: overwrite %s? ", dest);
            if (!yes())
                return;
        }
    }
    if (exists && sst.st_dev == dst_st.st_dev && sst.st_ino == dst_st.st_ino) {
        if (same_entry(source, dest)) {
            fprintf(stderr, "mv: %s and %s are the same file\n", source, dest);
            status = 1;
        } else if (unlink(source) != 0)
            err(source, errno);
        return;
    }
    if (rename(source, dest) == 0)
        return;
    if (errno != EXDEV) {
        err(source, errno);
        return;
    }
    /* another file system: duplicate, then remove */
    if (exists) {
        if (S_ISDIR(dst_st.st_mode) && !S_ISDIR(sst.st_mode)) {
            errmsg(dest, "is a directory, the source is not");
            return;
        }
        if (!S_ISDIR(dst_st.st_mode) && S_ISDIR(sst.st_mode)) {
            errmsg(dest, "is not a directory, the source is");
            return;
        }
        if (remove(dest) != 0) {
            err(dest, errno);
            return;
        }
    }
    path_set(&spath, source);
    path_set(&dpath, dest);
    if (!dup_file(AT_FDCWD, source, AT_FDCWD, dest)) {
        fprintf(stderr, "mv: %s: not moved; the partial copy %s is left\n", source, dest);
        status = 1;
        return;
    }
    path_set(&spath, source);
    remove_tree(AT_FDCWD, source);
}

static void usage(void)
{
    fprintf(stderr, "usage: mv [-if] source_file target_file\n"
                    "       mv [-if] source_file... target_dir\n");
    exit(2);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "fi")) != -1) {
        switch (c) {
        case 'f': opt_f = true; opt_i = false; break;
        case 'i': opt_i = true; opt_f = false; break;
        default: usage();
        }
    }
    argc -= optind;
    argv += optind;
    if (argc < 2)
        usage();
    stdin_tty = isatty(0);
    long om = sysconf(_SC_OPEN_MAX);
    src.max = dst.max = rmst.max = 32;
    if (om > 0 && (om - 10) / 4 < 32)
        src.max = dst.max = rmst.max = om > 18 ? (size_t)(om - 10) / 4 : 2;

    const char *target = argv[argc - 1];
    struct stat tst;
    bool target_dir = stat(target, &tst) == 0 && S_ISDIR(tst.st_mode);
    if (!target_dir) {
        if (argc > 2) {
            errmsg(target, "is not a directory");
            return 1;
        }
        size_t tl = strlen(target);
        struct stat sst;
        if (tl > 0 && target[tl - 1] == '/' && lstat(argv[0], &sst) == 0 && !S_ISDIR(sst.st_mode)) {
            errmsg(target, "ends in a slash, and the source is not a directory");
            return 1;
        }
        move(argv[0], target);
        return status;
    }
    for (int i = 0; i < argc - 1; i++) {
        char *dir, *last;
        split(argv[i], &dir, &last);
        size_t tl = strlen(target);
        char *dest = xrealloc(NULL, tl + strlen(last) + 2);
        strcpy(dest, target);
        if (tl == 0 || target[tl - 1] != '/')
            strcat(dest, "/");
        strcat(dest, last);
        move(argv[i], dest);
        free(dest);
        free(dir);
        free(last);
    }
    return status;
}
