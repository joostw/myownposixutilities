/*
 * ps.c - ps: report process status (POSIX.1-2024, XCU ps)
 *
 * Synopsis:
 *   ps [-aAw] [-defl] [-g grouplist] [-G grouplist] [-n namelist]
 *      [-o format]... [-p proclist] [-t termlist] [-u userlist]
 *      [-U userlist]
 *
 * The information comes from /proc (Linux): /proc/PID/stat, status,
 * cmdline and wchan, /proc/stat for the boot time; terminal names from
 * the character devices of /dev, /dev/pts and /dev/console.
 *
 * Choices where the standard leaves room:
 *  - The default listing is PID TTY TIME CMD; -f adds UID (a name),
 *    PPID, C and STIME, and CMD is the arguments; -l adds F, S, UID (a
 *    number unless -f), PPID, C, PRI, NI, ADDR, SZ and WCHAN.  The columns
 *    are as wide as their widest value; numbers are right-aligned; the
 *    last column is not padded.
 *  - -a omits session leaders, as historical ps does; -g selects by
 *    session ID; -G and -U also take names; -n is accepted and ignored.
 *  - The default selection: the effective user ID of ps and its
 *    controlling terminal (processes without one, when ps has none).
 *  - The names -o takes are those of the standard and, as other
 *    implementations, those of the -f and -l columns: f, s, uid, c, pri,
 *    addr, sz, wchan, stime, cmd (as args).
 *  - comm is the process's command name as the system keeps it (at most
 *    15 bytes on Linux); args is the argument list (the command name in
 *    square brackets when there is none, as for kernel threads).  A
 *    process that is a zombie has " <defunct>" after its args, and after
 *    the command name in CMD of the default and -l listings.
 *  - tty is the terminal's name under /dev (pts/0, tty1, console), "?"
 *    for none; -t takes those names, "/dev/" names, and the part after
 *    "tty".
 *  - pcpu is the processor time over the time since the process started;
 *    C is the same, an integer; PRI is the kernel's priority plus 60 (80
 *    for a nice value of 0); F is 1 (forked, no exec) plus 4 (used
 *    privileges); ADDR is "-"; SZ is the virtual size in pages;
 *    WCHAN is /proc/PID/wchan, "-" when running or unknown; STIME is HH:MM
 *    within a day, MmmDD within the year, else the year.
 *  - A field that cannot be known is "-", as the nice value of a process
 *    with a real-time scheduling policy.  Control characters in comm and
 *    args are written as '?', and the format effectors (tab, newline, ...)
 *    as spaces, so that every process is one line.  Lines are cut at the larger of
 *    {LINE_MAX} and COLUMNS (132 at least with -w); with -w twice they are
 *    not cut.  Processes are listed by process ID.
 *  - Unknown names in -o, lists that are not valid, and unknown options
 *    are usage errors (status 1, nothing written).
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "ps: out of memory\n");
        exit(1);
    }
    return p;
}

static char *xstrdup(const char *s)
{
    char *p = xmalloc(strlen(s) + 1);
    return strcpy(p, s);
}

/* ---- processes ---- */

struct proc {
    long pid, ppid, pgrp, session, tty, tpgid;
    char state;
    unsigned long flags;
    unsigned long long utime, stime, start;    /* clock ticks */
    long priority, nice;
    bool realtime;              /* a real-time policy: no nice value */
    unsigned long long vsize;
    long ruid, euid, rgid, egid;
    char comm[64];
    char *args;                 /* NULL: none */
    char wchan[64];
};

static long hz;
static long long boot;          /* the boot time, seconds since the Epoch */
static time_t now;

static char *read_text(const char *path, size_t *len)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    size_t cap = 1024, n = 0;
    char *b = xmalloc(cap + 1);
    size_t k;
    while ((k = fread(b + n, 1, cap - n, f)) > 0) {
        n += k;
        if (n == cap) {
            char *nb = realloc(b, (cap *= 2) + 1);
            if (!nb) {
                free(b);
                fclose(f);
                return NULL;
            }
            b = nb;
        }
    }
    fclose(f);
    b[n] = '\0';
    if (len)
        *len = n;
    return b;
}

static bool read_proc(long pid, struct proc *p)
{
    char path[64];
    memset(p, 0, sizeof *p);
    p->pid = pid;
    snprintf(path, sizeof path, "/proc/%ld/stat", pid);
    char *s = read_text(path, NULL);
    if (!s)
        return false;
    char *open = strchr(s, '('), *close = strrchr(s, ')');
    if (!open || !close || close < open) {
        free(s);
        return false;
    }
    size_t cl = (size_t)(close - open - 1);
    if (cl >= sizeof p->comm)
        cl = sizeof p->comm - 1;
    memcpy(p->comm, open + 1, cl);
    p->comm[cl] = '\0';
    for (char *q = p->comm; *q; q++)
        if ((unsigned char)*q < 32 || *q == 127)
            *q = '?';
    /* the fields after the name: 3 state, 4 ppid, ... (man 5 proc) */
    char *f[64];
    int n = 0;
    for (char *t = strtok(close + 1, " \n"); t && n < 64; t = strtok(NULL, " \n"))
        f[n++] = t;
    if (n < 22) {
        free(s);
        return false;
    }
    p->state = f[0][0];
    p->ppid = atol(f[1]);
    p->pgrp = atol(f[2]);
    p->session = atol(f[3]);
    p->tty = atol(f[4]);
    p->tpgid = atol(f[5]);
    p->flags = strtoul(f[6], NULL, 10);
    p->utime = strtoull(f[11], NULL, 10);
    p->stime = strtoull(f[12], NULL, 10);
    p->priority = atol(f[15]);
    p->nice = atol(f[16]);
    p->start = strtoull(f[19], NULL, 10);
    p->vsize = strtoull(f[20], NULL, 10);
    /* the scheduling policy: a nice value only for the normal ones */
    p->realtime = n > 38 && atoi(f[38]) != 0 && atoi(f[38]) != 3 && atoi(f[38]) != 5;
    free(s);

    snprintf(path, sizeof path, "/proc/%ld/status", pid);
    p->ruid = p->euid = p->rgid = p->egid = -1;
    if ((s = read_text(path, NULL))) {
        char *u = strstr(s, "\nUid:"), *g = strstr(s, "\nGid:");
        if (u)
            sscanf(u + 5, "%ld %ld", &p->ruid, &p->euid);
        if (g)
            sscanf(g + 5, "%ld %ld", &p->rgid, &p->egid);
        free(s);
    }
    snprintf(path, sizeof path, "/proc/%ld/cmdline", pid);
    size_t len;
    if ((s = read_text(path, &len)) && len > 0) {
        while (len > 0 && s[len - 1] == '\0')
            len--;
        for (size_t i = 0; i < len; i++) {
            unsigned char ch = (unsigned char)s[i];
            if (ch == '\0' || (ch >= '\t' && ch <= '\r'))
                s[i] = ' ';
            else if (ch < 32 || ch == 127)
                s[i] = '?';
        }
        s[len] = '\0';
        p->args = s;
    } else
        free(s);
    snprintf(path, sizeof path, "/proc/%ld/wchan", pid);
    if ((s = read_text(path, NULL))) {
        snprintf(p->wchan, sizeof p->wchan, "%s", s);
        free(s);
    }
    if (!p->wchan[0] || strcmp(p->wchan, "0") == 0 || p->state == 'R')
        strcpy(p->wchan, "-");
    return true;
}

/* ---- terminals ---- */

struct ttyname {
    unsigned long dev;
    char *name;
};
static struct ttyname *ttys;
static size_t nttys;
static bool ttys_read;

static void add_ttys(const char *dir, const char *prefix)
{
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        if (*prefix && strncmp(e->d_name, prefix, strlen(prefix)) != 0)
            continue;
        char path[512];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISCHR(st.st_mode))
            continue;
        ttys = realloc(ttys, (nttys + 1) * sizeof *ttys);
        if (!ttys) {
            fprintf(stderr, "ps: out of memory\n");
            exit(1);
        }
        ttys[nttys].dev = (unsigned long)st.st_rdev;
        ttys[nttys].name = xstrdup(path + 5);   /* without "/dev/" */
        nttys++;
    }
    closedir(d);
}

static void read_ttys(void)
{
    if (ttys_read)
        return;
    ttys_read = true;
    add_ttys("/dev/pts", "");
    add_ttys("/dev", "tty");
    add_ttys("/dev", "console");
}

/* the name of the terminal of device number dev (the kernel's encoding) */
static const char *tty_name(long dev)
{
    if (dev == 0)
        return NULL;
    read_ttys();
    /* /proc gives major in bits 8-19 and 31-..., minor in 0-7 and 20-31;
       st_rdev of glibc encodes them its own way: compare both ways */
    unsigned long major = ((unsigned long)dev >> 8) & 0xfff;
    unsigned long minor = ((unsigned long)dev & 0xff) | (((unsigned long)dev >> 12) & 0xfff00);
    for (size_t i = 0; i < nttys; i++) {
        unsigned long d = ttys[i].dev;
        unsigned long ma = ((d >> 8) & 0xfff) | ((d >> 32) & 0xfffff000UL);
        unsigned long mi = (d & 0xff) | ((d >> 12) & 0xffffff00UL);
        if (ma == major && mi == minor)
            return ttys[i].name;
    }
    return NULL;
}

/* ---- the fields ---- */

enum fid {
    F_RUSER, F_USER, F_RGROUP, F_GROUP, F_PID, F_PPID, F_PGID, F_PCPU, F_VSZ, F_NICE, F_ETIME,
    F_TIME, F_TTY, F_COMM, F_ARGS,
    /* those of -f and -l */
    F_F, F_S, F_UID, F_UIDNUM, F_C, F_PRI, F_ADDR, F_SZ, F_WCHAN, F_STIME,
    F_CMDNAME                   /* CMD of the default listing: comm, and <defunct> */
};

static const struct {
    const char *name;
    const char *header;
    enum fid id;
    bool right;
} names[] = {
    { "ruser", "RUSER", F_RUSER, false }, { "user", "USER", F_USER, false },
    { "rgroup", "RGROUP", F_RGROUP, false }, { "group", "GROUP", F_GROUP, false },
    { "pid", "PID", F_PID, true }, { "ppid", "PPID", F_PPID, true }, { "pgid", "PGID", F_PGID, true },
    { "pcpu", "%CPU", F_PCPU, true }, { "vsz", "VSZ", F_VSZ, true }, { "nice", "NI", F_NICE, true },
    { "etime", "ELAPSED", F_ETIME, true }, { "time", "TIME", F_TIME, true },
    { "tty", "TT", F_TTY, false }, { "comm", "COMMAND", F_COMM, false },
    { "args", "COMMAND", F_ARGS, false },
    { "f", "F", F_F, true }, { "s", "S", F_S, false }, { "uid", "UID", F_UIDNUM, true },
    { "c", "C", F_C, true }, { "pri", "PRI", F_PRI, true }, { "addr", "ADDR", F_ADDR, false },
    { "sz", "SZ", F_SZ, true }, { "wchan", "WCHAN", F_WCHAN, false },
    { "stime", "STIME", F_STIME, false }, { "cmd", "CMD", F_ARGS, false },
};
#define NNAMES (sizeof names / sizeof names[0])

struct column {
    enum fid id;
    char *header;
    size_t width;
    bool right;
};
static struct column *cols;
static size_t ncols;

static void add_column(enum fid id, const char *header, bool right)
{
    cols = realloc(cols, (ncols + 1) * sizeof *cols);
    if (!cols) {
        fprintf(stderr, "ps: out of memory\n");
        exit(1);
    }
    cols[ncols].id = id;
    cols[ncols].header = xstrdup(header);
    cols[ncols].width = strlen(header);
    cols[ncols].right = right;
    ncols++;
}

/* -o: names separated by blanks or commas; name=header takes the rest */
static bool parse_format(const char *s)
{
    const char *p = s;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
        if (!*p)
            return true;
        size_t n = strcspn(p, " \t,=");
        size_t k;
        for (k = 0; k < NNAMES; k++)
            if (strlen(names[k].name) == n && strncmp(names[k].name, p, n) == 0)
                break;
        if (k == NNAMES) {
            fprintf(stderr, "ps: %.*s: not a format name\n", (int)n, p);
            return false;
        }
        p += n;
        if (*p == '=') {
            add_column(names[k].id, p + 1, names[k].right);
            if (!p[1])
                cols[ncols - 1].width = strlen(names[k].header);
            return true;
        }
        add_column(names[k].id, names[k].header, names[k].right);
    }
}

static void user_name(char *buf, size_t size, long id)
{
    struct passwd *pw = id >= 0 ? getpwuid((uid_t)id) : NULL;
    if (pw)
        snprintf(buf, size, "%s", pw->pw_name);
    else if (id >= 0)
        snprintf(buf, size, "%ld", id);
    else
        snprintf(buf, size, "-");
}

static void group_name(char *buf, size_t size, long id)
{
    struct group *gr = id >= 0 ? getgrgid((gid_t)id) : NULL;
    if (gr)
        snprintf(buf, size, "%s", gr->gr_name);
    else if (id >= 0)
        snprintf(buf, size, "%ld", id);
    else
        snprintf(buf, size, "-");
}

static void duration(char *buf, size_t size, unsigned long long secs, bool with_hours)
{
    unsigned long long d = secs / 86400, h = secs / 3600 % 24, m = secs / 60 % 60, s = secs % 60;
    if (d)
        snprintf(buf, size, "%llu-%02llu:%02llu:%02llu", d, h, m, s);
    else if (h || with_hours)
        snprintf(buf, size, "%02llu:%02llu:%02llu", h, m, s);
    else
        snprintf(buf, size, "%02llu:%02llu", m, s);
}

static double cpu_percent(const struct proc *p)
{
    double up = (double)(now - boot) - (double)p->start / (double)hz;
    if (up <= 0)
        return 0;
    double used = (double)(p->utime + p->stime) / (double)hz;
    return used * 100 / up;
}

/* the value of a field, into a malloc()ed string */
static char *value(const struct proc *p, enum fid id, bool full)
{
    char buf[512];
    switch (id) {
    case F_RUSER: user_name(buf, sizeof buf, p->ruid); break;
    case F_USER: user_name(buf, sizeof buf, p->euid); break;
    case F_UID:
        if (full)
            user_name(buf, sizeof buf, p->euid);
        else
            snprintf(buf, sizeof buf, "%ld", p->euid);
        break;
    case F_UIDNUM: snprintf(buf, sizeof buf, "%ld", p->euid); break;
    case F_RGROUP: group_name(buf, sizeof buf, p->rgid); break;
    case F_GROUP: group_name(buf, sizeof buf, p->egid); break;
    case F_PID: snprintf(buf, sizeof buf, "%ld", p->pid); break;
    case F_PPID: snprintf(buf, sizeof buf, "%ld", p->ppid); break;
    case F_PGID: snprintf(buf, sizeof buf, "%ld", p->pgrp); break;
    case F_PCPU: snprintf(buf, sizeof buf, "%.1f", cpu_percent(p)); break;
    case F_C: snprintf(buf, sizeof buf, "%d", (int)cpu_percent(p)); break;
    case F_VSZ: snprintf(buf, sizeof buf, "%llu", p->vsize / 1024); break;
    case F_SZ: snprintf(buf, sizeof buf, "%llu", p->vsize / (unsigned long long)sysconf(_SC_PAGESIZE)); break;
    case F_NICE:
        if (p->realtime)
            snprintf(buf, sizeof buf, "-");
        else
            snprintf(buf, sizeof buf, "%ld", p->nice);
        break;
    case F_PRI: snprintf(buf, sizeof buf, "%ld", p->priority + 60); break;
    case F_ETIME: {
        long long e = (long long)(now - boot) - (long long)(p->start / (unsigned long long)hz);
        duration(buf, sizeof buf, e < 0 ? 0 : (unsigned long long)e, false);
        break;
    }
    case F_TIME: duration(buf, sizeof buf, (p->utime + p->stime) / (unsigned long long)hz, true); break;
    case F_TTY: {
        const char *t = tty_name(p->tty);
        snprintf(buf, sizeof buf, "%s", t ? t : "?");
        break;
    }
    case F_COMM: snprintf(buf, sizeof buf, "%s", p->comm); break;
    case F_CMDNAME:
        snprintf(buf, sizeof buf, "%s%s", p->comm, p->state == 'Z' ? " <defunct>" : "");
        break;
    case F_ARGS: {
        const char *d = p->state == 'Z' ? " <defunct>" : "";
        if (p->args && p->state != 'Z') {
            size_t n = strlen(p->args) + strlen(d) + 1;
            char *r = xmalloc(n);
            snprintf(r, n, "%s%s", p->args, d);
            return r;
        }
        snprintf(buf, sizeof buf, "[%s]%s", p->comm, d);
        break;
    }
    case F_F:
        snprintf(buf, sizeof buf, "%lu", (p->flags & 0x40 ? 1UL : 0) | (p->flags & 0x100 ? 4UL : 0));
        break;
    case F_S: snprintf(buf, sizeof buf, "%c", p->state); break;
    case F_ADDR: snprintf(buf, sizeof buf, "-"); break;
    case F_WCHAN: snprintf(buf, sizeof buf, "%s", p->wchan); break;
    case F_STIME: {
        time_t st = (time_t)(boot + (long long)(p->start / (unsigned long long)hz));
        struct tm tm_st, tm_now;
        localtime_r(&st, &tm_st);
        localtime_r(&now, &tm_now);
        if (now - st < 86400)
            strftime(buf, sizeof buf, "%H:%M", &tm_st);
        else if (tm_st.tm_year == tm_now.tm_year)
            strftime(buf, sizeof buf, "%b%d", &tm_st);
        else
            strftime(buf, sizeof buf, "%Y", &tm_st);
        break;
    }
    }
    return xstrdup(buf);
}

/* ---- the lists of the selection options ---- */

struct list {
    char **items;
    size_t n;
};

static bool split_list(const char *s, struct list *l)
{
    char *copy = xstrdup(s);
    bool any = false;
    for (char *t = strtok(copy, " \t,"); t; t = strtok(NULL, " \t,")) {
        l->items = realloc(l->items, (l->n + 1) * sizeof *l->items);
        if (!l->items) {
            fprintf(stderr, "ps: out of memory\n");
            exit(1);
        }
        l->items[l->n++] = xstrdup(t);
        any = true;
    }
    free(copy);
    return any;
}

static bool is_number(const char *s)
{
    return *s && strspn(s, "0123456789") == strlen(s);
}

static bool in_numbers(const struct list *l, long v)
{
    for (size_t i = 0; i < l->n; i++)
        if (is_number(l->items[i]) && strtol(l->items[i], NULL, 10) == v)
            return true;
    return false;
}

static bool in_users(const struct list *l, long uid)
{
    for (size_t i = 0; i < l->n; i++) {
        struct passwd *pw = getpwnam(l->items[i]);
        if (pw ? (long)pw->pw_uid == uid : is_number(l->items[i]) && strtol(l->items[i], NULL, 10) == uid)
            return true;
    }
    return false;
}

static bool in_groups(const struct list *l, long gid)
{
    for (size_t i = 0; i < l->n; i++) {
        struct group *gr = getgrnam(l->items[i]);
        if (gr ? (long)gr->gr_gid == gid : is_number(l->items[i]) && strtol(l->items[i], NULL, 10) == gid)
            return true;
    }
    return false;
}

static bool in_ttys(const struct list *l, long dev)
{
    const char *name = tty_name(dev);
    for (size_t i = 0; i < l->n; i++) {
        const char *t = l->items[i];
        if (strncmp(t, "/dev/", 5) == 0)
            t += 5;
        if (strcmp(t, "?") == 0 || strcmp(t, "-") == 0) {
            if (!name)
                return true;
            continue;
        }
        if (!name)
            continue;
        if (strcmp(t, name) == 0)
            return true;
        if (strncmp(name, "tty", 3) == 0 && strcmp(name + 3, t) == 0)
            return true;
    }
    return false;
}

static void usage(void)
{
    fprintf(stderr, "usage: ps [-aAw] [-defl] [-g grouplist] [-G grouplist] [-n namelist]\n"
                    "          [-o format]... [-p proclist] [-t termlist] [-u userlist] [-U userlist]\n");
    exit(1);
}

static int cmp_long(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool oa = false, oA = false, od = false, of = false, ol = false;
    int ow = 0;
    bool selecting = false;
    struct list lg = { 0 }, lG = { 0 }, lp = { 0 }, lt = { 0 }, lu = { 0 }, lU = { 0 };
    bool have_o = false;
    int c;
    while ((c = getopt(argc, argv, "aAdefg:G:ln:o:p:t:u:U:w")) != -1) {
        switch (c) {
        case 'a': oa = selecting = true; break;
        case 'A': case 'e': oA = selecting = true; break;
        case 'd': od = selecting = true; break;
        case 'f': of = true; break;
        case 'l': ol = true; break;
        case 'w': ow++; break;
        case 'n': break;
        case 'o':
            have_o = true;
            if (!parse_format(optarg))
                usage();
            break;
        case 'g': case 'G': case 'p': case 't': case 'u': case 'U': {
            struct list *l = c == 'g' ? &lg : c == 'G' ? &lG : c == 'p' ? &lp : c == 't' ? &lt
                           : c == 'u' ? &lu : &lU;
            if (!split_list(optarg, l)) {
                fprintf(stderr, "ps: -%c: the list is empty\n", c);
                usage();
            }
            if ((c == 'g' || c == 'p'))
                for (size_t i = 0; i < l->n; i++)
                    if (!is_number(l->items[i])) {
                        fprintf(stderr, "ps: -%c %s: not a number\n", c, l->items[i]);
                        usage();
                    }
            selecting = true;
            break;
        }
        default: usage();
        }
    }
    if (optind < argc)
        usage();
    if (!have_o) {
        if (ol) {
            add_column(F_F, "F", true);
            add_column(F_S, "S", false);
        }
        if (of || ol) {
            add_column(F_UID, "UID", !of);
            add_column(F_PID, "PID", true);
            add_column(F_PPID, "PPID", true);
            add_column(F_C, "C", true);
        } else
            add_column(F_PID, "PID", true);
        if (ol) {
            add_column(F_PRI, "PRI", true);
            add_column(F_NICE, "NI", true);
            add_column(F_ADDR, "ADDR", false);
            add_column(F_SZ, "SZ", true);
            add_column(F_WCHAN, "WCHAN", false);
        }
        if (of)
            add_column(F_STIME, "STIME", false);
        add_column(F_TTY, "TTY", false);
        add_column(F_TIME, "TIME", true);
        add_column(of ? F_ARGS : F_CMDNAME, "CMD", false);
    }

    hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0)
        hz = 100;
    now = time(NULL);
    char *st = read_text("/proc/stat", NULL);
    char *bt = st ? strstr(st, "\nbtime ") : NULL;
    boot = bt ? atoll(bt + 7) : 0;
    free(st);

    struct proc self;
    long my_tty = 0, my_euid = (long)geteuid();
    if (read_proc((long)getpid(), &self))
        my_tty = self.tty;
    free(self.args);

    /* the process IDs */
    DIR *d = opendir("/proc");
    if (!d) {
        fprintf(stderr, "ps: /proc: %s\n", strerror(errno));
        return 1;
    }
    long *pids = NULL;
    size_t npids = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (is_number(e->d_name)) {
            pids = realloc(pids, (npids + 1) * sizeof *pids);
            if (!pids) {
                fprintf(stderr, "ps: out of memory\n");
                return 1;
            }
            pids[npids++] = strtol(e->d_name, NULL, 10);
        }
    closedir(d);
    qsort(pids, npids, sizeof *pids, cmp_long);

    /* the rows */
    char ***rows = NULL;
    size_t nrows = 0;
    for (size_t i = 0; i < npids; i++) {
        struct proc p;
        if (!read_proc(pids[i], &p))
            continue;
        bool leader = p.session == p.pid;
        bool sel;
        if (!selecting)
            sel = p.euid == my_euid && p.tty == my_tty;
        else
            sel = oA || (oa && p.tty != 0 && !leader) || (od && !leader) ||
                  (lg.n && in_numbers(&lg, p.session)) || (lG.n && in_groups(&lG, p.rgid)) ||
                  (lp.n && in_numbers(&lp, p.pid)) || (lt.n && in_ttys(&lt, p.tty)) ||
                  (lu.n && in_users(&lu, p.euid)) || (lU.n && in_users(&lU, p.ruid));
        if (!sel) {
            free(p.args);
            continue;
        }
        char **row = xmalloc(ncols * sizeof *row);
        for (size_t k = 0; k < ncols; k++) {
            row[k] = value(&p, cols[k].id, of);
            if ((k + 1 < ncols || cols[k].right) && strlen(row[k]) > cols[k].width)
                cols[k].width = strlen(row[k]);
        }
        rows = realloc(rows, (nrows + 1) * sizeof *rows);
        if (!rows) {
            fprintf(stderr, "ps: out of memory\n");
            return 1;
        }
        rows[nrows++] = row;
        free(p.args);
    }

    /* the output, lines cut at the limit */
    long limit = sysconf(_SC_LINE_MAX);
    if (limit < 2048)
        limit = 2048;
    const char *ce = getenv("COLUMNS");
    if (ce && is_number(ce) && strtol(ce, NULL, 10) > limit)
        limit = strtol(ce, NULL, 10);
    if (ow == 1 && limit < 132)
        limit = 132;
    if (ow >= 2)
        limit = 0;
    bool header = false;
    for (size_t k = 0; k < ncols; k++)
        header |= cols[k].header[0] != '\0';
    size_t linecap = 1024;
    char *line = xmalloc(linecap);
    for (size_t r = header ? 0 : 1; r <= nrows; r++) {
        size_t n = 0;
        for (size_t k = 0; k < ncols; k++) {
            const char *v = r == 0 ? cols[k].header : rows[r - 1][k];
            size_t vl = strlen(v), w = cols[k].width;
            size_t need = n + vl + w + 2;
            if (need > linecap) {
                linecap = need * 2;
                line = realloc(line, linecap);
                if (!line) {
                    fprintf(stderr, "ps: out of memory\n");
                    return 1;
                }
            }
            if (k > 0)
                line[n++] = ' ';
            size_t pad = vl < w ? w - vl : 0;
            if (cols[k].right) {
                memset(line + n, ' ', pad);
                n += pad;
            }
            memcpy(line + n, v, vl);
            n += vl;
            if (!cols[k].right && k + 1 < ncols) {
                memset(line + n, ' ', pad);
                n += pad;
            }
        }
        while (n > 0 && line[n - 1] == ' ')
            n--;
        if (limit > 0 && n > (size_t)limit)
            n = (size_t)limit;
        line[n] = '\0';
        if (puts(line) == EOF)
            break;
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "ps: write error: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
