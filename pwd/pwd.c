/*
 * pwd — return working directory name  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis: pwd [-L|-P]
 *
 * -L (default): use $PWD if it is a valid absolute path naming the cwd
 * -P:           resolve symlinks via getcwd()
 */

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

static void pwd_physical(void)
{
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof buf) == NULL) {
        perror("pwd");
        exit(1);
    }
    puts(buf);
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
            puts(env);
            return 0;
        }
    }

    /* Fall back to physical */
    pwd_physical();
    return 0;
}
