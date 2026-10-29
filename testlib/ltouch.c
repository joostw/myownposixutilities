/*
 * ltouch.c - set file times without following symbolic links, for the
 * tests:  ltouch -t CCYYMMDDhhmm[.SS] file...
 *         ltouch -d 'YYYY-MM-DD hh:mm:SS[.frac]' file...
 *
 * POSIX touch has no option for the times of a symbolic link itself, so
 * the tests compile this.  The times are in the forms of touch -t (with
 * the century and year) and touch -d (T or a space between date and
 * time), as local time (TZ).  Both the access and the modification time
 * are set, with utimensat() and AT_SYMLINK_NOFOLLOW.
 */

#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int num(const char *s, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

/* the time; -1 if the string is not one */
static int parse(const char *opt, const char *s, struct timespec *ts)
{
    struct tm tm;
    long nsec = 0;
    size_t n = strlen(s);
    memset(&tm, 0, sizeof tm);
    if (strcmp(opt, "-t") == 0) {
        if (n != 12 && !(n == 15 && s[12] == '.'))
            return -1;
        tm.tm_year = num(s, 4) - 1900;
        tm.tm_mon = num(s + 4, 2) - 1;
        tm.tm_mday = num(s + 6, 2);
        tm.tm_hour = num(s + 8, 2);
        tm.tm_min = num(s + 10, 2);
        tm.tm_sec = n == 15 ? num(s + 13, 2) : 0;
    } else {
        /* YYYY-MM-DD hh:mm:SS[.frac] */
        if (n < 19 || s[4] != '-' || s[7] != '-' || (s[10] != ' ' && s[10] != 'T') ||
            s[13] != ':' || s[16] != ':')
            return -1;
        tm.tm_year = num(s, 4) - 1900;
        tm.tm_mon = num(s + 5, 2) - 1;
        tm.tm_mday = num(s + 8, 2);
        tm.tm_hour = num(s + 11, 2);
        tm.tm_min = num(s + 14, 2);
        tm.tm_sec = num(s + 17, 2);
        if (n > 19) {
            if (s[19] != '.' && s[19] != ',')
                return -1;
            int digits = 0;
            for (const char *p = s + 20; *p; p++) {
                if (*p < '0' || *p > '9')
                    return -1;
                if (digits++ < 9)
                    nsec = nsec * 10 + (*p - '0');
            }
            for (; digits < 9; digits++)
                nsec *= 10;
        }
    }
    tm.tm_isdst = -1;
    ts->tv_sec = mktime(&tm);
    ts->tv_nsec = nsec;
    return 0;
}

int main(int argc, char *argv[])
{
    struct timespec ts[2];
    if (argc < 4 || (strcmp(argv[1], "-t") != 0 && strcmp(argv[1], "-d") != 0)) {
        fprintf(stderr, "usage: ltouch -t CCYYMMDDhhmm[.SS] | -d 'YYYY-MM-DD hh:mm:SS[.frac]' file...\n");
        return 2;
    }
    if (parse(argv[1], argv[2], &ts[0]) != 0) {
        fprintf(stderr, "ltouch: bad time %s\n", argv[2]);
        return 2;
    }
    ts[1] = ts[0];
    int status = 0;
    for (int i = 3; i < argc; i++)
        if (utimensat(AT_FDCWD, argv[i], ts, AT_SYMLINK_NOFOLLOW) != 0) {
            perror(argv[i]);
            status = 1;
        }
    return status;
}
