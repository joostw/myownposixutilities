/*
 * atd.c - atd: run the jobs of at and batch
 *
 * Synopsis: atd [-fs]
 *
 * Not a utility of POSIX: the daemon that the at utility needs.  It scans
 * the spool directory (spool.c) and runs each job whose time has come, in
 * a new session (no controlling terminal) with the user's IDs (when atd
 * runs as root), the job file as the shell's standard input, and the
 * standard output and standard error into a file, which is mailed to the
 * user (mailx -s SUBJECT USER) when it is not empty, or always when the
 * job was submitted with -m.  The jobs of queue b (batch) are run one at
 * a time, in order; the other queues as a.  A job is renamed "=ID" while
 * it runs and removed when it is done.
 *
 * Options:
 *   -f  stay in the foreground (diagnostics to the standard error)
 *   -s  run the jobs that are due once, wait for them, and exit
 *
 * Without -s, atd scans the spool again at least once a minute.  When it
 * does not run as root, it runs only the jobs of its own user.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "spool.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* not in POSIX; declared as BSD and glibc have it */
extern int setgroups(size_t, const gid_t *);

static bool opt_f, opt_s;

static void say(const char *fmt, const char *a, const char *b)
{
    if (!opt_f && !opt_s)
        return;
    fprintf(stderr, "atd: ");
    fprintf(stderr, fmt, a, b);
    fputc('\n', stderr);
}

/* a running job */
struct run {
    pid_t pid;
    struct job job;
    char out[4096];             /* the output file */
    char running[4096];         /* the job file, renamed */
};
static struct run *runs;
static size_t nruns;

static bool batch_running(void)
{
    for (size_t i = 0; i < nruns; i++)
        if (runs[i].job.queue == 'b')
            return true;
    return false;
}

/* mail the output (or the notice) to the job's owner */
static void mail_output(const struct run *r)
{
    struct stat st;
    bool have_output = stat(r->out, &st) == 0 && st.st_size > 0;
    if (!have_output && !r->job.mail)
        return;
    struct passwd *pw = getpwuid(r->job.owner);
    if (!pw) {
        say("job %s: the owner has no user database entry%s", r->job.id, "");
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        say("fork: %s%s", strerror(errno), "");
        return;
    }
    if (pid == 0) {
        int fd = have_output ? open(r->out, O_RDONLY) : -1;
        if (fd < 0) {
            /* the notice, from a pipe */
            int p[2];
            if (pipe(p) != 0)
                _exit(1);
            char msg[256];
            int n = snprintf(msg, sizeof msg, "Job %s completed; it wrote no output.\n", r->job.id);
            if (write(p[1], msg, (size_t)n) != n)
                _exit(1);
            close(p[1]);
            fd = p[0];
        }
        dup2(fd, 0);
        if (fd != 0)
            close(fd);
        char subject[128];
        snprintf(subject, sizeof subject, "Output from your job %s", r->job.id);
        execlp("mailx", "mailx", "-s", subject, pw->pw_name, (char *)NULL);
        _exit(127);
    }
    int ws;
    while (waitpid(pid, &ws, 0) < 0 && errno == EINTR)
        ;
    if (!WIFEXITED(ws) || WEXITSTATUS(ws) != 0)
        say("job %s: mailx failed%s", r->job.id, "");
}

static void start(const struct job *j, const char *path)
{
    const char *spool = spool_dir();
    struct run r;
    memset(&r, 0, sizeof r);
    r.job = *j;
    snprintf(r.running, sizeof r.running, "%s/=%s", spool, j->id);
    snprintf(r.out, sizeof r.out, "%s/=%s.out", spool, j->id);
    if (rename(path, r.running) != 0) {
        say("job %s: %s", j->id, strerror(errno));
        return;
    }
    struct passwd *pw = getpwuid(j->owner);
    if (!pw) {
        say("job %s: the owner has no user database entry%s", j->id, "");
        unlink(r.running);
        return;
    }
    int in = open(r.running, O_RDONLY);
    int out = open(r.out, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (in < 0 || out < 0) {
        say("job %s: %s", j->id, strerror(errno));
        unlink(r.running);
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        say("fork: %s%s", strerror(errno), "");
        close(in);
        close(out);
        rename(r.running, path);
        return;
    }
    if (pid == 0) {
        setsid();
        dup2(in, 0);
        dup2(out, 1);
        dup2(out, 2);
        if (in > 2)
            close(in);
        if (out > 2)
            close(out);
        if (geteuid() == 0) {
            /* the user's groups: the entry's, and those listing the user */
            gid_t groups[256];
            size_t n = 0;
            groups[n++] = pw->pw_gid;
            struct group *gr;
            setgrent();
            while ((gr = getgrent()) != NULL && n < 256)
                for (char **m = gr->gr_mem; m && *m; m++)
                    if (strcmp(*m, pw->pw_name) == 0) {
                        groups[n++] = gr->gr_gid;
                        break;
                    }
            endgrent();
            if (setgroups(n, groups) != 0 || setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0) {
                fprintf(stderr, "atd: cannot take the user's IDs: %s\n", strerror(errno));
                _exit(1);
            }
        }
        /* a default environment; the job sets its own */
        extern char **environ;
        static char *empty[] = { NULL };
        environ = empty;
        size_t len = confstr(_CS_PATH, NULL, 0);
        char *path_v = len ? malloc(len) : NULL;
        if (path_v)
            confstr(_CS_PATH, path_v, len);
        setenv("PATH", path_v ? path_v : "/usr/bin:/bin", 1);
        setenv("HOME", pw->pw_dir, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("USER", pw->pw_name, 1);
        setenv("SHELL", j->shell, 1);
        if (chdir(pw->pw_dir) != 0 && chdir("/") != 0)
            _exit(1);
        signal(SIGHUP, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        const char *shell = j->shell;
        if (strcmp(shell, "sh") == 0)
            execvp("sh", (char *[]){ "sh", NULL });
        execvp(shell, (char *[]){ (char *)shell, NULL });
        fprintf(stderr, "atd: %s: %s\n", shell, strerror(errno));
        _exit(127);
    }
    close(in);
    close(out);
    r.pid = pid;
    struct run *nr = realloc(runs, (nruns + 1) * sizeof *runs);
    if (!nr) {
        say("out of memory%s%s", "", "");
        return;
    }
    runs = nr;
    runs[nruns++] = r;
    say("job %s started%s", j->id, "");
}

/* the finished jobs: mail, clean up; wait for them if block */
static void reap(bool block)
{
    for (;;) {
        if (nruns == 0)
            return;
        int ws;
        pid_t pid = waitpid(-1, &ws, block ? 0 : WNOHANG);
        if (pid <= 0) {
            if (pid < 0 && errno == EINTR)
                continue;
            return;
        }
        for (size_t i = 0; i < nruns; i++)
            if (runs[i].pid == pid) {
                mail_output(&runs[i]);
                unlink(runs[i].out);
                unlink(runs[i].running);
                say("job %s done%s", runs[i].job.id, "");
                runs[i] = runs[--nruns];
                break;
            }
    }
}

static int cmp_jobs(const void *a, const void *b)
{
    const struct job *x = a, *y = b;
    if (x->when != y->when)
        return x->when < y->when ? -1 : 1;
    long nx = atol(x->id), ny = atol(y->id);
    return nx < ny ? -1 : nx > ny;
}

/* run the jobs that are due; the time of the next one (0 if none) */
static time_t scan(void)
{
    const char *spool = spool_dir();
    DIR *d = opendir(spool);
    if (!d) {
        say("%s: %s", spool, strerror(errno));
        return 0;
    }
    struct job *jobs = NULL;
    size_t njobs = 0;
    struct dirent *e;
    uid_t me = geteuid();
    while ((e = readdir(d)) != NULL) {
        char path[4096];
        struct job j;
        snprintf(path, sizeof path, "%s/%s", spool, e->d_name);
        if (!job_read(path, &j) || (me != 0 && j.owner != me))
            continue;
        struct job *nj = realloc(jobs, (njobs + 1) * sizeof *jobs);
        if (!nj)
            break;
        jobs = nj;
        jobs[njobs++] = j;
    }
    closedir(d);
    qsort(jobs, njobs, sizeof *jobs, cmp_jobs);
    time_t now = time(NULL), next_time = 0;
    for (size_t i = 0; i < njobs; i++) {
        struct job *j = &jobs[i];
        if (j->when > now) {
            if (!next_time || j->when < next_time)
                next_time = j->when;
            continue;
        }
        if (j->queue == 'b' && batch_running()) {
            next_time = now + 1;
            continue;
        }
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", spool, j->id);
        start(j, path);
    }
    free(jobs);
    return next_time;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "fs")) != -1) {
        switch (c) {
        case 'f': opt_f = true; break;
        case 's': opt_s = true; break;
        default:
            fprintf(stderr, "usage: atd [-fs]\n");
            return 2;
        }
    }
    if (optind < argc) {
        fprintf(stderr, "usage: atd [-fs]\n");
        return 2;
    }
    umask(077);
    if (opt_s) {
        /* until no job is due: batch jobs one after another */
        for (;;) {
            time_t next_time = scan();
            if (nruns == 0 && (next_time == 0 || next_time > time(NULL)))
                break;
            reap(true);
        }
        return 0;
    }
    if (!opt_f) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "atd: fork: %s\n", strerror(errno));
            return 1;
        }
        if (pid > 0)
            return 0;
        setsid();
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, 0);
            dup2(fd, 1);
            dup2(fd, 2);
            if (fd > 2)
                close(fd);
        }
    }
    for (;;) {
        reap(false);
        time_t next_time = scan();
        time_t now = time(NULL);
        unsigned wait = 60;
        if (next_time && next_time - now < 60)
            wait = next_time > now ? (unsigned)(next_time - now) : 1;
        if (nruns)
            wait = wait > 1 ? 1 : wait;
        sleep(wait);
    }
}
