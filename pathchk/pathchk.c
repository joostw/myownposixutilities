/*
 * pathchk.c - pathchk: check pathnames (POSIX.1-2024, XCU pathchk)
 *
 * Synopsis: pathchk [-p] [-P] pathname...
 *
 * Without -p, each pathname is checked against the file system: its
 * length against {PATH_MAX}, each component against {NAME_MAX} of the
 * directory that contains (or would contain) it, and each existing
 * directory it passes through for search permission; a component that
 * exists but is not a directory, and is followed by more, is an error
 * too.  Components that do not exist are checked against the limits of
 * the last directory that does.  -p checks instead against
 * {_POSIX_PATH_MAX} and {_POSIX_NAME_MAX} and the portable filename
 * character set; -P adds a check for empty pathnames and components that
 * start with '-'.
 *
 * Choices where the standard leaves room:
 *  - {PATH_MAX} and {_POSIX_PATH_MAX} count the terminating null byte, so
 *    a pathname of 256 bytes is too long for -p.
 *  - Byte sequences: the file system takes any bytes but the slash and
 *    the null byte, so none is reported as not valid.
 *  - A pathname that is empty is an error also without -P (it can name no
 *    file).
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

static bool opt_p, opt_P;

static bool problem(const char *path, const char *what, const char *comp, size_t len)
{
    if (comp)
        fprintf(stderr, "pathchk: %s: %s: %.*s\n", path, what, (int)len, comp);
    else
        fprintf(stderr, "pathchk: %s: %s\n", path, what);
    return false;
}

static bool portable_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-';
}

static bool check(const char *path)
{
    size_t len = strlen(path);
    bool ok = true;
    if (len == 0)
        return problem("", "an empty pathname", NULL, 0);
    if (opt_P) {
        for (const char *p = path; *p;) {
            while (*p == '/')
                p++;
            if (*p == '-')
                ok = problem(path, "a component starts with '-'", p, strcspn(p, "/"));
            p += strcspn(p, "/");
        }
    }
    if (opt_p) {
        if (len + 1 > _POSIX_PATH_MAX)
            ok = problem(path, "longer than {_POSIX_PATH_MAX}", NULL, 0);
        for (const char *p = path; *p;) {
            while (*p == '/')
                p++;
            size_t n = strcspn(p, "/");
            if (n > _POSIX_NAME_MAX)
                ok = problem(path, "a component longer than {_POSIX_NAME_MAX}", p, n);
            for (size_t i = 0; i < n; i++)
                if (!portable_char((unsigned char)p[i])) {
                    ok = problem(path, "a character outside the portable filename character set", p, n);
                    break;
                }
            p += n;
        }
        return ok;
    }
    /* against the file system */
    long pmax = pathconf(path[0] == '/' ? "/" : ".", _PC_PATH_MAX);
    if (pmax < 0)
#ifdef PATH_MAX
        pmax = PATH_MAX;
#else
        pmax = _POSIX_PATH_MAX;
#endif
    if ((long)len + 1 > pmax)
        ok = problem(path, "longer than {PATH_MAX}", NULL, 0);
    char *dir = malloc(len + 2);
    if (!dir) {
        fprintf(stderr, "pathchk: out of memory\n");
        exit(2);
    }
    /* dir: the directory that contains the component being checked */
    strcpy(dir, path[0] == '/' ? "/" : ".");
    char *built = malloc(len + 2);
    if (!built) {
        fprintf(stderr, "pathchk: out of memory\n");
        exit(2);
    }
    built[0] = '\0';
    if (path[0] == '/')
        strcpy(built, "/");
    bool exists = true;         /* everything so far exists */
    long nmax = -1;
    for (const char *p = path; *p;) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        size_t n = strcspn(p, "/");
        bool more = p[n] != '\0' && p[n + strspn(p + n, "/")] != '\0';
        if (exists) {
            struct stat st;
            if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
                if (access(dir, X_OK) != 0 && errno == EACCES) {
                    ok = problem(path, "a directory that cannot be searched", dir, strlen(dir));
                    break;
                }
                errno = 0;
                long m = pathconf(dir, _PC_NAME_MAX);
                if (m > 0 || errno == 0)
                    nmax = m;
            }
        }
        if (nmax > 0 && (long)n > nmax)
            ok = problem(path, "a component longer than {NAME_MAX}", p, n);
        /* the next containing directory */
        size_t bl = strlen(built);
        if (bl && built[bl - 1] != '/')
            built[bl++] = '/';
        memcpy(built + bl, p, n);
        built[bl + n] = '\0';
        if (exists) {
            struct stat st;
            if (lstat(built, &st) != 0) {
                if (errno != ENOENT && errno != ENAMETOOLONG) {
                    ok = problem(path, strerror(errno), built, strlen(built));
                    break;
                }
                exists = false;
            } else if (more) {
                struct stat t;
                if (stat(built, &t) != 0 || !S_ISDIR(t.st_mode)) {
                    ok = problem(path, "not a directory", built, strlen(built));
                    break;
                }
            }
        }
        strcpy(dir, built);
        p += n;
    }
    free(dir);
    free(built);
    return ok;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "pP")) != -1) {
        if (c == 'p')
            opt_p = true;
        else if (c == 'P')
            opt_P = true;
        else {
            fprintf(stderr, "usage: pathchk [-p] [-P] pathname...\n");
            return 2;
        }
    }
    if (optind == argc) {
        fprintf(stderr, "usage: pathchk [-p] [-P] pathname...\n");
        return 2;
    }
    int status = 0;
    for (int i = optind; i < argc; i++)
        if (!check(argv[i]))
            status = 1;
    return status;
}
