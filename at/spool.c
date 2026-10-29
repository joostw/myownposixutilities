/*
 * spool.c - the at spool (shared by at, batch and atd)
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "spool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool set_id(void)
{
    return getuid() != geteuid() || getgid() != getegid();
}

const char *spool_dir(void)
{
    const char *e = getenv("AT_SPOOL");
    return !set_id() && e && *e ? e : AT_SPOOL;
}

const char *conf_dir(void)
{
    const char *e = getenv("AT_CONFDIR");
    return !set_id() && e && *e ? e : AT_CONFDIR;
}

bool job_id_ok(const char *id)
{
    const char *dot = strchr(id, '.');
    if (!dot || dot == id || strspn(id, "0123456789") != (size_t)(dot - id))
        return false;
    return dot[1] >= 'a' && dot[1] <= 'z' && !dot[2] && strlen(id) < 64;
}

bool job_read(const char *path, struct job *j)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (!job_id_ok(base))
        return false;
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    memset(j, 0, sizeof *j);
    snprintf(j->id, sizeof j->id, "%s", base);
    j->queue = strchr(base, '.')[1];
    char line[1100];
    bool have_owner = false, have_time = false, end = false;
    if (!fgets(line, sizeof line, f) || strncmp(line, "#!", 2) != 0) {
        fclose(f);
        return false;
    }
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, "# end") == 0) {
            end = true;
            break;
        }
        if (strncmp(line, "# owner ", 8) == 0) {
            j->owner = (uid_t)strtoul(line + 8, NULL, 10);
            have_owner = true;
        } else if (strncmp(line, "# time ", 7) == 0) {
            j->when = (time_t)strtoll(line + 7, NULL, 10);
            have_time = true;
        } else if (strncmp(line, "# mail ", 7) == 0)
            j->mail = line[7] == '1';
        else if (strncmp(line, "# shell ", 8) == 0)
            snprintf(j->shell, sizeof j->shell, "%.1000s", line + 8);
    }
    fclose(f);
    if (!j->shell[0])
        strcpy(j->shell, "sh");
    return end && have_owner && have_time;
}

void job_date(char *buf, size_t size, time_t t)
{
    static const char days[][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char mons[][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    struct tm *tm = localtime(&t);
    if (!tm) {
        snprintf(buf, size, "?");
        return;
    }
    snprintf(buf, size, "%s %s %2d %02d:%02d:%02d %d", days[tm->tm_wday], mons[tm->tm_mon],
             tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
}

/* is user a line of the file? -1 if there is no file */
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

bool user_allowed(const char *base, const char *user, bool privileged)
{
    if (privileged)
        return true;
    char path[4096];
    bool empty;
    snprintf(path, sizeof path, "%s/%s.allow", conf_dir(), base);
    int r = listed(path, user, &empty);
    if (r >= 0)
        return r == 1;
    snprintf(path, sizeof path, "%s/%s.deny", conf_dir(), base);
    r = listed(path, user, &empty);
    if (r >= 0)
        return r == 0;
    return false;
}
