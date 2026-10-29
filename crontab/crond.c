/*
 * crond.c - crond: run the commands of the crontab entries
 *
 * Synopsis: crond [-fs] [-T [[CC]YY]MMDDhhmm]
 *
 * Not a utility of POSIX: the daemon that the crontab utility needs.  At
 * every minute it reads the entries of the spool directory (cron.c) and
 * runs each command whose time matches, in a new session with the user's
 * IDs (when crond runs as root), in the user's home directory, with the
 * environment HOME, LOGNAME, USER, PATH (getconf PATH) and SHELL (the sh
 * of that PATH), by "sh -c command", the lines after the first '%' as its
 * standard input.  What the command writes to the standard output and
 * standard error is mailed to the user (mailx -s SUBJECT USER) when it is
 * not empty.
 *
 * Options:
 *   -f  stay in the foreground (diagnostics to the standard error)
 *   -s  run the commands of one minute (this one, or that of -T), wait for
 *       them, and exit
 *   -T  with -s: the minute, in the format of touch -t (for tests)
 *
 * When it does not run as root, crond runs only its own user's entry.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "cron.h"

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
static char *std_path, *sh_path;

static void say(const char *fmt, const char *a, const char *b)
{
    if (!opt_f && !opt_s)
        return;
    fprintf(stderr, "crond: ");
    fprintf(stderr, fmt, a, b);
    fputc('\n', stderr);
}

struct run {
    pid_t pid;
    char user[256];
    char out[4096];
    char command[256];          /* the start of it, for the subject */
};
static struct run *runs;
static size_t nruns;

static void mail_output(const struct run *r)
{
    struct stat st;
    if (stat(r->out, &st) != 0 || st.st_size == 0)
        return;
    pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid == 0) {
        int fd = open(r->out, O_RDONLY);
        if (fd < 0)
            _exit(1);
        dup2(fd, 0);
        if (fd)
            close(fd);
        char subject[600];
        snprintf(subject, sizeof subject, "Cron <%s> %s", r->user, r->command);
        execlp("mailx", "mailx", "-s", subject, r->user, (char *)NULL);
        _exit(127);
    }
    int ws;
    while (waitpid(pid, &ws, 0) < 0 && errno == EINTR)
        ;
    if (!WIFEXITED(ws) || WEXITSTATUS(ws) != 0)
        say("%s: mailx failed%s", r->user, "");
}

static void reap(bool block)
{
    while (nruns > 0) {
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
                runs[i] = runs[--nruns];
                break;
            }
    }
}

static void run(const struct passwd *pw, const char *field)
{
    char *cmd, *input;
    cron_command(field, &cmd, &input);
    if (!cmd)
        return;
    static unsigned long serial;
    struct run r;
    memset(&r, 0, sizeof r);
    snprintf(r.user, sizeof r.user, "%s", pw->pw_name);
    snprintf(r.command, sizeof r.command, "%.200s", cmd);
    snprintf(r.out, sizeof r.out, "%s/.out.%ld.%lu", cron_spool(), (long)getpid(), ++serial);
    int out = open(r.out, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int p[2];
    if (out < 0 || pipe(p) != 0) {
        say("%s: %s", r.out, strerror(errno));
        if (out >= 0)
            close(out);
        free(cmd);
        free(input);
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        say("fork: %s%s", strerror(errno), "");
        close(out);
        close(p[0]);
        close(p[1]);
        free(cmd);
        free(input);
        return;
    }
    if (pid == 0) {
        setsid();
        close(p[1]);
        dup2(p[0], 0);
        dup2(out, 1);
        dup2(out, 2);
        if (p[0] > 2)
            close(p[0]);
        if (out > 2)
            close(out);
        if (geteuid() == 0) {
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
                fprintf(stderr, "crond: cannot take the user's IDs: %s\n", strerror(errno));
                _exit(1);
            }
        }
        extern char **environ;
        static char *empty[] = { NULL };
        environ = empty;
        setenv("PATH", std_path, 1);
        setenv("SHELL", sh_path, 1);
        setenv("HOME", pw->pw_dir, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("USER", pw->pw_name, 1);
        if (chdir(pw->pw_dir) != 0 && chdir("/") != 0)
            _exit(1);
        signal(SIGPIPE, SIG_DFL);
        execl(sh_path, "sh", "-c", cmd, (char *)NULL);
        fprintf(stderr, "crond: %s: %s\n", sh_path, strerror(errno));
        _exit(127);
    }
    close(p[0]);
    close(out);
    /* the input: written by a child of its own, so a command that does not
       read it does not hold crond up */
    pid_t w = fork();
    if (w == 0) {
        signal(SIGPIPE, SIG_IGN);
        size_t len = strlen(input), off = 0;
        while (off < len) {
            ssize_t k = write(p[1], input + off, len - off);
            if (k <= 0)
                break;
            off += (size_t)k;
        }
        _exit(0);
    }
    close(p[1]);
    if (w > 0)
        while (waitpid(w, NULL, 0) < 0 && errno == EINTR)
            ;
    free(cmd);
    free(input);
    r.pid = pid;
    struct run *nr = realloc(runs, (nruns + 1) * sizeof *runs);
    if (!nr)
        return;
    runs = nr;
    runs[nruns++] = r;
}

/* run the entries that match minute t */
static void minute(time_t t)
{
    struct tm tm = *localtime(&t);
    const char *spool = cron_spool();
    DIR *d = opendir(spool);
    if (!d) {
        say("%s: %s", spool, strerror(errno));
        return;
    }
    uid_t me = geteuid();
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        struct passwd *pw = getpwnam(e->d_name);
        if (!pw || (me != 0 && pw->pw_uid != me))
            continue;
        struct passwd user = *pw;   /* getpwnam()'s storage is reused */
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", spool, e->d_name);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        char *line = NULL;
        size_t cap = 0;
        while (getline(&line, &cap, f) >= 0) {
            struct entry en;
            char err[256];
            if (cron_parse(line, &en, err, sizeof err) == 1 && cron_match(&en, &tm))
                run(&user, en.command);
            free(en.command);
        }
        free(line);
        fclose(f);
    }
    closedir(d);
}

/* -T: [[CC]YY]MMDDhhmm */
static bool parse_T(const char *s, time_t *out)
{
    size_t len = strlen(s);
    if ((len != 8 && len != 10 && len != 12) || strspn(s, "0123456789") != len)
        return false;
    int v[6], n = (int)len / 2;
    for (int i = 0; i < n; i++)
        v[i] = (s[2 * i] - '0') * 10 + (s[2 * i + 1] - '0');
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    int k = 0;
    if (len == 12) {
        tm.tm_year = v[0] * 100 + v[1] - 1900;
        k = 2;
    } else if (len == 10) {
        tm.tm_year = (v[0] >= 69 ? 1900 + v[0] : 2000 + v[0]) - 1900;
        k = 1;
    }
    tm.tm_mon = v[k] - 1;
    tm.tm_mday = v[k + 1];
    tm.tm_hour = v[k + 2];
    tm.tm_min = v[k + 3];
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    *out = mktime(&tm);
    return *out != (time_t)-1;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    const char *targ = NULL;
    int c;
    while ((c = getopt(argc, argv, "fsT:")) != -1) {
        switch (c) {
        case 'f': opt_f = true; break;
        case 's': opt_s = true; break;
        case 'T': targ = optarg; break;
        default:
            fprintf(stderr, "usage: crond [-fs] [-T [[CC]YY]MMDDhhmm]\n");
            return 2;
        }
    }
    if (optind < argc || (targ && !opt_s)) {
        fprintf(stderr, "usage: crond [-fs] [-T [[CC]YY]MMDDhhmm]\n");
        return 2;
    }
    size_t len = confstr(_CS_PATH, NULL, 0);
    std_path = len ? malloc(len) : NULL;
    if (std_path)
        confstr(_CS_PATH, std_path, len);
    else
        std_path = "/usr/bin:/bin";
    /* the sh of the standard PATH */
    sh_path = "/bin/sh";
    char *copy = strdup(std_path);
    for (char *dir = copy ? strtok(copy, ":") : NULL; dir; dir = strtok(NULL, ":")) {
        char p[4096];
        snprintf(p, sizeof p, "%s/sh", dir);
        if (access(p, X_OK) == 0) {
            sh_path = strdup(p);
            break;
        }
    }
    umask(077);
    if (opt_s) {
        time_t t = time(NULL);
        if (targ && !parse_T(targ, &t)) {
            fprintf(stderr, "crond: -T %s: not a time\n", targ);
            return 2;
        }
        minute(t);
        reap(true);
        return 0;
    }
    if (!opt_f) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "crond: fork: %s\n", strerror(errno));
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
    time_t last = time(NULL) / 60 * 60;
    for (;;) {
        reap(false);
        time_t now = time(NULL);
        time_t m = now / 60 * 60;
        /* each minute since the last one, at most an hour of them */
        if (m > last) {
            time_t from = m - last > 3600 ? m - 3600 + 60 : last + 60;
            for (time_t t = from; t <= m; t += 60)
                minute(t);
            last = m;
        }
        time_t next = (time(NULL) / 60 + 1) * 60;
        unsigned wait = (unsigned)(next - time(NULL));
        if (nruns && wait > 1)
            wait = 1;
        sleep(wait ? wait : 1);
    }
}
