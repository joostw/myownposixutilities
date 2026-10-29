/*
 * touch.c - touch: change file access and modification times
 *   (POSIX.1-2024, XCU touch)
 *
 * Synopsis: touch [-acm] [-r ref_file|-t time|-d date_time] file...
 *
 * -r, -t and -d are mutually exclusive.  -t and -d dates must exist (no
 * 30 February); seconds 60 means one second after second 59.  A date
 * the timestamps cannot hold is an error, before any file is touched.
 * Times before the Epoch are accepted where the system can hold them.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE     /* timegm(); with _POSIX_C_SOURCE, getopt() stays POSIX (no permuting) */

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* the largest time_t (a signed integer type) */
#define TIME_MAX_ ((time_t)((((uintmax_t)1 << (sizeof(time_t) * 8 - 2)) - 1) * 2 + 1))

static const char usage_msg[] =
    "usage: touch [-acm] [-r ref_file|-t time|-d date_time] file...\n";

static bool opt_a, opt_c, opt_m;
static struct timespec ts[2];  /* [0]=access, [1]=modification */
static int had_error;

static bool isdigit2(const char *p)
{
    return p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9';
}

static int d2(const char *p) { return (p[0] - '0') * 10 + (p[1] - '0'); }

/*
 * The time of a date that was read, as local time (affected by TZ) or
 * UTC.  The fields must name a real date; seconds 60 is one second
 * after second 59 of that minute.
 */
static int make_time(int year, int mon, int day, int hour, int min, int sec,
                     bool utc, time_t *out)
{
    if (mon < 1 || mon > 12 || day < 1 || day > 31 || hour > 23 || min > 59 || sec > 60)
        return -1;
    struct tm tm = {0};
    tm.tm_year  = year - 1900;
    tm.tm_mon   = mon - 1;
    tm.tm_mday  = day;
    tm.tm_hour  = hour;
    tm.tm_min   = min;
    tm.tm_sec   = sec == 60 ? 59 : sec;
    tm.tm_isdst = -1;
    errno = 0;
    time_t t = utc ? timegm(&tm) : mktime(&tm);
    if (t == (time_t)-1 && errno != 0)
        return -1;                      /* not representable */
    /* mktime() moves an impossible date (31 April) on: refuse it */
    if (tm.tm_year != year - 1900 || tm.tm_mon != mon - 1 || tm.tm_mday != day)
        return -1;
    if (sec == 60) {
        if (t == TIME_MAX_)
            return -1;
        t++;
    }
    *out = t;
    return 0;
}

/* -t [[CC]YY]MMDDhhmm[.SS] */
static int parse_t(const char *s, struct timespec *out)
{
    const char *dot = strchr(s, '.');
    size_t main_len = dot ? (size_t)(dot - s) : strlen(s);

    if (main_len != 8 && main_len != 10 && main_len != 12)
        return -1;
    for (size_t i = 0; i < main_len; i++)
        if (s[i] < '0' || s[i] > '9')
            return -1;

    int ss = 0;
    if (dot) {
        if (strlen(dot) != 3 || !isdigit2(dot + 1))
            return -1;
        ss = d2(dot + 1);
    }

    const char *p = s;
    int year;
    if (main_len == 12) {
        year = d2(p) * 100 + d2(p + 2);
        p += 4;
    } else if (main_len == 10) {
        int yy = d2(p);
        year = (yy >= 69 ? 1900 : 2000) + yy;
        p += 2;
    } else {
        time_t now = time(NULL);
        year = localtime(&now)->tm_year + 1900;
    }

    time_t t;
    if (make_time(year, d2(p), d2(p + 2), d2(p + 4), d2(p + 6), ss, false, &t) == -1)
        return -1;
    out->tv_sec  = t;
    out->tv_nsec = 0;
    return 0;
}

/*
 * -d YYYY-MM-DDThh:mm:SS[.frac][tz]  (or [,frac])
 * T may be a single space; tz is empty (local time) or 'Z' (UTC).
 */
static int parse_d(const char *s, struct timespec *out)
{
    const char *p = s;

    int year = 0, ydigits = 0;
    while (*p >= '0' && *p <= '9') {
        if (year > 99999999)
            return -1;                  /* far beyond any timestamp */
        year = year * 10 + (*p++ - '0');
        ydigits++;
    }
    if (ydigits < 4 || *p++ != '-')
        return -1;
    if (!isdigit2(p) || p[2] != '-') return -1;
    int mon = d2(p);  p += 3;
    if (!isdigit2(p) || (p[2] != 'T' && p[2] != ' ')) return -1;
    int day = d2(p);  p += 3;
    if (!isdigit2(p) || p[2] != ':') return -1;
    int hour = d2(p); p += 3;
    if (!isdigit2(p) || p[2] != ':') return -1;
    int min = d2(p);  p += 3;
    if (!isdigit2(p)) return -1;
    int sec = d2(p);  p += 2;

    long nsec = 0;
    if (*p == '.' || *p == ',') {
        int ndigits = 0;
        p++;
        while (*p >= '0' && *p <= '9') {
            if (ndigits < 9)
                nsec = nsec * 10 + (*p - '0');
            ndigits++;
            p++;
        }
        if (ndigits == 0)
            return -1;                  /* one or more digits */
        for (; ndigits < 9; ndigits++)
            nsec *= 10;
    }

    bool utc = false;
    if (*p == 'Z') {
        utc = true;
        p++;
    }
    if (*p)
        return -1;

    time_t t;
    if (make_time(year, mon, day, hour, min, sec, utc, &t) == -1)
        return -1;
    out->tv_sec  = t;
    out->tv_nsec = nsec;
    return 0;
}

static void fail(const char *path)
{
    fprintf(stderr, "touch: %s: %s\n", path, strerror(errno));
    had_error = 1;
}

static void do_touch(const char *path)
{
    struct stat st;
    if (stat(path, &st) == -1) {
        if (errno != ENOENT) {
            fail(path);
            return;
        }
        if (opt_c)
            return;
        /* creat(), then futimens() */
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd == -1) {
            fail(path);
            return;
        }
        if (futimens(fd, ts) == -1)
            fail(path);
        close(fd);
        return;
    }
    if (utimensat(AT_FDCWD, path, ts, 0) == -1)
        fail(path);
}

int main(int argc, char *argv[])
{
    const char *opt_r = NULL, *opt_t = NULL, *opt_d = NULL;
    int opt;

    setlocale(LC_ALL, "");
    while ((opt = getopt(argc, argv, "acmd:r:t:")) != -1) {
        switch (opt) {
        case 'a': opt_a = true;   break;
        case 'c': opt_c = true;   break;
        case 'm': opt_m = true;   break;
        case 'd': opt_d = optarg; break;
        case 'r': opt_r = optarg; break;
        case 't': opt_t = optarg; break;
        default:
            fputs(usage_msg, stderr);
            return 1;
        }
    }
    argc -= optind;
    argv += optind;

    if ((opt_r != NULL) + (opt_t != NULL) + (opt_d != NULL) > 1) {
        fprintf(stderr, "touch: -r, -t and -d are mutually exclusive\n");
        fputs(usage_msg, stderr);
        return 1;
    }
    if (argc == 0) {
        fputs(usage_msg, stderr);
        return 1;
    }

    if (opt_r) {
        struct stat st;
        if (stat(opt_r, &st) == -1) {
            fail(opt_r);
            return 1;
        }
        ts[0] = st.st_atim;
        ts[1] = st.st_mtim;
    } else if (opt_t) {
        if (parse_t(opt_t, &ts[0]) == -1) {
            fprintf(stderr, "touch: invalid time: %s\n", opt_t);
            return 1;
        }
        ts[1] = ts[0];
    } else if (opt_d) {
        if (parse_d(opt_d, &ts[0]) == -1) {
            fprintf(stderr, "touch: invalid date_time: %s\n", opt_d);
            return 1;
        }
        ts[1] = ts[0];
    } else {
        ts[0].tv_nsec = ts[1].tv_nsec = UTIME_NOW;
    }

    if (!opt_a && !opt_m)
        opt_a = opt_m = true;
    if (!opt_a) ts[0].tv_nsec = UTIME_OMIT;
    if (!opt_m) ts[1].tv_nsec = UTIME_OMIT;

    for (int i = 0; i < argc; i++)
        do_touch(argv[i]);
    return had_error ? 1 : 0;
}
