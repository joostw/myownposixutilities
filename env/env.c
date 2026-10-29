/*
 * env.c - env: set the environment for command invocation (POSIX.1-2024,
 * XCU env)
 *
 * Synopsis: env [-i] [name=value]... [utility [argument...]]
 *
 * The options are read by hand, not with getopt(): scanning stops at the
 * first argument that is not an option, so the utility's own options are
 * never taken for env's.  The utility is looked for in the PATH of the new
 * environment (when it has none, in the value of getconf PATH); a file
 * found but not executable as a program (ENOEXEC) is run by sh, as a
 * shell script.
 *
 * Choices where the standard leaves room:
 *  - A first argument "-" acts as -i (the historical meaning).
 *  - A name=value operand replaces an earlier entry of the same name (from
 *    the inherited environment or an earlier operand); an operand whose
 *    name is empty ("=x") is put in the environment as it is.
 *  - Errors of env itself (a bad option, a write error) exit with 125.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

static char **env;
static size_t nenv, capenv;

static void add(char *entry)
{
    size_t nl = strcspn(entry, "=");
    for (size_t i = 0; i < nenv; i++)
        if (strncmp(env[i], entry, nl) == 0 && env[i][nl] == '=') {
            env[i] = entry;
            return;
        }
    if (nenv + 2 > capenv) {
        capenv = capenv ? capenv * 2 : 64;
        if (!(env = realloc(env, capenv * sizeof *env))) {
            fprintf(stderr, "env: out of memory\n");
            exit(125);
        }
    }
    env[nenv++] = entry;
    env[nenv] = NULL;
}

static const char *getvar(const char *name)
{
    size_t nl = strlen(name);
    for (size_t i = 0; i < nenv; i++)
        if (strncmp(env[i], name, nl) == 0 && env[i][nl] == '=')
            return env[i] + nl + 1;
    return NULL;
}

/* exec file; on ENOEXEC, as a shell script */
static void try_exec(const char *file, char **argv)
{
    execve(file, argv, env);
    if (errno == ENOEXEC) {
        int n = 0;
        while (argv[n])
            n++;
        char **sargv = malloc((size_t)(n + 2) * sizeof *sargv);
        if (!sargv)
            return;
        sargv[0] = "sh";
        sargv[1] = (char *)file;
        for (int i = 1; i <= n; i++)
            sargv[i + 1] = argv[i];
        /* sh from the standard PATH */
        size_t len = confstr(_CS_PATH, NULL, 0);
        char *path = len ? malloc(len) : NULL;
        if (path) {
            confstr(_CS_PATH, path, len);
            for (char *dir = strtok(path, ":"); dir; dir = strtok(NULL, ":")) {
                char sh[4096];
                snprintf(sh, sizeof sh, "%s/sh", dir);
                execve(sh, sargv, env);
            }
            free(path);
        }
        execve("/bin/sh", sargv, env);
        free(sargv);
        errno = ENOEXEC;
    }
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool clear = false;
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (strcmp(a, "-") == 0) {
            clear = true;
            continue;
        }
        if (a[0] != '-')
            break;
        for (const char *p = a + 1; *p; p++)
            if (*p != 'i') {
                fprintf(stderr, "env: -%c: unknown option\n"
                                "usage: env [-i] [name=value]... [utility [argument...]]\n", *p);
                return 125;
            }
        clear = true;
    }
    capenv = 64;
    if (!(env = malloc(capenv * sizeof *env))) {
        fprintf(stderr, "env: out of memory\n");
        return 125;
    }
    env[0] = NULL;
    if (!clear)
        for (char **e = environ; *e; e++)
            add(*e);
    for (; i < argc && strchr(argv[i], '='); i++)
        add(argv[i]);

    if (i == argc) {
        for (size_t k = 0; k < nenv; k++)
            if (puts(env[k]) == EOF)
                break;
        if (fflush(stdout) != 0 || ferror(stdout)) {
            fprintf(stderr, "env: write error: %s\n", strerror(errno));
            return 125;
        }
        return 0;
    }

    const char *utility = argv[i];
    char **uargv = argv + i;
    if (strchr(utility, '/')) {
        try_exec(utility, uargv);
        int e = errno;
        fprintf(stderr, "env: %s: %s\n", utility, strerror(e));
        return e == ENOENT || e == ENOTDIR ? 127 : 126;
    }
    const char *path = getvar("PATH");
    char *defpath = NULL;
    if (!path) {
        size_t len = confstr(_CS_PATH, NULL, 0);
        if (len && (defpath = malloc(len)))
            confstr(_CS_PATH, defpath, len);
        path = defpath ? defpath : "/usr/bin:/bin";
    }
    size_t ul = strlen(utility);
    int saved = ENOENT;
    const char *p = path;
    for (;;) {
        const char *colon = strchr(p, ':');
        size_t dl = colon ? (size_t)(colon - p) : strlen(p);
        char *file = malloc(dl + ul + 2);
        if (!file) {
            fprintf(stderr, "env: out of memory\n");
            return 125;
        }
        if (dl == 0)
            memcpy(file, utility, ul + 1);      /* an empty entry: the current directory */
        else {
            memcpy(file, p, dl);
            file[dl] = '/';
            memcpy(file + dl + 1, utility, ul + 1);
        }
        try_exec(file, uargv);
        /* a file that is there but cannot be run decides the status */
        /* a file that is there but cannot be run decides the status; a
           directory that cannot be searched does not */
        int e = errno;
        struct stat st;
        if (e != ENOENT && e != ENOTDIR && !(e == EACCES && stat(file, &st) != 0))
            saved = e;
        free(file);
        if (!colon)
            break;
        p = colon + 1;
    }
    fprintf(stderr, "env: %s: %s\n", utility, saved == ENOENT ? "not found" : strerror(saved));
    free(defpath);
    return saved == ENOENT ? 127 : 126;
}
