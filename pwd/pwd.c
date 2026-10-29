/*
 * pwd.c - pwd: return working directory name (POSIX.1-2024, XCU pwd)
 *
 * Synopsis: pwd [-L|-P]
 *
 * -L (default): use $PWD if it is a valid absolute path naming the cwd
 * -P:           resolve symlinks via getcwd()
 */

#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Return true if path contains no '.' or '..' components. */
static bool no_dot(const char *p)
{
    while (*p) {
        while (*p == '/') p++;
        if (p[0] == '.' && (p[1] == '/' || p[1] == '\0')) return false;
        if (p[0] == '.' && p[1] == '.' &&
            (p[2] == '/' || p[2] == '\0')) return false;
        while (*p && *p != '/') p++;
    }
    return true;
}

#include <errno.h>

static void out(const char *s)
{
    if (puts(s) == EOF || fflush(stdout) == EOF) {
        perror("pwd: write error");
        exit(1);
    }
}

/* the physical pathname: getcwd() with a buffer that grows, so that
   directories deeper than PATH_MAX bytes work where the system allows */
static void pwd_physical(void)
{
    size_t cap = PATH_MAX;
    char *buf = NULL;
    for (;;) {
        char *nb = realloc(buf, cap);
        if (!nb) {
            perror("pwd");
            exit(1);
        }
        buf = nb;
        if (getcwd(buf, cap) != NULL)
            break;
        if (errno != ERANGE) {
            perror("pwd");
            exit(1);
        }
        cap *= 2;
    }
    out(buf);
    free(buf);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    bool logical = true; /* default: -L */

    int opt;
    while ((opt = getopt(argc, argv, "LP")) != -1) {
        switch (opt) {
        case 'L': logical = true;  break;
        case 'P': logical = false; break;
        default:
            fprintf(stderr, "usage: pwd [-L|-P]\n");
            return 1;
        }
    }

    if (!logical) { pwd_physical(); return 0; }

    /* -L: try $PWD first */
    const char *env = getenv("PWD");
    if (env && env[0] == '/' && no_dot(env) &&
        strlen(env) < PATH_MAX) {
        struct stat es, cs;
        if (stat(env, &es) == 0 && stat(".", &cs) == 0 &&
            es.st_dev == cs.st_dev && es.st_ino == cs.st_ino) {
            out(env);
            return 0;
        }
    }

    /* Fall back to physical */
    pwd_physical();
    return 0;
}
