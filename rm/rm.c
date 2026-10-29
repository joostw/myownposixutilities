/*
 * rm.c - rm: remove directory entries (POSIX.1-2024, XCU rm)
 *
 * Synopsis:
 *   rm [-diRrv] file...
 *   rm -f [-diRrv] [file...]
 *
 * Hierarchies are removed with openat() and unlinkat() relative to open
 * directories, so pathname length is no limit.  At most 64 directory
 * descriptors are kept open (fewer under a low limit on open files); a level whose descriptor was closed is
 * reopened as ".." of the level below it (rm never follows symbolic links,
 * so ".." is the directory it came from), and its device and inode are
 * checked.  Each directory is read completely before anything in it is
 * removed.
 *
 * Choices where the standard leaves room:
 *  - The prompts are "rm: remove FILE? ", "rm: remove write-protected
 *    FILE? ", "rm: descend into directory FILE? " and "rm: remove
 *    directory FILE? "; -v writes "removed FILE" (or "removed directory
 *    FILE").
 *  - An empty directory is not prompted for twice: with -i, only "remove
 *    directory" is asked (the standard allows skipping to step 2d).
 *  - A directory is not removed, and nothing more is said about it, when
 *    something in it was kept, by an answer or by an error already
 *    reported.
 *  - A directory that cannot be read is still removed if it is empty.
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

/* directory descriptors kept open: at most 64, fewer when the limit on
   open files is low (set in main) */
static size_t maxopen = 64;

static bool opt_d, opt_f, opt_i, opt_r, opt_v;
static bool stdin_tty;
static int status;

/* the pathname of the current file, for messages */
static char *path;
static size_t pathlen, pathcap;

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (!p) {
        fprintf(stderr, "rm: out of memory\n");
        exit(1);
    }
    return p;
}

/* append "/name" (or name, at the start) to path; the old length */
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

static void err(const char *what, int e)
{
    if (what)
        fprintf(stderr, "rm: %s: %s: %s\n", path, what, strerror(e));
    else
        fprintf(stderr, "rm: %s: %s\n", path, strerror(e));
    status = 1;
}

/* read a line from the standard input; is it an affirmative response? */
static bool yes(void)
{
    static regex_t re;
    static int ready;           /* 0 not yet, 1 regex, -1 the fallback */
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

static bool ask(const char *fmt)
{
    fprintf(stderr, fmt, path);
    return yes();
}

/* would step 3 (or 2b) prompt for this file?  the prompt, or NULL */
static const char *prompt_for(int dirfd, const char *name, const struct stat *st, bool dir_descend)
{
    if (opt_f)
        return NULL;
    bool unwritable = !S_ISLNK(st->st_mode) &&
                      faccessat(dirfd, name, W_OK, AT_EACCESS) != 0 && errno == EACCES;
    if (opt_i)
        return dir_descend ? "rm: descend into directory %s? "
             : unwritable ? "rm: remove write-protected %s? " : "rm: remove %s? ";
    if (unwritable && stdin_tty)
        return dir_descend ? "rm: descend into write-protected directory %s? "
             : "rm: remove write-protected %s? ";
    return NULL;
}

static void removed(bool dir)
{
    if (opt_v && printf(dir ? "removed directory %s\n" : "removed %s\n", path) < 0)
        status = 1;
}

/* the stack of directories being removed */
struct level {
    int fd;                     /* -1 when closed */
    dev_t dev;
    ino_t ino;
};
static struct level *lv;
static size_t depth, lvcap, nopen;

static int push_level(int fd, const struct stat *st)
{
    if (depth == lvcap) {
        lvcap = lvcap ? lvcap * 2 : 16;
        lv = xrealloc(lv, lvcap * sizeof *lv);
    }
    lv[depth].fd = fd;
    lv[depth].dev = st->st_dev;
    lv[depth].ino = st->st_ino;
    depth++;
    if (++nopen > maxopen) {
        /* close the shallowest open level but keep the one just opened */
        for (size_t i = 0; i + 1 < depth; i++)
            if (lv[i].fd >= 0) {
                close(lv[i].fd);
                lv[i].fd = -1;
                nopen--;
                break;
            }
    }
    return 0;
}

/* leave the top level: make sure the one below is open (as ".." of the
   top), then close the top.  false if the directory below has moved. */
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

static bool remove_entry(int dirfd, const char *name, bool operand);

/* the names in an open directory, other than dot and dot-dot */
static char **read_names(int fd, size_t *count, int *error)
{
    int fd2 = dup(fd);
    DIR *d = fd2 >= 0 ? fdopendir(fd2) : NULL;
    char **names = NULL;
    size_t n = 0, cap = 0;
    *error = 0;
    if (!d) {
        *error = errno;
        if (fd2 >= 0)
            close(fd2);
        *count = 0;
        return NULL;
    }
    struct dirent *e;
    for (;;) {
        errno = 0;
        if (!(e = readdir(d))) {
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
            fprintf(stderr, "rm: out of memory\n");
            exit(1);
        }
    }
    closedir(d);
    *count = n;
    return names;
}

/* remove a directory and what is in it (step 2); true if it is gone */
static bool remove_tree(int dirfd, const char *name, const struct stat *st)
{
    const char *p = prompt_for(dirfd, name, st, true);
    int fd = openat(dirfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        int e = errno;
        /* an empty directory that cannot be read can still be removed */
        if (e == EACCES && (!opt_i || ask("rm: remove directory %s? "))) {
            if (unlinkat(dirfd, name, AT_REMOVEDIR) == 0) {
                removed(true);
                return true;
            }
        }
        err(NULL, e);
        return false;
    }
    struct stat fst;
    if (fstat(fd, &fst) != 0 || fst.st_dev != st->st_dev || fst.st_ino != st->st_ino) {
        close(fd);
        fprintf(stderr, "rm: %s: changed while being removed\n", path);
        status = 1;
        return false;
    }
    int rerr;
    size_t n;
    char **names = read_names(fd, &n, &rerr);
    if (n > 0 && p && !ask(p)) {
        for (size_t i = 0; i < n; i++)
            free(names[i]);
        free(names);
        close(fd);
        return false;
    }
    push_level(fd, &fst);
    bool all = true;
    if (rerr) {
        err("cannot read directory", rerr);
        all = false;
    }
    for (size_t i = 0; i < n; i++) {
        struct level *me = &lv[depth - 1];
        if (me->fd < 0) {
            /* reopened as we came back up: pop_level of the child does it */
            fprintf(stderr, "rm: %s: lost the directory\n", path);
            status = 1;
            all = false;
            free(names[i]);
            continue;
        }
        size_t old = path_push(names[i]);
        if (!remove_entry(me->fd, names[i], false))
            all = false;
        path_pop(old);
        free(names[i]);
    }
    free(names);
    if (!pop_level()) {
        fprintf(stderr, "rm: %s: the directory above it has moved\n", path);
        status = 1;
        /* the caller's descriptor is closed: nothing more can be done */
        return false;
    }
    /* the parent's descriptor may have been closed and reopened meanwhile */
    if (depth > 0)
        dirfd = lv[depth - 1].fd;
    if (!all)
        return false;
    if (opt_i && !ask("rm: remove directory %s? "))
        return false;
    if (unlinkat(dirfd, name, AT_REMOVEDIR) != 0) {
        err(NULL, errno);
        return false;
    }
    removed(true);
    return true;
}

/* the steps for one file, an operand or an entry of a directory being
   removed; true if it is gone */
static bool remove_entry(int dirfd, const char *name, bool operand)
{
    struct stat st;
    if (fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT && opt_f && operand)
            return true;
        err(NULL, errno);
        return false;
    }
    if (S_ISDIR(st.st_mode)) {
        if (opt_r)
            return remove_tree(dirfd, name, &st);
        if (!opt_d) {
            fprintf(stderr, "rm: %s: is a directory\n", path);
            status = 1;
            return false;
        }
    }
    const char *p = prompt_for(dirfd, name, &st, false);
    if (p && S_ISDIR(st.st_mode))
        p = opt_i ? "rm: remove directory %s? " : "rm: remove write-protected directory %s? ";
    if (p && !ask(p))
        return false;
    if (unlinkat(dirfd, name, S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0) != 0) {
        err(NULL, errno);
        return false;
    }
    removed(S_ISDIR(st.st_mode));
    return true;
}

/* is the last component of the operand dot or dot-dot? */
static bool dot_or_dotdot(const char *s)
{
    size_t n = strlen(s);
    while (n > 1 && s[n - 1] == '/')
        n--;
    size_t i = n;
    while (i > 0 && s[i - 1] != '/')
        i--;
    return (n - i == 1 && s[i] == '.') || (n - i == 2 && s[i] == '.' && s[i + 1] == '.');
}

static void usage(void)
{
    fprintf(stderr, "usage: rm [-diRrv] file...\n       rm -f [-diRrv] [file...]\n");
    exit(2);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "dfiRrv")) != -1) {
        switch (c) {
        case 'd': opt_d = true; break;
        case 'f': opt_f = true; opt_i = false; break;
        case 'i': opt_i = true; opt_f = false; break;
        case 'R': case 'r': opt_r = true; break;
        case 'v': opt_v = true; break;
        default: usage();
        }
    }
    argc -= optind;
    argv += optind;
    if (argc == 0) {
        if (opt_f)
            return 0;
        usage();
    }
    long om = sysconf(_SC_OPEN_MAX);
    if (om > 0 && (om - 10) / 2 < (long)maxopen)
        maxopen = om > 14 ? (size_t)(om - 10) / 2 : 2;
    stdin_tty = isatty(0);
    struct stat root;
    bool have_root = stat("/", &root) == 0;
    for (int i = 0; i < argc; i++) {
        pathlen = 0;
        path_push(argv[i]);
        if (dot_or_dotdot(argv[i])) {
            fprintf(stderr, "rm: %s: refusing to remove dot or dot-dot\n", argv[i]);
            status = 1;
            continue;
        }
        struct stat st;
        if (have_root && lstat(argv[i], &st) == 0 && st.st_dev == root.st_dev &&
            st.st_ino == root.st_ino) {
            fprintf(stderr, "rm: %s: refusing to remove the root directory\n", argv[i]);
            status = 1;
            continue;
        }
        remove_entry(AT_FDCWD, argv[i], true);
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        if (opt_v)
            fprintf(stderr, "rm: write error: %s\n", strerror(errno));
        status = 1;
    }
    return status;
}
