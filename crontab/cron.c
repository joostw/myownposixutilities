/*
 * cron.c - the crontab spool and entries (shared by crontab and crond)
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "cron.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool set_id(void)
{
    return getuid() != geteuid() || getgid() != getegid();
}

const char *cron_spool(void)
{
    const char *e = getenv("CRON_SPOOL");
    return !set_id() && e && *e ? e : CRON_SPOOL;
}

const char *cron_confdir(void)
{
    const char *e = getenv("CRON_CONFDIR");
    return !set_id() && e && *e ? e : CRON_CONFDIR;
}

/* a pattern: "*", or elements (n or n-m) separated by commas */
static bool pattern(const char *s, size_t len, int lo, int hi, unsigned long long *bits, bool *star)
{
    *bits = 0;
    *star = false;
    if (len == 1 && s[0] == '*') {
        *star = true;
        for (int i = lo; i <= hi; i++)
            *bits |= 1ULL << i;
        return true;
    }
    const char *p = s, *end = s + len;
    while (p < end) {
        if (!isdigit((unsigned char)*p))
            return false;
        long a = 0, b;
        while (p < end && isdigit((unsigned char)*p) && a < 1000)
            a = a * 10 + (*p++ - '0');
        b = a;
        if (p < end && *p == '-') {
            p++;
            if (p >= end || !isdigit((unsigned char)*p))
                return false;
            b = 0;
            while (p < end && isdigit((unsigned char)*p) && b < 1000)
                b = b * 10 + (*p++ - '0');
        }
        if (a < lo || b > hi || a > b)
            return false;
        for (long i = a; i <= b; i++)
            *bits |= 1ULL << i;
        if (p < end) {
            if (*p != ',')
                return false;
            p++;
            if (p >= end)
                return false;
        }
    }
    return true;
}

int cron_parse(const char *line, struct entry *e, char *err, size_t errsize)
{
    memset(e, 0, sizeof *e);
    const char *p = line;
    while (*p == ' ' || *p == '\t')
        p++;
    if (!*p || *p == '\n' || *p == '#')
        return 0;
    static const struct {
        const char *name;
        int lo, hi;
    } f[5] = { { "minute", 0, 59 }, { "hour", 0, 23 }, { "day of the month", 1, 31 },
               { "month", 1, 12 }, { "day of the week", 0, 6 } };
    unsigned long long bits[5];
    bool star[5];
    for (int i = 0; i < 5; i++) {
        size_t n = strcspn(p, " \t\n");
        if (n == 0) {
            snprintf(err, errsize, "the line has fewer than six fields");
            return -1;
        }
        if (!pattern(p, n, f[i].lo, f[i].hi, &bits[i], &star[i])) {
            snprintf(err, errsize, "%.*s: not a pattern of the %s (%d to %d)", (int)(n > 40 ? 40 : n), p,
                     f[i].name, f[i].lo, f[i].hi);
            return -1;
        }
        p += n;
        while (*p == ' ' || *p == '\t')
            p++;
    }
    size_t n = strcspn(p, "\n");
    if (n == 0) {
        snprintf(err, errsize, "the command is missing");
        return -1;
    }
    e->minute = bits[0];
    e->hour = (unsigned long)bits[1];
    e->mday = (unsigned long)bits[2];
    e->month = (unsigned long)bits[3];
    e->wday = (unsigned long)bits[4];
    e->mday_star = star[2];
    e->month_star = star[3];
    e->wday_star = star[4];
    e->command = strndup(p, n);
    return e->command ? 1 : -1;
}

bool cron_match(const struct entry *e, const struct tm *tm)
{
    if (!(e->minute >> tm->tm_min & 1) || !(e->hour >> tm->tm_hour & 1))
        return false;
    bool md = (e->month >> (tm->tm_mon + 1) & 1) && (e->mday >> tm->tm_mday & 1);
    bool wd = e->wday >> tm->tm_wday & 1;
    if (e->wday_star)
        return md;
    if (e->month_star && e->mday_star)
        return wd;
    return md || wd;
}

void cron_command(const char *field, char **cmd, char **input)
{
    size_t n = strlen(field);
    char *c = malloc(n + 1), *in = malloc(n + 2);
    if (!c || !in) {
        free(c);
        free(in);
        *cmd = *input = NULL;
        return;
    }
    size_t ci = 0, ii = 0;
    bool in_input = false;
    for (size_t i = 0; i < n; i++) {
        char ch = field[i];
        if (ch == '\\' && i + 1 < n && field[i + 1] == '%') {
            /* \% is a percent sign; other backslashes stay, for the shell */
            ch = field[++i];
        } else if (ch == '%') {
            if (!in_input)
                in_input = true;
            else
                in[ii++] = '\n';
            continue;
        }
        if (in_input)
            in[ii++] = ch;
        else
            c[ci++] = ch;
    }
    if (in_input)
        in[ii++] = '\n';
    c[ci] = '\0';
    in[ii] = '\0';
    *cmd = c;
    *input = in;
}

static int listed(const char *path, const char *user, bool *empty)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[512];
    int found = 0;
    *empty = true;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *s = line, *e;
        while (*s == ' ' || *s == '\t')
            s++;
        e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = '\0';
        if (!*s)
            continue;
        *empty = false;
        if (strcmp(s, user) == 0)
            found = 1;
    }
    fclose(f);
    return found;
}

bool cron_allowed(const char *user, bool privileged)
{
    if (privileged)
        return true;
    char path[4096];
    bool empty;
    snprintf(path, sizeof path, "%s/cron.allow", cron_confdir());
    int r = listed(path, user, &empty);
    if (r >= 0)
        return r == 1;
    snprintf(path, sizeof path, "%s/cron.deny", cron_confdir());
    r = listed(path, user, &empty);
    if (r >= 0)
        return r == 0;
    return false;
}
