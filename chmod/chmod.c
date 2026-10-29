/*
 * chmod.c - chmod: change the file modes (POSIX.1-2024, XCU chmod)
 *
 * Synopsis: chmod [-R] mode file...
 *
 * The mode operand is compiled once into a list of actions and applied to
 * each file in turn, starting from that file's mode.  -R walks the
 * hierarchy with openat() relative to open directories (no limit on depth
 * or pathname length; at most 64 descriptors are kept open, a closed level
 * is reopened as ".." of the one below and checked).
 *
 * Choices where the standard leaves room:
 *  - Set-user-ID and set-group-ID requests are honoured for every file
 *    type, also when no execute bit is set; an octal mode sets all twelve
 *    bits as given, for directories too.  Clearing the execute bits leaves
 *    the set-ID bits alone.
 *  - t sets or clears S_ISVTX when the who is empty or includes o (a
 *    implies o); with only u or g it is ignored.
 *  - -R: symbolic links named as operands are followed; symbolic links met
 *    in the hierarchy are neither followed nor changed.
 *  - -R: a directory's mode is changed before its contents when the new
 *    mode lets the owner read and search it, and after them otherwise, so
 *    that both opening up and closing a hierarchy work without privileges.
 *  - A mode that is a digit string but not octal, or above 07777, is an
 *    error.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int status;

/* ---- the mode ---- */

struct action {
    char op;                    /* + - = */
    int who;                    /* bits 1 u, 2 g, 4 o; 0: none given */
    mode_t perm;                /* the r w x s t bits, for all three classes */
    bool X;                     /* X given */
    char copy;                  /* u g o: copy the permissions of that class, or 0 */
};

static struct action *acts;
static size_t nacts;
static bool octal;
static mode_t octal_mode;
static mode_t cmask;

static void bad_mode(const char *s)
{
    fprintf(stderr, "chmod: invalid mode: %s\n", s);
    exit(2);
}

static void compile_mode(const char *s)
{
    if (*s >= '0' && *s <= '9') {
        mode_t m = 0;
        for (const char *p = s; *p; p++) {
            if (*p < '0' || *p > '7')
                bad_mode(s);
            m = m * 8 + (mode_t)(*p - '0');
            if (m > 07777)
                bad_mode(s);
        }
        octal = true;
        octal_mode = m;
        return;
    }
    size_t cap = 0;
    const char *p = s;
    for (;;) {
        /* clause: [wholist] actionlist */
        int who = 0;
        for (;; p++) {
            if (*p == 'u') who |= 1;
            else if (*p == 'g') who |= 2;
            else if (*p == 'o') who |= 4;
            else if (*p == 'a') who |= 7;
            else break;
        }
        if (*p != '+' && *p != '-' && *p != '=')
            bad_mode(s);
        while (*p == '+' || *p == '-' || *p == '=') {
            struct action a = { *p++, who, 0, false, 0 };
            if (*p == 'u' || *p == 'g' || *p == 'o') {
                a.copy = *p++;          /* permcopy: one letter, alone */
            } else {
                for (;; p++) {
                    if (*p == 'r') a.perm |= 0444;
                    else if (*p == 'w') a.perm |= 0222;
                    else if (*p == 'x') a.perm |= 0111;
                    else if (*p == 'X') a.X = true;
                    else if (*p == 's') a.perm |= S_ISUID | S_ISGID;
                    else if (*p == 't') a.perm |= S_ISVTX;
                    else break;
                }
            }
            if (nacts == cap) {
                cap = cap ? cap * 2 : 8;
                if (!(acts = realloc(acts, cap * sizeof *acts))) {
                    fprintf(stderr, "chmod: out of memory\n");
                    exit(1);
                }
            }
            acts[nacts++] = a;
        }
        if (*p == '\0')
            break;
        if (*p != ',')
            bad_mode(s);
        p++;
    }
}

/* the bits of a class: its rwx bits, and the special bit that goes with it */
static mode_t class_bits(int who)
{
    mode_t m = 0;
    if (who & 1) m |= S_IRWXU | S_ISUID;
    if (who & 2) m |= S_IRWXG | S_ISGID;
    if (who & 4) m |= S_IRWXO | S_ISVTX;
    return m;
}

/* the new mode of a file whose mode is old */
static mode_t new_mode(mode_t old)
{
    if (octal)
        return octal_mode;
    bool isdir = S_ISDIR(old);
    mode_t m = old & 07777;
    for (size_t i = 0; i < nacts; i++) {
        struct action *a = &acts[i];
        mode_t perm = a->perm;
        if (a->X && (isdir || (old & 0111)))
            perm |= 0111;
        if (a->copy) {
            mode_t c = a->copy == 'u' ? (m >> 6) & 7 : a->copy == 'g' ? (m >> 3) & 7 : m & 7;
            perm = c << 6 | c << 3 | c;
        }
        mode_t mask;
        if (a->who) {
            mask = class_bits(a->who);
            /* t goes with o; s with u and g: for o alone it is nothing */
        } else {
            mask = 07777 & ~cmask;
            mask |= S_ISUID | S_ISGID | S_ISVTX;    /* the umask has no say in these */
        }
        perm &= mask;
        switch (a->op) {
        case '+':
            m |= perm;
            break;
        case '-':
            m &= ~perm;
            break;
        case '=':
            m &= ~(a->who ? class_bits(a->who) : 07777);
            m |= perm;
            break;
        }
    }
    return m;
}

/* ---- the files ---- */

static char *path;
static size_t pathlen, pathcap;

static size_t path_push(const char *name)
{
    size_t old = pathlen, n = strlen(name);
    bool slash = pathlen > 0 && path[pathlen - 1] != '/';
    if (pathlen + n + 2 > pathcap) {
        pathcap = (pathlen + n + 2) * 2;
        if (!(path = realloc(path, pathcap))) {
            fprintf(stderr, "chmod: out of memory\n");
            exit(1);
        }
    }
    if (slash)
        path[pathlen++] = '/';
    memcpy(path + pathlen, name, n + 1);
    pathlen += n;
    return old;
}

static void path_pop(size_t old)
{
    pathlen = old;
    path[pathlen] = '\0';
}

static void err(int e)
{
    fprintf(stderr, "chmod: %s: %s\n", path, strerror(e));
    status = 1;
}

struct level {
    int fd;
    dev_t dev;
    ino_t ino;
};
static struct level *lv;
static size_t depth, lvcap, nopen, maxopen = 64;

static void push_level(int fd, const struct stat *st)
{
    if (depth == lvcap) {
        lvcap = lvcap ? lvcap * 2 : 16;
        if (!(lv = realloc(lv, lvcap * sizeof *lv))) {
            fprintf(stderr, "chmod: out of memory\n");
            exit(1);
        }
    }
    lv[depth++] = (struct level){ fd, st->st_dev, st->st_ino };
    if (++nopen > maxopen)
        for (size_t i = 0; i + 1 < depth; i++)
            if (lv[i].fd >= 0) {
                close(lv[i].fd);
                lv[i].fd = -1;
                nopen--;
                break;
            }
}

/* leave the top level, reopening the one below as its ".." if needed */
static bool pop_level(void)
{
    struct level *top = &lv[depth - 1];
    bool ok = true;
    if (depth >= 2 && lv[depth - 2].fd < 0) {
        struct level *b = &lv[depth - 2];
        struct stat st;
        int fd = openat(top->fd, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd >= 0 && fstat(fd, &st) == 0 && st.st_dev == b->dev && st.st_ino == b->ino) {
            b->fd = fd;
            nopen++;
        } else {
            if (fd >= 0)
                close(fd);
            ok = false;
        }
    }
    close(top->fd);
    nopen--;
    depth--;
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
            if (!(names = realloc(names, cap * sizeof *names))) {
                fprintf(stderr, "chmod: out of memory\n");
                exit(1);
            }
        }
        if (!(names[n++] = strdup(e->d_name))) {
            fprintf(stderr, "chmod: out of memory\n");
            exit(1);
        }
    }
    closedir(d);
    *count = n;
    return names;
}

static bool recursive;

/* change one file (the entry name of the directory dirfd); follow: an
   operand, whose symbolic links are followed */
static void change(int dirfd, const char *name, bool follow)
{
    struct stat st;
    if (fstatat(dirfd, name, &st, follow ? 0 : AT_SYMLINK_NOFOLLOW) != 0) {
        err(errno);
        return;
    }
    if (S_ISLNK(st.st_mode))
        return;                 /* met in the hierarchy: left alone */
    mode_t m = new_mode(st.st_mode);
    if (!recursive || !S_ISDIR(st.st_mode)) {
        if (fchmodat(dirfd, name, m, 0) != 0)
            err(errno);
        return;
    }
    /* first: change it before its contents; it is then done either way */
    bool first = (m & (S_IRUSR | S_IXUSR)) == (S_IRUSR | S_IXUSR);
    if (first && m != (st.st_mode & 07777) && fchmodat(dirfd, name, m, 0) != 0)
        err(errno);
    int fd = openat(dirfd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | (follow ? 0 : O_NOFOLLOW));
    if (fd < 0) {
        err(errno);
        if (!first && m != (st.st_mode & 07777) && fchmodat(dirfd, name, m, 0) != 0)
            err(errno);
        return;
    }
    struct stat fst;
    if (fstat(fd, &fst) != 0 || fst.st_dev != st.st_dev || fst.st_ino != st.st_ino) {
        fprintf(stderr, "chmod: %s: changed while being walked\n", path);
        status = 1;
        close(fd);
        return;
    }
    size_t n;
    int rerr;
    char **names = read_names(fd, &n, &rerr);
    push_level(fd, &fst);
    if (rerr)
        err(rerr);
    for (size_t i = 0; i < n; i++) {
        int me = lv[depth - 1].fd;
        size_t old = path_push(names[i]);
        if (me >= 0)
            change(me, names[i], false);
        path_pop(old);
        free(names[i]);
    }
    free(names);
    bool ok = pop_level();
    if (!ok) {
        fprintf(stderr, "chmod: %s: the directory above it has moved\n", path);
        status = 1;
        return;
    }
    if (depth > 0)
        dirfd = lv[depth - 1].fd;
    if (!first && m != (st.st_mode & 07777) && fchmodat(dirfd, name, m, 0) != 0)
        err(errno);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    /* the mode may start with '-' (chmod -w f): only -R is an option */
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (a[0] != '-' || a[1] == '\0')
            break;
        if (strspn(a + 1, "R") == strlen(a + 1)) {
            recursive = true;
            continue;
        }
        break;                  /* a mode such as -w or -x,u+r */
    }
    if (argc - i < 2) {
        fprintf(stderr, "usage: chmod [-R] mode file...\n");
        return 2;
    }
    cmask = umask(0);
    umask(cmask);
    compile_mode(argv[i++]);
    long om = sysconf(_SC_OPEN_MAX);
    if (om > 0 && (om - 10) / 2 < (long)maxopen)
        maxopen = om > 14 ? (size_t)(om - 10) / 2 : 2;
    for (; i < argc; i++) {
        pathlen = 0;
        path_push(argv[i]);
        change(AT_FDCWD, argv[i], true);
    }
    return status;
}
