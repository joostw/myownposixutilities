/*
 * nice.c - nice: invoke a utility with an altered nice value
 * (POSIX.1-2024, XCU nice)
 *
 * Synopsis: nice [-n increment] utility [argument...]
 *
 * nice() is called with the increment, then the utility is executed: it
 * is searched for in PATH as execvp() does, a file that is not a program
 * is run by sh, and the status is 127 if it is not found, 126 if it is
 * found but cannot be executed.
 *
 * Choices where the standard leaves room:
 *  - Without -n the increment is 10.
 *  - When nice() fails for want of privileges, a warning is written and the
 *    utility is run anyway, as the standard requires; other failures too.
 *  - The increment is a decimal integer with an optional sign; one out of
 *    the range of int is an error.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *progname = "nice";

/* ---- executing the utility (as execvp(), with the statuses of XCU 2.9.1.1) ---- */

/* exec file with argv; on ENOEXEC, as a script of sh */
static void try_exec(const char *file, char **argv)
{
    execv(file, argv);
    if (errno != ENOEXEC)
        return;
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
            execv(sh, sargv);
        }
        free(path);
    }
    execv("/bin/sh", sargv);
    free(sargv);
    errno = ENOEXEC;
}

/* execute argv[0], searched for in PATH unless it has a '/'; on failure,
   a diagnostic and the status: 127 if it is not found, 126 if it is found
   but cannot be executed */
static int exec_utility(char **argv)
{
    const char *utility = argv[0];
    int saved = ENOENT;
    if (strchr(utility, '/')) {
        try_exec(utility, argv);
        saved = errno;
    } else {
        const char *path = getenv("PATH");
        char *defpath = NULL;
        if (!path) {
            size_t len = confstr(_CS_PATH, NULL, 0);
            if (len && (defpath = malloc(len)))
                confstr(_CS_PATH, defpath, len);
            path = defpath ? defpath : "/usr/bin:/bin";
        }
        size_t ul = strlen(utility);
        for (const char *p = path;;) {
            const char *colon = strchr(p, ':');
            size_t dl = colon ? (size_t)(colon - p) : strlen(p);
            char *file = malloc(dl + ul + 2);
            if (!file) {
                saved = ENOMEM;
                break;
            }
            if (dl == 0)
                memcpy(file, utility, ul + 1);  /* an empty entry: the current directory */
            else {
                memcpy(file, p, dl);
                file[dl] = '/';
                memcpy(file + dl + 1, utility, ul + 1);
            }
            try_exec(file, argv);
            int e = errno;
            /* a file that is there but cannot be run decides the status; a
               directory that cannot be searched does not */
            struct stat st;
            if (e != ENOENT && e != ENOTDIR && !(e == EACCES && stat(file, &st) != 0))
                saved = e;
            free(file);
            if (!colon)
                break;
            p = colon + 1;
        }
        free(defpath);
    }
    if (saved == ENOENT || saved == ENOTDIR) {
        fprintf(stderr, "%s: %s: not found\n", progname, utility);
        return 127;
    }
    fprintf(stderr, "%s: %s: %s\n", progname, utility, strerror(saved));
    return 126;
}

static void usage(void)
{
    fprintf(stderr, "usage: nice [-n increment] utility [argument...]\n");
    exit(125);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    long incr = 10;
    int c;
    while ((c = getopt(argc, argv, "n:")) != -1) {
        if (c != 'n')
            usage();
        const char *p = optarg;
        if (*p == '-' || *p == '+')
            p++;
        char *end;
        errno = 0;
        incr = strtol(optarg, &end, 10);
        if (!isdigit((unsigned char)*p) || *end || errno || incr < INT_MIN || incr > INT_MAX) {
            fprintf(stderr, "nice: -n %s: not a valid increment\n", optarg);
            return 125;
        }
    }
    if (optind == argc)
        usage();
    errno = 0;
    if (nice((int)incr) == -1 && errno != 0)
        fprintf(stderr, "nice: cannot change the nice value: %s\n", strerror(errno));
    return exec_utility(argv + optind);
}
