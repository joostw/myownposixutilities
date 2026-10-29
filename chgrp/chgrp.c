/*
 * chgrp.c - chgrp: change the file group ownership (POSIX.1-2024, XCU chgrp)
 *
 * Synopsis:
 *   chgrp [-h] group file...
 *   chgrp -R [-H|-L|-P] group file...
 *
 * The same program as chown, but for the group only (the owner argument
 * of fchownat() is -1: unchanged).
 *
 * -R walks the hierarchy with fstatat(), fchownat() and openat() relative
 * to open directories, so depth and pathname length are no limit; at most
 * 32 directory descriptors are kept open, a closed level being reopened as
 * ".." of the level below and checked by its device and inode (the level
 * above a directory reached through a symbolic link that -L followed stays
 * open).  Directory loops (-L) are reported and not entered.
 *
 * Choices where the standard leaves room:
 *  - -R without -H, -L or -P acts as -P: symbolic links are changed
 *    themselves and not followed.
 *  - With -H or -L, a symbolic link whose target does not exist is changed
 *    itself.
 *  - A name is looked up first; only if there is no such group is a
 *    string of digits taken as a number.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <locale.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *prog = "chgrp";
static uid_t uid = (uid_t)-1;
static gid_t gid = (gid_t)-1;
static bool opt_h, opt_R;
static char follow = 'P';       /* -H -L -P */
static int status;

static void *xrealloc(void *p, size_t n)
{
    if (!(p = realloc(p, n ? n : 1))) {
        fprintf(stderr, "%s: out of memory\n", prog);
        exit(1);
    }
    return p;
}

static bool all_digits(const char *s)
{
    return *s && strspn(s, "0123456789") == strlen(s);
}

static void get_group(const char *s)
{
    struct group *gr = getgrnam(s);
    if (gr)
        gid = gr->gr_gid;
    else if (all_digits(s))
        gid = (gid_t)strtoul(s, NULL, 10);
    else {
        fprintf(stderr, "%s: %s: no such group\n", prog, s);
        exit(1);
    }
}

/* ---- pathnames ---- */

static char *path;
static size_t pathlen, pathcap;

static size_t path_push(const char *name)
{
    size_t old = pathlen, n = strlen(name);
    bool slash = pathlen > 0 && path[pathlen - 1] != '/';
    if (pathlen + n + 2 > pathcap) {
        pathcap = (pathlen + n + 2) * 2;
        path = xrealloc(path, pathcap);
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
    fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(e));
    status = 1;
}

/* ---- the open directories ---- */

struct level {
    int fd;
    dev_t dev;
    ino_t ino;
    bool pinned;
};
static struct level *lv;
static size_t depth, lvcap, nopen, maxopen = 32;

static void push_level(int fd, const struct stat *st)
{
    if (depth == lvcap) {
        lvcap = lvcap ? lvcap * 2 : 16;
        lv = xrealloc(lv, lvcap * sizeof *lv);
    }
    lv[depth++] = (struct level){ fd, st->st_dev, st->st_ino, false };
    if (++nopen > maxopen)
        for (size_t i = 0; i + 1 < depth; i++)
            if (lv[i].fd >= 0 && !lv[i].pinned) {
                close(lv[i].fd);
                lv[i].fd = -1;
                nopen--;
                break;
            }
}

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
            names = xrealloc(names, cap * sizeof *names);
        }
        size_t l = strlen(e->d_name) + 1;
        names[n++] = memcpy(xrealloc(NULL, l), e->d_name, l);
    }
    closedir(d);
    *count = n;
    return names;
}

/* ---- changing ---- */

static void change(int dirfd, const char *name, bool operand)
{
    bool want = opt_R ? (follow == 'L' || (follow == 'H' && operand)) : !opt_h;
    struct stat st;
    bool followed = false;
    if (want && fstatat(dirfd, name, &st, 0) == 0)
        followed = true;
    else if (want && (errno != ENOENT || !opt_R)) {
        /* without -R the operand is chown()ed: a dangling link is an error */
        err(errno);
        return;
    } else if (fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        err(errno);
        return;
    }
    if (fchownat(dirfd, name, uid, gid, followed ? 0 : AT_SYMLINK_NOFOLLOW) != 0)
        err(errno);
    if (!opt_R || !S_ISDIR(st.st_mode))
        return;
    for (size_t i = 0; i < depth; i++)
        if (lv[i].dev == st.st_dev && lv[i].ino == st.st_ino) {
            fprintf(stderr, "%s: %s: a directory loop (not entered)\n", prog, path);
            status = 1;
            return;
        }
    int fd = openat(dirfd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | (followed ? 0 : O_NOFOLLOW));
    struct stat fst;
    if (fd < 0) {
        err(errno);
        return;
    }
    if (fstat(fd, &fst) != 0 || fst.st_dev != st.st_dev || fst.st_ino != st.st_ino) {
        fprintf(stderr, "%s: %s: changed while being walked\n", prog, path);
        status = 1;
        close(fd);
        return;
    }
    size_t n;
    int rerr;
    char **names = read_names(fd, &n, &rerr);
    if (followed && depth > 0) {
        struct stat lst;
        if (fstatat(dirfd, name, &lst, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(lst.st_mode))
            lv[depth - 1].pinned = true;    /* ".." of fd is elsewhere */
    }
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
    if (!pop_level()) {
        fprintf(stderr, "%s: %s: the directory above it has moved\n", prog, path);
        status = 1;
    }
}

static void usage(void)
{
    fprintf(stderr, "usage: chgrp [-h] group file...\n"
                    "       chgrp -R [-H|-L|-P] group file...\n");
    exit(2);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "hHLPR")) != -1) {
        switch (c) {
        case 'h': opt_h = true; break;
        case 'R': opt_R = true; break;
        case 'H': case 'L': case 'P': follow = (char)c; break;
        default: usage();
        }
    }
    if (argc - optind < 2)
        usage();
    const char *g = argv[optind++];
    if (!*g) {
        fprintf(stderr, "chgrp: the group is empty\n");
        return 2;
    }
    get_group(g);
    long om = sysconf(_SC_OPEN_MAX);
    if (om > 0 && (om - 10) / 2 < (long)maxopen)
        maxopen = om > 14 ? (size_t)(om - 10) / 2 : 2;
    for (int i = optind; i < argc; i++) {
        pathlen = 0;
        path_push(argv[i]);
        change(AT_FDCWD, argv[i], true);
    }
    return status;
}
