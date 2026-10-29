/*
 * timeout.c - timeout: execute a utility with a time limit (POSIX.1-2024,
 * XCU timeout)
 *
 * Synopsis: timeout [-fp] [-k time] [-s signal_name] duration utility
 *                   [argument...]
 *
 * timeout makes itself a process group leader (unless -f), runs the
 * utility in a child process, and waits for it with sigtimedwait(): for
 * SIGCHLD, for the signals it passes on, and for the end of the time
 * limit (and of the -k time after the first signal).  A signal is sent to
 * the process group (to the child only with -f), with the disposition of
 * that signal in timeout set to be ignored while it is sent; SIGCONT
 * follows each signal, so that a stopped utility gets it.
 *
 * Choices where the standard leaves room:
 *  - The other processes are signaled through the process group.
 *  - SIGCONT is sent after every signal, not only to a stopped utility.
 *  - The signals passed on are those whose default action ends the
 *    process and that timeout did not inherit as ignored (SIGHUP, SIGINT,
 *    SIGQUIT, SIGALRM, SIGTERM, SIGUSR1, SIGUSR2, SIGPIPE, SIGXCPU, SIGXFSZ,
 *    SIGVTALRM, SIGPROF), and the -s signal.  A SIGALRM is passed on like
 *    the others.
 *  - SIGKILL and SIGSTOP cannot be ignored: to send them to its process
 *    group, timeout first goes back to the process group it was in; so the
 *    status is 124 with -s KILL and with -k.  When timeout led its process
 *    group from the start (as a job of an interactive shell), there is no
 *    other to go to: SIGKILL ends timeout too (the status is then as with
 *    -p, killed by SIGKILL), SIGSTOP stops it.
 *  - -s takes the name also with SIG before it, and a signal number.
 *  - A duration may start with its fraction (".5"); one longer than about
 *    68 years is taken as that long.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *progname = "timeout";

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

/* ---- signals ---- */

static const struct {
    const char *name;
    int sig;
} signames[] = {
    { "ABRT", SIGABRT }, { "ALRM", SIGALRM }, { "BUS", SIGBUS }, { "CHLD", SIGCHLD },
    { "CONT", SIGCONT }, { "FPE", SIGFPE }, { "HUP", SIGHUP }, { "ILL", SIGILL },
    { "INT", SIGINT }, { "KILL", SIGKILL }, { "PIPE", SIGPIPE }, { "QUIT", SIGQUIT },
    { "SEGV", SIGSEGV }, { "STOP", SIGSTOP }, { "SYS", SIGSYS }, { "TERM", SIGTERM },
    { "TRAP", SIGTRAP }, { "TSTP", SIGTSTP }, { "TTIN", SIGTTIN }, { "TTOU", SIGTTOU },
    { "URG", SIGURG }, { "USR1", SIGUSR1 }, { "USR2", SIGUSR2 }, { "VTALRM", SIGVTALRM },
    { "PROF", SIGPROF }, { "XCPU", SIGXCPU }, { "XFSZ", SIGXFSZ },
};

/* the signals passed on: the default action ends the process */
static const int passed[] = {
    SIGHUP, SIGINT, SIGQUIT, SIGALRM, SIGTERM, SIGUSR1, SIGUSR2, SIGPIPE,
    SIGXCPU, SIGXFSZ, SIGVTALRM, SIGPROF,
};
#define NPASSED (sizeof passed / sizeof passed[0])

static int signal_number(const char *s)
{
    if (isdigit((unsigned char)*s)) {
        char *end;
        long n = strtol(s, &end, 10);
        if (*end || n <= 0 || n > 64)
            return -1;
        for (size_t i = 0; i < sizeof signames / sizeof signames[0]; i++)
            if (signames[i].sig == n)
                return (int)n;
        return -1;
    }
    if (strncasecmp(s, "SIG", 3) == 0)
        s += 3;
    for (size_t i = 0; i < sizeof signames / sizeof signames[0]; i++)
        if (strcasecmp(s, signames[i].name) == 0)
            return signames[i].sig;
    return -1;
}

static void on_signal(int sig)
{
    (void)sig;                  /* only to keep the signal pending */
}

/* a duration in seconds; -1 if it is not one */
static double duration(const char *s)
{
    const char *p = s;
    bool digits = false;
    while (isdigit((unsigned char)*p))
        p++, digits = true;
    if (*p == '.') {
        p++;
        while (isdigit((unsigned char)*p))
            p++, digits = true;
    }
    if (!digits)
        return -1;
    double mult = 1;
    switch (*p) {
    case '\0': break;
    case 's': mult = 1; p++; break;
    case 'm': mult = 60; p++; break;
    case 'h': mult = 3600; p++; break;
    case 'd': mult = 86400; p++; break;
    default: return -1;
    }
    if (*p)
        return -1;
    double v = strtod(s, NULL) * mult;
    return v > 2147483647.0 ? 2147483647.0 : v;
}

static struct timespec now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t;
}

static struct timespec add(struct timespec t, double secs)
{
    time_t whole = (time_t)secs;      /* secs >= 0 */
    t.tv_sec += whole;
    t.tv_nsec += (long)((secs - (double)whole) * 1e9);
    if (t.tv_nsec >= 1000000000L) {
        t.tv_sec++;
        t.tv_nsec -= 1000000000L;
    }
    return t;
}

/* the time from now until t, at least 0 */
static struct timespec until(struct timespec t)
{
    struct timespec n = now(), d;
    d.tv_sec = t.tv_sec - n.tv_sec;
    d.tv_nsec = t.tv_nsec - n.tv_nsec;
    if (d.tv_nsec < 0) {
        d.tv_sec--;
        d.tv_nsec += 1000000000L;
    }
    if (d.tv_sec < 0)
        d.tv_sec = 0, d.tv_nsec = 0;
    return d;
}

static bool opt_f;
static pid_t child;
static pid_t orig_pgrp;         /* the process group timeout was in */
static bool moved;              /* timeout has gone back to it */
static struct sigaction handler;

/* send sig to the child, or to the process group with sig ignored here */
static void send_signal(int sig)
{
    if (opt_f) {
        kill(child, sig);
        return;
    }
    if (sig == SIGKILL || sig == SIGSTOP) {
        /* these cannot be ignored: timeout goes back to the process group
           it was in, if there was another, and signals the one it led */
        if (!moved && orig_pgrp != getpid() && setpgid(0, orig_pgrp) == 0)
            moved = true;
        kill(moved ? -getpid() : 0, sig);
        return;
    }
    if (moved) {
        kill(-getpid(), sig);
        return;
    }
    struct sigaction ign, old;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(sig, &ign, &old);
    kill(0, sig);
    /* the signal is blocked here, and a blocked signal may be kept pending
       even when it is ignored; setting SIG_IGN discards a pending one */
    sigaction(sig, &ign, NULL);
    sigaction(sig, &old, NULL);
}

static void usage(void)
{
    fprintf(stderr, "usage: timeout [-fp] [-k time] [-s signal_name] duration utility [argument...]\n");
    exit(125);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool opt_p = false;
    double ktime = 0;
    bool have_k = false;
    int sig = SIGTERM;
    int c;
    while ((c = getopt(argc, argv, "fk:ps:")) != -1) {
        switch (c) {
        case 'f': opt_f = true; break;
        case 'p': opt_p = true; break;
        case 'k':
            if ((ktime = duration(optarg)) < 0) {
                fprintf(stderr, "timeout: -k %s: not a valid time\n", optarg);
                return 125;
            }
            have_k = ktime > 0;
            break;
        case 's':
            if ((sig = signal_number(optarg)) < 0) {
                fprintf(stderr, "timeout: -s %s: not a signal\n", optarg);
                return 125;
            }
            break;
        default: usage();
        }
    }
    if (argc - optind < 2)
        usage();
    double limit = duration(argv[optind]);
    if (limit < 0) {
        fprintf(stderr, "timeout: %s: not a valid duration\n", argv[optind]);
        return 125;
    }
    char **uargv = argv + optind + 1;

    /* the signals: those passed on and SIGCHLD are blocked, and taken
       with sigtimedwait(); SIGTTIN and SIGTTOU are ignored */
    memset(&handler, 0, sizeof handler);
    handler.sa_handler = on_signal;
    sigemptyset(&handler.sa_mask);
    sigset_t set, oldmask;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    struct sigaction old_passed[NPASSED], old_chld, old_sig, old_ttin, old_ttou;
    bool have_passed[NPASSED];
    sigaction(sig, NULL, &old_sig);
    for (size_t i = 0; i < NPASSED; i++) {
        sigaction(passed[i], NULL, &old_passed[i]);
        have_passed[i] = old_passed[i].sa_handler != SIG_IGN || passed[i] == sig;
        if (have_passed[i])
            sigaddset(&set, passed[i]);
    }
    bool sig_passed = false;    /* the -s signal is among those passed on */
    for (size_t i = 0; i < NPASSED; i++)
        sig_passed |= passed[i] == sig;
    if (!sig_passed && sig != SIGKILL && sig != SIGSTOP && sig != SIGCHLD && sig != SIGCONT)
        sigaddset(&set, sig);
    sigprocmask(SIG_BLOCK, &set, &oldmask);
    for (int s = 1; s < 65; s++)
        if (sigismember(&set, s) == 1)
            sigaction(s, &handler, s == SIGCHLD ? &old_chld : NULL);
    struct sigaction ign;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGTTIN, &ign, &old_ttin);
    sigaction(SIGTTOU, &ign, &old_ttou);

    orig_pgrp = getpgrp();
    if (!opt_f && orig_pgrp != getpid())
        setpgid(0, 0);
    fflush(NULL);
    child = fork();
    if (child < 0) {
        fprintf(stderr, "timeout: fork: %s\n", strerror(errno));
        return 125;
    }
    if (child == 0) {
        /* the dispositions timeout inherited, but the -s signal's */
        for (size_t i = 0; i < NPASSED; i++)
            sigaction(passed[i], &old_passed[i], NULL);
        if (!sig_passed && sig != SIGKILL && sig != SIGSTOP)
            sigaction(sig, &old_sig, NULL);
        sigaction(SIGCHLD, &old_chld, NULL);
        sigaction(SIGTTIN, &old_ttin, NULL);
        sigaction(SIGTTOU, &old_ttou, NULL);
        if (sig != SIGKILL && sig != SIGSTOP)
            signal(sig, SIG_DFL);
        sigprocmask(SIG_SETMASK, &oldmask, NULL);
        _exit(exec_utility(uargv));
    }

    struct timespec deadline = add(now(), limit), kill_deadline = { 0, 0 };
    bool timed_out = false, first_sent = false, kill_sent = false;
    int ws;
    for (;;) {
        pid_t r = waitpid(child, &ws, WNOHANG);
        if (r == child)
            break;
        if (r < 0 && errno != EINTR) {
            fprintf(stderr, "timeout: wait: %s\n", strerror(errno));
            return 125;
        }
        struct timespec wait;
        const struct timespec *wp = NULL;
        if (!first_sent && limit > 0) {
            wait = until(deadline);
            wp = &wait;
        } else if (first_sent && have_k && !kill_sent) {
            wait = until(kill_deadline);
            wp = &wait;
        }
        siginfo_t info;
        int s = wp ? sigtimedwait(&set, &info, wp) : sigwaitinfo(&set, &info);
        if (s == SIGCHLD || (s < 0 && errno == EINTR))
            continue;
        if (s < 0) {
            if (errno != EAGAIN)
                continue;
            /* a time is up */
            if (!first_sent) {
                timed_out = true;
                first_sent = true;
                send_signal(sig);
                if (sig != SIGCONT)
                    send_signal(SIGCONT);
                kill_deadline = add(now(), ktime);
            } else {
                kill_sent = true;
                send_signal(SIGKILL);
            }
            continue;
        }
        /* a signal to pass on: the first signal, if it is the first */
        send_signal(s);
        if (s != SIGCONT)
            send_signal(SIGCONT);
        if (!first_sent) {
            first_sent = true;
            kill_deadline = add(now(), ktime);
        }
    }
    if (timed_out && !opt_p)
        return 124;
    if (WIFEXITED(ws))
        return WEXITSTATUS(ws);
    if (WIFSIGNALED(ws)) {
        int s = WTERMSIG(ws);
        struct rlimit rl = { 0, 0 };
        setrlimit(RLIMIT_CORE, &rl);
        signal(s, SIG_DFL);
        sigset_t one;
        sigemptyset(&one);
        sigaddset(&one, s);
        sigprocmask(SIG_UNBLOCK, &one, NULL);
        if (opt_f)
            kill(getpid(), s);
        else
            raise(s);
        return 128 + s;
    }
    return 125;
}
