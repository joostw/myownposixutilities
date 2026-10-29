/*
 * realpath.c - realpath: resolve a pathname (POSIX.1-2024, XCU realpath)
 *
 * Synopsis: realpath [-E|-e] file
 *
 * When realpath() succeeds, its result is written.  Otherwise, with -E
 * (and without options) and an ENOENT error, the pathname is expanded:
 * its directory part is resolved with realpath(), which must give an
 * existing directory, and its last component (trailing slashes ignored)
 * is appended; a last component that is a symbolic link is followed in the
 * same way, as many times as it takes (at most {SYMLOOP_MAX}, or 40).
 *
 * Choices where the standard leaves room:
 *  - Without options, realpath acts as with -E: the last component need
 *    not exist.
 *  - A last component "." or ".." of a missing directory fails (ENOENT).
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *xstrdup(const char *s)
{
    char *p = strdup(s);
    if (!p) {
        fprintf(stderr, "realpath: out of memory\n");
        exit(1);
    }
    return p;
}

/* expand a pathname whose realpath() failed with ENOENT; NULL (errno
   set) if that cannot be done */
static char *expand(const char *path, int depth)
{
    long max = sysconf(_SC_SYMLOOP_MAX);
    if (depth > (max > 0 ? max : 40)) {
        errno = ELOOP;
        return NULL;
    }
    char *p = xstrdup(path);
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/')
        p[--n] = '\0';          /* trailing slashes that are not leading ones */
    char *slash = strrchr(p, '/');
    const char *dir, *last;
    if (!slash) {
        dir = ".";
        last = p;
    } else if (slash == p) {
        dir = "/";
        last = p + 1;
    } else {
        *slash = '\0';
        dir = p;
        last = slash + 1;
    }
    if (strcmp(last, ".") == 0 || strcmp(last, "..") == 0 || *last == '\0') {
        free(p);
        errno = ENOENT;
        return NULL;
    }
    char *rdir = realpath(dir, NULL);
    struct stat st;
    if (!rdir || stat(rdir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        if (rdir) {
            free(rdir);
            errno = ENOTDIR;
        }
        free(p);
        return NULL;
    }
    size_t dl = strlen(rdir);
    char *full = malloc(dl + strlen(last) + 2);
    if (!full) {
        fprintf(stderr, "realpath: out of memory\n");
        exit(1);
    }
    strcpy(full, rdir);
    if (dl == 0 || rdir[dl - 1] != '/')
        strcat(full, "/");
    strcat(full, last);
    free(rdir);
    free(p);
    if (lstat(full, &st) != 0) {
        if (errno == ENOENT)
            return full;        /* the last component does not exist */
        free(full);
        return NULL;
    }
    if (!S_ISLNK(st.st_mode)) {
        /* it exists after all (it appeared meanwhile): resolve it */
        char *r = realpath(full, NULL);
        free(full);
        return r;
    }
    /* a dangling symbolic link: follow its contents */
    size_t cap = 256;
    char *target = NULL;
    ssize_t len;
    for (;;) {
        target = realloc(target, cap);
        if (!target) {
            fprintf(stderr, "realpath: out of memory\n");
            exit(1);
        }
        len = readlink(full, target, cap);
        if (len < 0 || (size_t)len < cap)
            break;
        cap *= 2;
    }
    if (len < 0) {
        free(target);
        free(full);
        return NULL;
    }
    target[len] = '\0';
    char *next;
    if (target[0] == '/')
        next = xstrdup(target);
    else {
        char *s = strrchr(full, '/');
        size_t pl = (size_t)(s - full);
        next = malloc(pl + strlen(target) + 2);
        if (!next) {
            fprintf(stderr, "realpath: out of memory\n");
            exit(1);
        }
        memcpy(next, full, pl);
        next[pl] = '/';
        strcpy(next + pl + 1, target);
    }
    free(target);
    free(full);
    char *r = realpath(next, NULL);
    if (!r && errno == ENOENT)
        r = expand(next, depth + 1);
    free(next);
    return r;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    char mode = 'E';
    int c;
    while ((c = getopt(argc, argv, "Ee")) != -1) {
        if (c == 'E' || c == 'e')
            mode = (char)c;
        else {
            fprintf(stderr, "usage: realpath [-E|-e] file\n");
            return 2;
        }
    }
    if (argc - optind != 1) {
        fprintf(stderr, "usage: realpath [-E|-e] file\n");
        return 2;
    }
    const char *file = argv[optind];
    if (!*file) {
        fprintf(stderr, "realpath: an empty pathname\n");
        return 1;
    }
    char *r = realpath(file, NULL);
    if (!r && errno == ENOENT && mode == 'E')
        r = expand(file, 0);
    if (!r) {
        fprintf(stderr, "realpath: %s: %s\n", file, strerror(errno));
        return 1;
    }
    if (printf("%s\n", r) < 0 || fflush(stdout) != 0) {
        fprintf(stderr, "realpath: write error: %s\n", strerror(errno));
        return 1;
    }
    free(r);
    return 0;
}
