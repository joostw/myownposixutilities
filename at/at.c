/*
 * at.c - at, batch: execute commands at a later time (POSIX.1-2024, XCU at
 * and batch)
 *
 * Synopsis:
 *   at [-m] [-f file] [-q queuename] -t time_arg
 *   at [-m] [-f file] [-q queuename] timespec...
 *   at -r at_job_id...
 *   at -l -q queuename
 *   at -l [at_job_id...]
 *   batch
 *
 * One program: invoked as batch it is "at -q b -m now".  A job is a file
 * of the spool directory (spool.c), written whole under a temporary name
 * and then renamed: a script for the shell that sets the environment
 * variables, the working directory and the file creation mask that at
 * found, followed by the commands.  atd runs the jobs.  Installed
 * set-user-ID root, at does all but the spool's work as the real user.
 *
 * Choices where the standard leaves room:
 *  - A job ID is "<number>.<queue>", the number from the counter file
 *    .seq of the spool (locked with fcntl()).  Queues are the letters a to
 *    z; a is the default, b is batch's; atd runs the others as a.
 *  - The time zones of a timespec: utc and gmt (and z), any case.  A date
 *    of the past is not an error: the job runs at once.  A day of the week
 *    is the next such day, today if the time is still to come.
 *  - The environment of the job: all the variables at found whose names
 *    are names of the shell, but for TERM, TERMCAP, DISPLAY, COLUMNS, LINES,
 *    SHLVL, PWD, OLDPWD and _.  SHELL, if set and not empty, is the job's
 *    command interpreter, else sh.
 *  - at -l lists the invoking user's jobs (all of them for root), by time,
 *    in the format of the standard; with job IDs, those jobs.  at -r removes
 *    the invoking user's jobs (any of them for root).
 *  - at.allow and at.deny (spool.c) apply to every use of at and batch.
 *  - Errors: status 1; usage errors status 2; nothing is submitted then.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "spool.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *prog = "at";
static uid_t ruid;
static bool setuid_root;        /* installed set-user-ID root */

/* the spool's work is done with the effective user ID the program has */
static void privileged(bool on)
{
    if (setuid_root && seteuid(on ? 0 : ruid) != 0) {
        fprintf(stderr, "%s: %s\n", prog, strerror(errno));
        exit(1);
    }
}

static void usage(void)
{
    if (strcmp(prog, "batch") == 0)
        fprintf(stderr, "usage: batch\n");
    else
        fprintf(stderr, "usage: at [-m] [-f file] [-q queuename] -t time_arg\n"
                        "       at [-m] [-f file] [-q queuename] timespec...\n"
                        "       at -r at_job_id...\n"
                        "       at -l -q queuename\n"
                        "       at -l [at_job_id...]\n");
    exit(2);
}

static void die(const char *fmt, const char *arg)
{
    fprintf(stderr, "%s: ", prog);
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    exit(1);
}

/* ---- the time ---- */

/* -t: [[CC]YY]MMDDhhmm[.SS], as touch -t */
static bool parse_t(const char *s, time_t *out)
{
    size_t len = strlen(s), digits = strspn(s, "0123456789");
    const char *dot = strchr(s, '.');
    int sec = 0;
    if (dot) {
        if ((size_t)(dot - s) != digits || strlen(dot + 1) != 2 || strspn(dot + 1, "0123456789") != 2)
            return false;
        sec = atoi(dot + 1);
        len = digits;
    } else if (digits != len)
        return false;
    if (len != 8 && len != 10 && len != 12)
        return false;
    int v[6], n = (int)len / 2;
    for (int i = 0; i < n; i++)
        v[i] = (s[2 * i] - '0') * 10 + (s[2 * i + 1] - '0');
    time_t now = time(NULL);
    struct tm *cur = localtime(&now);
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    int k = 0;
    if (len == 12) {
        tm.tm_year = v[0] * 100 + v[1] - 1900;
        k = 2;
    } else if (len == 10) {
        tm.tm_year = (v[0] >= 69 ? 1900 + v[0] : 2000 + v[0]) - 1900;
        k = 1;
    } else
        tm.tm_year = cur->tm_year;
    tm.tm_mon = v[k] - 1;
    tm.tm_mday = v[k + 1];
    tm.tm_hour = v[k + 2];
    tm.tm_min = v[k + 3];
    tm.tm_sec = sec == 60 ? 59 : sec;
    tm.tm_isdst = -1;
    if (tm.tm_mon < 0 || tm.tm_mon > 11 || tm.tm_mday < 1 || tm.tm_mday > 31 || tm.tm_hour > 23 ||
        tm.tm_min > 59 || sec > 60)
        return false;
    struct tm want = tm;
    time_t t = mktime(&tm);
    if (t == (time_t)-1 || tm.tm_mday != want.tm_mday || tm.tm_mon != want.tm_mon)
        return false;
    *out = t;
    return true;
}

/* the tokens of a timespec */
enum tok { T_END, T_NUM, T_WORD, T_COLON, T_COMMA, T_PLUS, T_BAD };
struct lexer {
    const char *s;
    enum tok tok;
    char text[64];
    size_t len;                 /* digits of a number */
};

static void next(struct lexer *l)
{
    while (*l->s == ' ' || *l->s == '\t')
        l->s++;
    l->text[0] = '\0';
    char c = *l->s;
    if (!c) {
        l->tok = T_END;
        return;
    }
    size_t n = 0;
    if (isdigit((unsigned char)c)) {
        while (isdigit((unsigned char)l->s[n]) && n < sizeof l->text - 1)
            n++;
        l->tok = T_NUM;
    } else if (isalpha((unsigned char)c)) {
        while (isalpha((unsigned char)l->s[n]) && n < sizeof l->text - 1)
            n++;
        l->tok = T_WORD;
    } else {
        n = 1;
        l->tok = c == ':' ? T_COLON : c == ',' ? T_COMMA : c == '+' ? T_PLUS : T_BAD;
    }
    memcpy(l->text, l->s, n);
    l->text[n] = '\0';
    for (size_t i = 0; i < n; i++)
        l->text[i] = (char)tolower((unsigned char)l->text[i]);
    l->len = n;
    l->s += n;
}

static int word_index(const char *w, const char *const list[], int n, bool abbrev)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(w, list[i]) == 0)
            return i;
        if (abbrev && strlen(w) == 3 && strncmp(w, list[i], 3) == 0)
            return i;
    }
    return -1;
}

static const char *const months[] = { "january", "february", "march", "april", "may", "june",
                                      "july", "august", "september", "october", "november", "december" };
static const char *const wdays[] = { "sunday", "monday", "tuesday", "wednesday", "thursday",
                                     "friday", "saturday" };

/* an increment: "+" n period or "next" period; adds to *tm */
static bool increment(struct lexer *l, struct tm *tm)
{
    long n;
    if (l->tok == T_PLUS) {
        next(l);
        if (l->tok != T_NUM || l->len > 6)
            return false;
        n = atol(l->text);
        next(l);
    } else if (l->tok == T_WORD && strcmp(l->text, "next") == 0) {
        n = 1;
        next(l);
    } else
        return false;
    if (l->tok != T_WORD)
        return false;
    const char *p = l->text;
    size_t pl = strlen(p);
    if (pl > 1 && p[pl - 1] == 's')
        pl--;
    if (strncmp(p, "minute", pl) == 0 && pl == 6)
        tm->tm_min += (int)n;
    else if (strncmp(p, "hour", pl) == 0 && pl == 4)
        tm->tm_hour += (int)n;
    else if (strncmp(p, "day", pl) == 0 && pl == 3)
        tm->tm_mday += (int)n;
    else if (strncmp(p, "week", pl) == 0 && pl == 4)
        tm->tm_mday += 7 * (int)n;
    else if (strncmp(p, "month", pl) == 0 && pl == 5)
        tm->tm_mon += (int)n;
    else if (strncmp(p, "year", pl) == 0 && pl == 4)
        tm->tm_year += (int)n;
    else
        return false;
    next(l);
    return true;
}

/* mktime() in UTC: with TZ set to UTC0 for the call */
static time_t mktime_tz(struct tm *tm, bool utc)
{
    if (!utc)
        return mktime(tm);
    const char *old = getenv("TZ");
    char *save = old ? strdup(old) : NULL;
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t t = mktime(tm);
    if (save) {
        setenv("TZ", save, 1);
        free(save);
    } else
        unsetenv("TZ");
    tzset();
    return t;
}

static bool parse_timespec(const char *spec, time_t *out)
{
    struct lexer l = { spec, T_END, "", 0 };
    next(&l);
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    bool utc = false;
    if (l.tok == T_WORD && strcmp(l.text, "now") == 0) {
        next(&l);
        if (l.tok != T_END && !increment(&l, &tm))
            return false;
        if (l.tok != T_END)
            return false;
        tm.tm_isdst = -1;
        *out = mktime(&tm);
        return *out != (time_t)-1;
    }
    /* the time */
    int hour, min = 0;
    if (l.tok == T_WORD && (strcmp(l.text, "noon") == 0 || strcmp(l.text, "midnight") == 0)) {
        hour = l.text[0] == 'n' ? 12 : 0;
        next(&l);
    } else if (l.tok == T_NUM) {
        bool hr_min = l.len == 4;
        if (l.len != 1 && l.len != 2 && l.len != 4)
            return false;
        int v = atoi(l.text);
        hour = hr_min ? v / 100 : v;
        min = hr_min ? v % 100 : 0;
        next(&l);
        if (!hr_min && l.tok == T_COLON) {
            next(&l);
            if (l.tok != T_NUM || l.len > 2)
                return false;
            min = atoi(l.text);
            next(&l);
        }
        if (l.tok == T_WORD && (strcmp(l.text, "am") == 0 || strcmp(l.text, "pm") == 0)) {
            if (hour < 1 || hour > 12)
                return false;
            hour = hour % 12 + (l.text[0] == 'p' ? 12 : 0);
            next(&l);
        }
        if (hour > 23 || min > 59)
            return false;
    } else
        return false;
    if (l.tok == T_WORD && (strcmp(l.text, "utc") == 0 || strcmp(l.text, "gmt") == 0 ||
                            strcmp(l.text, "z") == 0)) {
        utc = true;
        next(&l);
    }
    struct tm base = tm;
    if (utc) {
        struct tm *g = gmtime(&now);
        base = *g;
    }
    tm = base;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = 0;
    bool later = hour > base.tm_hour || (hour == base.tm_hour && min >= base.tm_min);
    /* the date */
    int m;
    if (l.tok == T_WORD && (m = word_index(l.text, months, 12, true)) >= 0) {
        next(&l);
        if (l.tok != T_NUM || l.len > 2)
            return false;
        int day = atoi(l.text);
        next(&l);
        tm.tm_mon = m;
        tm.tm_mday = day;
        if (l.tok == T_COMMA) {
            next(&l);
            if (l.tok != T_NUM || l.len != 4)
                return false;
            tm.tm_year = atoi(l.text) - 1900;
            next(&l);
        } else if (m < base.tm_mon)    /* a month that is past: next year */
            tm.tm_year++;
        int days[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (day < 1 || day > days[m])
            return false;
    } else if (l.tok == T_WORD && (m = word_index(l.text, wdays, 7, true)) >= 0) {
        next(&l);
        int ahead = (m - base.tm_wday + 7) % 7;
        if (ahead == 0 && !later)
            ahead = 7;
        tm.tm_mday += ahead;
    } else if (l.tok == T_WORD && strcmp(l.text, "today") == 0) {
        next(&l);
    } else if (l.tok == T_WORD && strcmp(l.text, "tomorrow") == 0) {
        tm.tm_mday++;
        next(&l);
    } else if (!later)
        tm.tm_mday++;           /* no date: tomorrow if the time is past */
    if (l.tok != T_END && !increment(&l, &tm))
        return false;
    if (l.tok != T_END)
        return false;
    tm.tm_isdst = -1;
    *out = mktime_tz(&tm, utc);
    return *out != (time_t)-1;
}

/* ---- submitting ---- */

static void put_quoted(FILE *f, const char *s)
{
    putc('\'', f);
    for (; *s; s++)
        if (*s == '\'')
            fputs("'\\''", f);
        else
            putc(*s, f);
    putc('\'', f);
}

static bool skip_var(const char *name, size_t n)
{
    static const char *const skip[] = { "TERM", "TERMCAP", "DISPLAY", "COLUMNS", "LINES", "SHLVL",
                                        "PWD", "OLDPWD", "_" };
    for (size_t i = 0; i < sizeof skip / sizeof skip[0]; i++)
        if (strlen(skip[i]) == n && strncmp(skip[i], name, n) == 0)
            return true;
    if (!(isalpha((unsigned char)name[0]) || name[0] == '_'))
        return true;
    for (size_t i = 0; i < n; i++)
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_'))
            return true;
    return false;
}

/* the next job number, from the locked counter file */
static long next_number(const char *spool)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/.seq", spool);
    int fd = open(path, O_RDWR | O_CREAT, 0600);
    if (fd < 0)
        die("cannot open the job counter: %s", strerror(errno));
    struct flock lk;
    memset(&lk, 0, sizeof lk);
    lk.l_type = F_WRLCK;
    lk.l_whence = SEEK_SET;
    while (fcntl(fd, F_SETLKW, &lk) != 0)
        if (errno != EINTR)
            die("cannot lock the job counter: %s", strerror(errno));
    char buf[32] = "";
    ssize_t n = read(fd, buf, sizeof buf - 1);
    long v = n > 0 ? strtol(buf, NULL, 10) : 0;
    v = v >= 0 && v < LONG_MAX ? v + 1 : 1;
    int len = snprintf(buf, sizeof buf, "%ld\n", v);
    if (lseek(fd, 0, SEEK_SET) != 0 || ftruncate(fd, 0) != 0 || write(fd, buf, (size_t)len) != len)
        die("cannot write the job counter: %s", strerror(errno));
    close(fd);                  /* and the lock goes */
    return v;
}

static int submit(FILE *in, const char *inname, char queue, time_t when, bool mail)
{
    /* the commands, read as the user */
    char *cmds = NULL;
    size_t clen = 0, ccap = 0;
    int ch;
    while ((ch = getc(in)) != EOF) {
        if (clen + 2 > ccap) {
            ccap = ccap ? ccap * 2 : 4096;
            char *nc = realloc(cmds, ccap);
            if (!nc)
                die("%s", "out of memory");
            cmds = nc;
        }
        cmds[clen++] = (char)ch;
    }
    if (ferror(in))
        die("%s", inname);
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd))
        die("cannot find the working directory: %s", strerror(errno));
    mode_t mask = umask(0);
    umask(mask);
    const char *shell = getenv("SHELL");
    if (!shell || !*shell)
        shell = "sh";

    const char *spool = spool_dir();
    privileged(true);
    long num = next_number(spool);
    char id[64], tmp[4096], path[4096];
    snprintf(id, sizeof id, "%ld.%c", num, queue);
    snprintf(path, sizeof path, "%s/%s", spool, id);
    snprintf(tmp, sizeof tmp, "%s/.new.%s", spool, id);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        die("cannot create the job: %s", strerror(errno));
    FILE *f = fdopen(fd, "w");
    if (!f)
        die("cannot create the job: %s", strerror(errno));
    fprintf(f, "#!/bin/sh\n# atjob 1\n# owner %lu\n# time %lld\n# queue %c\n# mail %d\n# shell %s\n# end\n",
            (unsigned long)ruid, (long long)when, queue, mail ? 1 : 0, shell);
    extern char **environ;
    for (char **e = environ; *e; e++) {
        const char *eq = strchr(*e, '=');
        if (!eq || skip_var(*e, (size_t)(eq - *e)))
            continue;
        fprintf(f, "%.*s=", (int)(eq - *e), *e);
        put_quoted(f, eq + 1);
        fprintf(f, "; export %.*s\n", (int)(eq - *e), *e);
    }
    fprintf(f, "umask %04o\ncd ", (unsigned)mask);
    put_quoted(f, cwd);
    fprintf(f, " || {\n\techo 'at: cannot change to the working directory' >&2\n\texit 1\n}\n");
    if (clen && fwrite(cmds, 1, clen, f) != clen)
        die("cannot write the job: %s", strerror(errno));
    if (clen && cmds[clen - 1] != '\n')
        putc('\n', f);
    if (fflush(f) != 0 || ferror(f) || fsync(fd) != 0 || fclose(f) != 0) {
        unlink(tmp);
        die("cannot write the job: %s", strerror(errno));
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        die("cannot create the job: %s", strerror(errno));
    }
    privileged(false);
    free(cmds);
    char date[64];
    job_date(date, sizeof date, when);
    fprintf(stderr, "job %s at %s\n", id, date);
    return 0;
}

/* ---- listing and removing ---- */

static int cmp_jobs(const void *a, const void *b)
{
    const struct job *x = a, *y = b;
    if (x->when != y->when)
        return x->when < y->when ? -1 : 1;
    long nx = atol(x->id), ny = atol(y->id);
    return nx < ny ? -1 : nx > ny;
}

static int list(char **ids, int nids, char queue)
{
    const char *spool = spool_dir();
    int status = 0;
    struct job *jobs = NULL;
    size_t njobs = 0;
    privileged(true);
    if (nids > 0) {
        for (int i = 0; i < nids; i++) {
            char path[4096];
            struct job j;
            snprintf(path, sizeof path, "%s/%s", spool, ids[i]);
            if (!job_id_ok(ids[i]) || !job_read(path, &j) || (ruid != 0 && j.owner != ruid)) {
                fprintf(stderr, "%s: %s: no such job\n", prog, ids[i]);
                status = 1;
                continue;
            }
            char date[64];
            job_date(date, sizeof date, j.when);
            printf("%s\t%s\n", j.id, date);
        }
        privileged(false);
        return status;
    }
    DIR *d = opendir(spool);
    if (!d) {
        if (errno == ENOENT) {
            privileged(false);
            return 0;
        }
        die("cannot read the spool: %s", strerror(errno));
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        char path[4096];
        struct job j;
        snprintf(path, sizeof path, "%s/%s", spool, e->d_name);
        if (!job_read(path, &j) || (ruid != 0 && j.owner != ruid) || (queue && j.queue != queue))
            continue;
        struct job *nj = realloc(jobs, (njobs + 1) * sizeof *jobs);
        if (!nj)
            die("%s", "out of memory");
        jobs = nj;
        jobs[njobs++] = j;
    }
    closedir(d);
    privileged(false);
    qsort(jobs, njobs, sizeof *jobs, cmp_jobs);
    for (size_t i = 0; i < njobs; i++) {
        char date[64];
        job_date(date, sizeof date, jobs[i].when);
        printf("%s\t%s\n", jobs[i].id, date);
    }
    free(jobs);
    return status;
}

static int remove_jobs(char **ids, int nids)
{
    const char *spool = spool_dir();
    int status = 0;
    privileged(true);
    for (int i = 0; i < nids; i++) {
        char path[4096];
        struct job j;
        snprintf(path, sizeof path, "%s/%s", spool, ids[i]);
        if (!job_id_ok(ids[i]) || !job_read(path, &j)) {
            fprintf(stderr, "%s: %s: no such job\n", prog, ids[i]);
            status = 1;
        } else if (ruid != 0 && j.owner != ruid) {
            fprintf(stderr, "%s: %s: not your job\n", prog, ids[i]);
            status = 1;
        } else if (unlink(path) != 0) {
            fprintf(stderr, "%s: %s: %s\n", prog, ids[i], strerror(errno));
            status = 1;
        }
    }
    privileged(false);
    return status;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    const char *base = strrchr(argv[0], '/');
    base = base ? base + 1 : argv[0];
    bool batch = strcmp(base, "batch") == 0;
    prog = batch ? "batch" : "at";
    ruid = getuid();
    setuid_root = geteuid() == 0 && ruid != 0;
    privileged(false);

    bool om = false, ol = false, orr = false;
    const char *file = NULL, *targ = NULL;
    char queue = 0;
    int c;
    if (batch) {
        if (getopt(argc, argv, "") != -1 || optind < argc)
            usage();
    } else
        while ((c = getopt(argc, argv, "mf:q:t:lr")) != -1) {
            switch (c) {
            case 'm': om = true; break;
            case 'f': file = optarg; break;
            case 'q':
                if (!islower((unsigned char)optarg[0]) || optarg[1] || !isalpha((unsigned char)optarg[0])) {
                    fprintf(stderr, "at: -q %s: a queue is a letter from a to z\n", optarg);
                    return 2;
                }
                queue = optarg[0];
                break;
            case 't': targ = optarg; break;
            case 'l': ol = true; break;
            case 'r': orr = true; break;
            default: usage();
            }
        }
    struct passwd *pw = getpwuid(ruid);
    if (!pw)
        die("%s", "the invoking user has no entry in the user database");
    if (!user_allowed("at", pw->pw_name, ruid == 0)) {
        fprintf(stderr, "%s: you may not use %s (at.allow, at.deny)\n", prog, prog);
        return 1;
    }
    int nops = argc - optind;
    char **ops = argv + optind;
    if (orr) {
        if (ol || om || file || queue || targ || nops == 0)
            usage();
        return remove_jobs(ops, nops);
    }
    if (ol) {
        if (om || file || targ || (queue && nops > 0))
            usage();
        int r = list(ops, nops, queue);
        if (fflush(stdout) != 0 || ferror(stdout)) {
            fprintf(stderr, "at: write error: %s\n", strerror(errno));
            return 1;
        }
        return r;
    }
    time_t when;
    if (batch) {
        when = time(NULL);
        queue = 'b';
        om = true;
    } else if (targ) {
        if (nops > 0)
            usage();
        if (!parse_t(targ, &when)) {
            fprintf(stderr, "at: -t %s: not a time ([[CC]YY]MMDDhhmm[.SS])\n", targ);
            return 1;
        }
    } else {
        if (nops == 0)
            usage();
        size_t len = 1;
        for (int i = 0; i < nops; i++)
            len += strlen(ops[i]) + 1;
        char *spec = malloc(len);
        if (!spec)
            die("%s", "out of memory");
        spec[0] = '\0';
        for (int i = 0; i < nops; i++) {
            if (i)
                strcat(spec, " ");
            strcat(spec, ops[i]);
        }
        if (!parse_timespec(spec, &when)) {
            fprintf(stderr, "at: %s: not a time\n", spec);
            return 1;
        }
        free(spec);
    }
    if (!queue)
        queue = 'a';
    FILE *in = stdin;
    const char *inname = "standard input";
    if (file) {
        if (!(in = fopen(file, "r"))) {
            fprintf(stderr, "at: %s: %s\n", file, strerror(errno));
            return 1;
        }
        inname = file;
    }
    return submit(in, inname, queue, when, om);
}
