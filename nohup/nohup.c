/*
 * nohup.c - nohup: invoke a utility immune to hangups (POSIX.1-2024,
 * XCU nohup)
 *
 * Synopsis: nohup utility [argument...]
 *
 * SIGHUP is set to be ignored, the standard output and standard error are
 * redirected as the standard says (to nohup.out in the working directory,
 * or in $HOME, created with the permissions rw for the owner only), and
 * the utility is executed (searched for in PATH as execvp() does).
 *
 * Choices where the standard leaves room:
 *  - A terminal on the standard input is replaced by /dev/null.
 *  - The message is "nohup: appending output to FILE".
 *  - A first "--" is discarded.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* open nohup.out in dir (NULL: the working directory) for appending */
static int open_out(const char *dir, char **name)
{
    const char *base = "nohup.out";
    char *path;
    if (dir) {
        size_t n = strlen(dir) + strlen(base) + 2;
        if (!(path = malloc(n)))
            return -1;
        snprintf(path, n, "%s/%s", dir, base);
    } else if (!(path = strdup(base)))
        return -1;
    /* created: exactly rw for the owner, whatever the mask */
    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd >= 0)
        fchmod(fd, S_IRUSR | S_IWUSR);
    else if (errno == EEXIST)
        fd = open(path, O_WRONLY | O_APPEND);
    if (fd < 0) {
        free(path);
        return -1;
    }
    *name = path;
    return fd;
}

static const char *progname = "nohup";

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

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int i = 1;
    if (i < argc && strcmp(argv[i], "--") == 0)
        i++;
    if (i >= argc) {
        fprintf(stderr, "usage: nohup utility [argument...]\n");
        return 127;
    }
    bool out_tty = isatty(1);
    bool out_open = fcntl(1, F_GETFD) != -1;
    bool err_tty = isatty(2);
    if (isatty(0)) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd >= 0 && fd != 0) {
            dup2(fd, 0);
            close(fd);
        }
    }
    int outfd = -1;
    char *name = NULL;
    if (out_tty || (err_tty && !out_open)) {
        outfd = open_out(NULL, &name);
        if (outfd < 0) {
            const char *home = getenv("HOME");
            if (home && *home)
                outfd = open_out(home, &name);
        }
        if (outfd < 0) {
            fprintf(stderr, "nohup: cannot open nohup.out: %s\n", strerror(errno));
            return 127;
        }
        /* not in the place of a closed standard descriptor */
        if (outfd <= 2) {
            int fd = fcntl(outfd, F_DUPFD, 3);
            close(outfd);
            if ((outfd = fd) < 0) {
                fprintf(stderr, "nohup: %s\n", strerror(errno));
                return 127;
            }
        }
        fprintf(stderr, "nohup: appending output to %s\n", name);
    }
    if (out_tty && dup2(outfd, 1) < 0) {
        fprintf(stderr, "nohup: %s\n", strerror(errno));
        return 127;
    }
    if (err_tty) {
        int target = (out_open && !out_tty) ? 1 : outfd;
        if (target < 0)
            target = 1;
        if (dup2(target, 2) < 0)
            return 127;
    }
    if (outfd > 2)
        close(outfd);
    signal(SIGHUP, SIG_IGN);
    return exec_utility(argv + i);
}
