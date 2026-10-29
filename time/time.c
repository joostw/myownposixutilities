/*
 * time.c - time: time a simple command (POSIX.1-2024, XCU time)
 *
 * Synopsis: time [-p] utility [argument...]
 *
 * The utility runs in a child process; the elapsed time is measured with
 * clock_gettime(CLOCK_MONOTONIC), the user and system times are the
 * children's times of times().  While it waits, time ignores SIGINT and
 * SIGQUIT (the child has them as time found them).
 *
 * Choices where the standard leaves room:
 *  - Without -p the format is that of -p: "real %f\nuser %f\nsys %f\n",
 *    with two digits after the radix character (the clock tick is a
 *    hundredth of a second or coarser on the systems this is made for;
 *    with more ticks per second, enough digits for them).
 *  - When the utility is killed by a signal, a message says so, the times
 *    are written, and time then ends itself by the same signal (with no
 *    core image), so that its status is that of the utility.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *progname = "time";

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
    int c;
    while ((c = getopt(argc, argv, "p")) != -1) {
        if (c != 'p') {
            fprintf(stderr, "usage: time [-p] utility [argument...]\n");
            return 125;
        }
    }
    if (optind == argc) {
        fprintf(stderr, "usage: time [-p] utility [argument...]\n");
        return 125;
    }
    long tick = sysconf(_SC_CLK_TCK);
    int digits = 2;
    for (long t = 100; tick > t && digits < 9; t *= 10)
        digits++;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    struct sigaction ign, old_int, old_quit;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGINT, &ign, &old_int);
    sigaction(SIGQUIT, &ign, &old_quit);
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "time: fork: %s\n", strerror(errno));
        return 125;
    }
    if (pid == 0) {
        sigaction(SIGINT, &old_int, NULL);
        sigaction(SIGQUIT, &old_quit, NULL);
        _exit(exec_utility(argv + optind));
    }
    int ws;
    while (waitpid(pid, &ws, 0) < 0)
        if (errno != EINTR) {
            fprintf(stderr, "time: wait: %s\n", strerror(errno));
            return 125;
        }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    struct tms tm;
    times(&tm);
    double real = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    double user = (double)tm.tms_cutime / (double)tick, sys = (double)tm.tms_cstime / (double)tick;
    if (WIFSIGNALED(ws))
        fprintf(stderr, "time: %s: terminated by signal %d\n", argv[optind], WTERMSIG(ws));
    fprintf(stderr, "real %.*f\nuser %.*f\nsys %.*f\n", digits, real, digits, user, digits, sys);
    if (WIFSIGNALED(ws)) {
        int sig = WTERMSIG(ws);
        struct rlimit rl = { 0, 0 };
        setrlimit(RLIMIT_CORE, &rl);
        signal(sig, SIG_DFL);
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, sig);
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        raise(sig);
        return 128 + sig;
    }
    return WIFEXITED(ws) ? WEXITSTATUS(ws) : 125;
}
