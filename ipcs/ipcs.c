/*
 * ipcs.c - ipcs: report XSI interprocess communication facilities status
 * (POSIX.1-2024, XCU ipcs) [XSI]
 *
 * Synopsis: ipcs [-qms] [-a|-bcopt]
 *
 * The source of the information is /proc/sysvipc (msg, shm and sem: one
 * line an entry, the columns named by the first line), which lists every
 * entry whatever its permissions; QBYTES, which is not there, comes from
 * msgctl(IPC_STAT).  The introductory line names /proc/sysvipc as the
 * source and has the date as date writes it in the POSIX locale.
 *
 * Choices where the standard leaves room:
 *  - The columns: T, ID (%10d), KEY (0x%08x), MODE (11 characters and a
 *    space), OWNER and GROUP (%8s, longer names whole), then the columns
 *    of the options in the order of the standard, numbers right-aligned in
 *    their headings' width (at least 6), times in 9 columns (" no-entry").
 *    A heading line is written before a report when its columns differ
 *    from those of the report before it.
 *  - In MODE the first two characters are '-' (Linux does not tell whether
 *    a process waits to send or to receive, and does not clear segments on
 *    the first attach); there is no other access control: a space follows.
 *  - QBYTES of a queue that msgctl(IPC_STAT) may not read (no read
 *    permission) is the system's default, /proc/sys/kernel/msgmnb.
 *  - A facility whose /proc/sysvipc file is missing is "not in system";
 *    one whose file is there but empty has its heading and no entries.
 *  - Unknown options are usage errors (status 2).
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <grp.h>
#include <locale.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>
#include <unistd.h>

static bool ob, oc, oo, op, ot;
static int status;
static char last_heading[1024];

/* the columns of one line of a /proc/sysvipc file, by name */
struct row {
    char *names[32], *values[32];
    int n;
};

static const char *get(const struct row *r, const char *name)
{
    for (int i = 0; i < r->n; i++)
        if (strcmp(r->names[i], name) == 0)
            return r->values[i];
    return "0";
}

static long long num(const struct row *r, const char *name)
{
    return strtoll(get(r, name), NULL, 10);
}

static int split(char *s, char **out, int max)
{
    int n = 0;
    for (char *t = strtok(s, " \t\n"); t && n < max; t = strtok(NULL, " \t\n"))
        out[n++] = t;
    return n;
}

static void user(char *buf, size_t size, long long id)
{
    struct passwd *pw = getpwuid((uid_t)id);
    if (pw)
        snprintf(buf, size, "%8s", pw->pw_name);
    else
        snprintf(buf, size, "%8lld", id);
}

static void group(char *buf, size_t size, long long id)
{
    struct group *gr = getgrgid((gid_t)id);
    if (gr)
        snprintf(buf, size, "%8s", gr->gr_name);
    else
        snprintf(buf, size, "%8lld", id);
}

static void when(char *buf, size_t size, long long t)
{
    if (t == 0) {
        snprintf(buf, size, "%9s", " no-entry");
        return;
    }
    time_t tt = (time_t)t;
    struct tm *tm = localtime(&tt);
    if (!tm) {
        snprintf(buf, size, "%9s", "?");
        return;
    }
    char hms[32];
    snprintf(hms, sizeof hms, "%d:%2.2d:%2.2d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    snprintf(buf, size, "%9s", hms);
}

/* the mode: two flags, three sets of permissions, and a space */
static void mode(char *buf, long long perms, char write_char)
{
    buf[0] = '-';
    buf[1] = '-';
    for (int i = 0; i < 3; i++) {
        int bits = (int)(perms >> (6 - 3 * i)) & 7;
        buf[2 + 3 * i] = bits & 4 ? 'r' : '-';
        buf[3 + 3 * i] = bits & 2 ? write_char : '-';
        buf[4 + 3 * i] = '-';
    }
    buf[11] = ' ';
    buf[12] = '\0';
}

/* a growing line */
struct line {
    char s[1024];
    size_t n;
};

static void col(struct line *l, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(l->s + l->n, sizeof l->s - l->n, fmt, ap);
    va_end(ap);
    if (k > 0)
        l->n += (size_t)k < sizeof l->s - l->n ? (size_t)k : sizeof l->s - l->n - 1;
}

static long default_qbytes(void)
{
    FILE *f = fopen("/proc/sys/kernel/msgmnb", "r");
    long v = 0;
    if (f) {
        if (fscanf(f, "%ld", &v) != 1)
            v = 0;
        fclose(f);
    }
    return v;
}

enum fac { MSG, SHM, SEM };

static void report(enum fac f)
{
    static const char *const files[] = { "/proc/sysvipc/msg", "/proc/sysvipc/shm", "/proc/sysvipc/sem" };
    static const char *const names[] = { "Message Queue", "Shared Memory", "Semaphore" };
    static const char *const titles[] = { "Message Queues", "Shared Memory", "Semaphores" };
    FILE *fp = fopen(files[f], "r");
    if (!fp) {
        printf("%s facility not in system.\n", names[f]);
        return;
    }
    /* the heading */
    struct line h = { "", 0 };
    col(&h, "T %10s %10s %-12s%8s %8s", "ID", "KEY", "MODE", "OWNER", "GROUP");
    if (oc)
        col(&h, " %8s %8s", "CREATOR", "CGROUP");
    if (f == MSG) {
        if (oo)
            col(&h, " %6s %6s", "CBYTES", "QNUM");
        if (ob)
            col(&h, " %6s", "QBYTES");
        if (op)
            col(&h, " %6s %6s", "LSPID", "LRPID");
        if (ot)
            col(&h, " %9s %9s", "STIME", "RTIME");
    } else if (f == SHM) {
        if (oo)
            col(&h, " %6s", "NATTCH");
        if (ob)
            col(&h, " %10s", "SEGSZ");
        if (op)
            col(&h, " %6s %6s", "CPID", "LPID");
        if (ot)
            col(&h, " %9s %9s", "ATIME", "DTIME");
    } else {
        if (ob)
            col(&h, " %6s", "NSEMS");
        if (ot)
            col(&h, " %9s", "OTIME");
    }
    if (ot)
        col(&h, " %9s", "CTIME");
    if (strcmp(h.s, last_heading) != 0) {
        puts(h.s);
        strcpy(last_heading, h.s);
    }
    printf("%s:\n", titles[f]);

    char *buf = NULL, *head = NULL;
    size_t cap = 0, hcap = 0;
    if (getline(&head, &hcap, fp) < 0) {
        fclose(fp);
        free(head);
        return;
    }
    struct row r;
    char *hcopy = strdup(head);
    if (!hcopy) {
        fprintf(stderr, "ipcs: out of memory\n");
        exit(1);
    }
    int nh = split(hcopy, r.names, 32);
    long qdefault = -1;
    while (getline(&buf, &cap, fp) >= 0) {
        r.n = split(buf, r.values, 32);
        if (r.n > nh)
            r.n = nh;
        const char *idname = f == MSG ? "msqid" : f == SHM ? "shmid" : "semid";
        long long id = num(&r, idname);
        struct line l = { "", 0 };
        char m[16], a[64], b[64];
        mode(m, strtoll(get(&r, "perms"), NULL, 8), f == SEM ? 'a' : 'w');
        user(a, sizeof a, num(&r, "uid"));
        group(b, sizeof b, num(&r, "gid"));
        col(&l, "%c %10lld 0x%08x %s%s %s", "qms"[f], id, (unsigned)num(&r, "key"), m, a, b);
        if (oc) {
            user(a, sizeof a, num(&r, "cuid"));
            group(b, sizeof b, num(&r, "cgid"));
            col(&l, " %s %s", a, b);
        }
        if (f == MSG) {
            if (oo)
                col(&l, " %6lld %6lld", num(&r, "cbytes"), num(&r, "qnum"));
            if (ob) {
                struct msqid_ds d;
                long qb;
                if (msgctl((int)id, IPC_STAT, &d) == 0)
                    qb = (long)d.msg_qbytes;
                else {
                    if (qdefault < 0)
                        qdefault = default_qbytes();
                    qb = qdefault;
                }
                col(&l, " %6ld", qb);
            }
            if (op)
                col(&l, " %6lld %6lld", num(&r, "lspid"), num(&r, "lrpid"));
            if (ot) {
                when(a, sizeof a, num(&r, "stime"));
                when(b, sizeof b, num(&r, "rtime"));
                col(&l, " %s %s", a, b);
            }
        } else if (f == SHM) {
            if (oo)
                col(&l, " %6lld", num(&r, "nattch"));
            if (ob)
                col(&l, " %10lld", num(&r, "size"));
            if (op)
                col(&l, " %6lld %6lld", num(&r, "cpid"), num(&r, "lpid"));
            if (ot) {
                when(a, sizeof a, num(&r, "atime"));
                when(b, sizeof b, num(&r, "dtime"));
                col(&l, " %s %s", a, b);
            }
        } else {
            if (ob)
                col(&l, " %6lld", num(&r, "nsems"));
            if (ot) {
                when(a, sizeof a, num(&r, "otime"));
                col(&l, " %s", a);
            }
        }
        if (ot) {
            when(a, sizeof a, num(&r, "ctime"));
            col(&l, " %s", a);
        }
        puts(l.s);
    }
    if (ferror(fp)) {
        fprintf(stderr, "ipcs: %s: %s\n", files[f], strerror(errno));
        status = 1;
    }
    free(buf);
    free(head);
    free(hcopy);
    fclose(fp);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool oq = false, om = false, os = false;
    int c;
    while ((c = getopt(argc, argv, "qmsabcopt")) != -1) {
        switch (c) {
        case 'q': oq = true; break;
        case 'm': om = true; break;
        case 's': os = true; break;
        case 'a': ob = oc = oo = op = ot = true; break;
        case 'b': ob = true; break;
        case 'c': oc = true; break;
        case 'o': oo = true; break;
        case 'p': op = true; break;
        case 't': ot = true; break;
        default:
            fprintf(stderr, "usage: ipcs [-qms] [-a|-bcopt]\n");
            return 2;
        }
    }
    if (optind < argc) {
        fprintf(stderr, "usage: ipcs [-qms] [-a|-bcopt]\n");
        return 2;
    }
    if (!oq && !om && !os)
        oq = om = os = true;
    /* the date as date writes it in the POSIX locale */
    char date[128];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    static const char days[][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char mons[][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    char zone[64] = "";
    if (tm)
        strftime(zone, sizeof zone, "%Z", tm);
    if (tm)
        snprintf(date, sizeof date, "%s %s %2d %02d:%02d:%02d %s %d", days[tm->tm_wday],
                 mons[tm->tm_mon], tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, zone,
                 tm->tm_year + 1900);
    else
        snprintf(date, sizeof date, "?");
    printf("IPC status from /proc/sysvipc as of %s\n", date);
    if (oq)
        report(MSG);
    if (om)
        report(SHM);
    if (os)
        report(SEM);
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "ipcs: write error: %s\n", strerror(errno));
        return 1;
    }
    return status;
}
