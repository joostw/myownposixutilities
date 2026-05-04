/*
 * touch — change file access and modification times  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis: touch [-acm] [-r ref_file|-t time|-d date_time] file...
 */

#define _GNU_SOURCE  /* for timegm() */

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static bool opt_a, opt_c, opt_m;
static struct timespec ts[2];  /* [0]=access, [1]=modification */
static int had_error;

/*
 * Parse -t [[CC]YY]MMDDhhmm[.SS]
 */
static int parse_t(const char *s, struct timespec *out)
{
    const char *dot = strchr(s, '.');
    size_t main_len = dot ? (size_t)(dot - s) : strlen(s);

    if (main_len != 8 && main_len != 10 && main_len != 12)
        return -1;

    for (size_t i = 0; i < main_len; i++)
        if (s[i] < '0' || s[i] > '9') return -1;

    int ss = 0;
    if (dot) {
        if (strlen(dot) != 3) return -1;
        if (dot[1] < '0' || dot[1] > '9' || dot[2] < '0' || dot[2] > '9') return -1;
        ss = (dot[1] - '0') * 10 + (dot[2] - '0');
        if (ss > 60) return -1;
    }

    const char *p = s;
    struct tm tm = {0};
    tm.tm_isdst = -1;

    if (main_len == 12) {
        int cc = (p[0]-'0')*10 + (p[1]-'0');
        int yy = (p[2]-'0')*10 + (p[3]-'0');
        tm.tm_year = cc * 100 + yy - 1900;
        p += 4;
    } else if (main_len == 10) {
        int yy = (p[0]-'0')*10 + (p[1]-'0');
        tm.tm_year = ((yy >= 69) ? 1900 : 2000) + yy - 1900;
        p += 2;
    } else {
        time_t now = time(NULL);
        struct tm *lt = localtime(&now);
        tm.tm_year = lt->tm_year;
    }

    tm.tm_mon  = (p[0]-'0')*10 + (p[1]-'0') - 1;
    tm.tm_mday = (p[2]-'0')*10 + (p[3]-'0');
    tm.tm_hour = (p[4]-'0')*10 + (p[5]-'0');
    tm.tm_min  = (p[6]-'0')*10 + (p[7]-'0');
    tm.tm_sec  = ss;

    if (tm.tm_mon < 0 || tm.tm_mon > 11) return -1;
    if (tm.tm_mday < 1 || tm.tm_mday > 31) return -1;
    if (tm.tm_hour > 23) return -1;
    if (tm.tm_min > 59) return -1;

    time_t t = mktime(&tm);
    if (t == (time_t)-1) return -1;

    out->tv_sec  = t;
    out->tv_nsec = 0;
    return 0;
}

static int isdigit2(const char *p)
{
    return (p[0] >= '0' && p[0] <= '9') && (p[1] >= '0' && p[1] <= '9');
}

static int d2(const char *p) { return (p[0]-'0')*10 + (p[1]-'0'); }

/*
 * Parse -d YYYY-MM-DDThh:mm:SS[.frac][tz]
 * T may be a single space; tz is empty (local) or 'Z' (UTC).
 */
static int parse_d(const char *s, struct timespec *out)
{
    size_t i = 0;
    size_t len = strlen(s);

    /* year: at least four digits */
    int year = 0;
    size_t yr_start = i;
    while (i < len && s[i] >= '0' && s[i] <= '9')
        year = year * 10 + (s[i++] - '0');
    if (i - yr_start < 4 || i >= len || s[i] != '-') return -1;
    i++;

    if (i + 2 >= len || !isdigit2(s+i) || s[i+2] != '-') return -1;
    int mon = d2(s+i);  i += 3;

    if (i + 1 >= len || !isdigit2(s+i)) return -1;
    int day = d2(s+i);  i += 2;

    if (i >= len || (s[i] != 'T' && s[i] != ' ')) return -1;
    i++;

    if (i + 2 >= len || !isdigit2(s+i) || s[i+2] != ':') return -1;
    int hour = d2(s+i); i += 3;

    if (i + 2 >= len || !isdigit2(s+i) || s[i+2] != ':') return -1;
    int min  = d2(s+i); i += 3;

    if (i + 1 >= len || !isdigit2(s+i)) return -1;
    int sec  = d2(s+i); i += 2;

    long nsec = 0;
    if (i < len && (s[i] == '.' || s[i] == ',')) {
        i++;
        int ndigits = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9') {
            if (ndigits < 9) nsec = nsec * 10 + (s[i] - '0');
            ndigits++;
            i++;
        }
        if (ndigits == 0) return -1;  /* spec requires at least one digit */
        while (ndigits++ < 9) nsec *= 10;
    }

    bool utc = false;
    if (i < len) {
        if (s[i] == 'Z') { utc = true; i++; }
        else return -1;
    }
    if (i != len) return -1;

    if (mon < 1 || mon > 12) return -1;
    if (day < 1 || day > 31) return -1;
    if (hour > 23) return -1;
    if (min > 59) return -1;
    if (sec > 60) return -1;

    struct tm tm = {0};
    tm.tm_year  = year - 1900;
    tm.tm_mon   = mon - 1;
    tm.tm_mday  = day;
    tm.tm_hour  = hour;
    tm.tm_min   = min;
    tm.tm_sec   = sec;
    tm.tm_isdst = -1;

    time_t t = utc ? timegm(&tm) : mktime(&tm);
    if (t == (time_t)-1) return -1;

    out->tv_sec  = t;
    out->tv_nsec = nsec;
    return 0;
}

static void do_touch(const char *path)
{
    struct stat st;
    if (stat(path, &st) == -1) {
        if (errno != ENOENT) {
            fprintf(stderr, "touch: %s: %s\n", path, strerror(errno));
            had_error = 1;
            return;
        }
        if (opt_c) return;
        int fd = open(path, O_WRONLY | O_CREAT, 0666);
        if (fd == -1) {
            fprintf(stderr, "touch: %s: %s\n", path, strerror(errno));
            had_error = 1;
            return;
        }
        if (futimens(fd, ts) == -1) {
            fprintf(stderr, "touch: %s: %s\n", path, strerror(errno));
            had_error = 1;
        }
        close(fd);
        return;
    }

    if (utimensat(AT_FDCWD, path, ts, 0) == -1) {
        fprintf(stderr, "touch: %s: %s\n", path, strerror(errno));
        had_error = 1;
    }
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    const char *opt_r = NULL, *opt_t_arg = NULL, *opt_d_arg = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "acmd:r:t:")) != -1) {
        switch (opt) {
        case 'a': opt_a = true;       break;
        case 'c': opt_c = true;       break;
        case 'm': opt_m = true;       break;
        case 'd': opt_d_arg = optarg; break;
        case 'r': opt_r    = optarg;  break;
        case 't': opt_t_arg = optarg; break;
        default:
            fprintf(stderr,
                "usage: touch [-acm] [-r ref_file|-t time|-d date_time] file...\n");
            return 1;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc == 0) {
        fprintf(stderr,
            "usage: touch [-acm] [-r ref_file|-t time|-d date_time] file...\n");
        return 1;
    }

    /* Resolve time source */
    if (opt_r) {
        struct stat st;
        if (stat(opt_r, &st) == -1) {
            fprintf(stderr, "touch: %s: %s\n", opt_r, strerror(errno));
            return 1;
        }
        ts[0] = st.st_atim;
        ts[1] = st.st_mtim;
    } else if (opt_t_arg) {
        struct timespec base;
        if (parse_t(opt_t_arg, &base) == -1) {
            fprintf(stderr, "touch: invalid time: %s\n", opt_t_arg);
            return 1;
        }
        ts[0] = ts[1] = base;
    } else if (opt_d_arg) {
        struct timespec base;
        if (parse_d(opt_d_arg, &base) == -1) {
            fprintf(stderr, "touch: invalid date_time: %s\n", opt_d_arg);
            return 1;
        }
        ts[0] = ts[1] = base;
    } else {
        ts[0].tv_nsec = UTIME_NOW;
        ts[1].tv_nsec = UTIME_NOW;
    }

    /* If neither -a nor -m, update both */
    if (!opt_a && !opt_m)
        opt_a = opt_m = true;

    if (!opt_a) ts[0].tv_nsec = UTIME_OMIT;
    if (!opt_m) ts[1].tv_nsec = UTIME_OMIT;

    for (int i = 0; i < argc; i++)
        do_touch(argv[i]);

    return had_error ? 1 : 0;
}
