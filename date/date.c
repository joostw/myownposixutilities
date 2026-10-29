/*
 * date.c - date: write the date and time (POSIX.1-2024, XCU date)
 *
 * Synopsis:
 *   date [-u] [+format]
 *   date [-u] mmddhhmm[[cc]yy]        [XSI]
 *
 * The format is given to strftime() with localtime(&now), now being
 * time(0) (-u sets TZ to UTC0, so that is gmtime(&now)); a newline is
 * added.
 * Without a format it is "%a %b %e %H:%M:%S %Z %Y".  The second form sets
 * the system clock (clock_settime(CLOCK_REALTIME)) to that time, in the
 * timezone of TZ (UTC with -u), with the seconds 0.
 *
 * %C, %F, %G and %Y with the flag '0' or '+' or a minimum field width are
 * converted here, as the standard describes them (strftime() of glibc
 * does not know '+'); the rest of the format is strftime()'s.
 *
 * Choices where the standard leaves room:
 *  - The default format is the same in every locale (the names in it are
 *    the locale's).
 *  - %C, %F, %G or %Y with a width and no flag pad with '0'; with a flag
 *    and no width they are as without both.
 *  - A '%' at the end of the format is written as it is.
 *  - When the clock is set, the new date is written in the default format.
 *  - A time that does not exist in the timezone (a day that the month does
 *    not have, an hour skipped for daylight saving time) is an error.
 *  - An operand that is neither a format nor a date, and a date that is
 *    not valid, are errors (status 1); more than one operand and an
 *    unknown option are usage errors (status 2).
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *const default_format = "%a %b %e %H:%M:%S %Z %Y";

static void *xrealloc(void *p, size_t n)
{
    if (!(p = realloc(p, n))) {
        fprintf(stderr, "date: out of memory\n");
        exit(1);
    }
    return p;
}

/* a growing string */
struct str {
    char *s;
    size_t len, cap;
};

static void add(struct str *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->s = xrealloc(b->s, b->cap);
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
}

/* a year (or century) with the flag '0' or '+' and a minimum width, as
   strftime() of POSIX.1-2024 gives it; '%' doubled, for strftime() */
static void add_year(struct str *b, long long v, char flag, long width, int digits_before_sign)
{
    char num[32];
    unsigned long long a = v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v;
    int nd = snprintf(num, sizeof num, "%llu", a);
    char sign = 0;
    if (v < 0)
        sign = '-';
    else if (flag == '+' && (nd > digits_before_sign || width > digits_before_sign))
        sign = '+';     /* the field takes more than 4 bytes (2 for %C) */
    long len = nd + (sign != 0);
    if (sign)
        add(b, &sign, 1);
    for (; len < width; len++)
        add(b, "0", 1);
    add(b, num, (size_t)nd);
}

/* the format, with %C, %F, %G and %Y that have a flag or a minimum field
   width done here: strftime() may not know them (glibc does not know '+') */
static char *expand(const char *f, const struct tm *tm)
{
    struct str b = { NULL, 0, 0 };
    add(&b, "", 0);
    while (*f) {
        if (*f != '%') {
            add(&b, f++, 1);
            continue;
        }
        const char *start = f++;
        char flag = 0;
        if (*f == '0' || *f == '+')
            flag = *f++;
        long width = -1;
        if (*f >= '1' && *f <= '9') {
            width = strtol(f, (char **)&f, 10);
            if (width > 4096)
                width = 4096;
        }
        char conv = *f;
        if ((flag || width >= 0) && conv && strchr("CFGY", conv)) {
            f++;
            long long year = tm->tm_year + 1900LL;
            if (conv == 'G') {
                char g[32];
                if (strftime(g, sizeof g, "%G", tm) > 0)
                    year = strtoll(g, NULL, 10);
            }
            if (conv == 'C')
                add_year(&b, year / 100, flag ? flag : '0', width, 2);
            else if (conv == 'F') {
                long w = width < 6 ? 6 : width;
                add_year(&b, year, flag ? flag : '0', w - 6, 4);
                add(&b, "-%m-%d", 6);
            } else
                add_year(&b, year, flag ? flag : '0', width, 4);
            continue;
        }
        if (!conv) {
            add(&b, "%%", 2);   /* a '%' at the end: itself */
            continue;
        }
        f++;
        add(&b, start, (size_t)(f - start));
    }
    return b.s;
}

/* write t in format, and a newline */
static int show(const char *format0, time_t t, bool utc)
{
    (void)utc;                  /* -u has set TZ to UTC0 */
    struct tm *tm = localtime(&t);
    if (!tm) {
        fprintf(stderr, "date: the time cannot be represented\n");
        return 1;
    }
    char *format = expand(format0, tm);
    /* strftime() gives 0 for an empty result too: a character is added
       to the format and taken away from the result */
    size_t flen = strlen(format);
    char *f = malloc(flen + 2);
    if (!f) {
        fprintf(stderr, "date: out of memory\n");
        return 1;
    }
    memcpy(f, format, flen);
    f[flen] = 'x';
    f[flen + 1] = '\0';
    size_t cap = 256, n;
    char *buf = NULL;
    for (;;) {
        char *nb = realloc(buf, cap);
        if (!nb) {
            fprintf(stderr, "date: out of memory\n");
            return 1;
        }
        buf = nb;
        n = strftime(buf, cap, f, tm);
        if (n > 0)
            break;
        if (cap > 64 * 1024 * 1024) {
            fprintf(stderr, "date: the format gives too long a result\n");
            return 1;
        }
        cap *= 2;
    }
    buf[n - 1] = '\n';
    if (fwrite(buf, 1, n, stdout) != n || fflush(stdout) != 0) {
        fprintf(stderr, "date: write error: %s\n", strerror(errno));
        return 1;
    }
    free(buf);
    free(f);
    free(format);
    return 0;
}

static int two(const char *s)
{
    return (s[0] - '0') * 10 + (s[1] - '0');
}

/* set the clock from mmddhhmm[[cc]yy] */
static int set_date(const char *s, bool utc)
{
    size_t len = strlen(s);
    if ((len != 8 && len != 10 && len != 12) || strspn(s, "0123456789") != len) {
        fprintf(stderr, "date: %s: not a date (mmddhhmm[[cc]yy]) or a format (+format)\n", s);
        return 1;
    }
    time_t now = time(NULL);
    struct tm *cur = utc ? gmtime(&now) : localtime(&now);
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    int mon = two(s), day = two(s + 2), hour = two(s + 4), min = two(s + 6), year;
    if (len == 12)
        year = two(s + 8) * 100 + two(s + 10);
    else if (len == 10) {
        int yy = two(s + 8);
        year = yy >= 69 ? 1900 + yy : 2000 + yy;
    } else
        year = cur ? cur->tm_year + 1900 : 1970;
    if (mon < 1 || mon > 12 || day < 1 || day > 31 || hour > 23 || min > 59) {
        fprintf(stderr, "date: %s: not a valid date\n", s);
        return 1;
    }
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    struct tm want = tm;
    errno = 0;
    time_t t = mktime(&tm);
    if ((t == (time_t)-1 && errno) || tm.tm_year != want.tm_year || tm.tm_mon != want.tm_mon ||
        tm.tm_mday != want.tm_mday || tm.tm_hour != want.tm_hour || tm.tm_min != want.tm_min) {
        fprintf(stderr, "date: %s: no such time\n", s);
        return 1;
    }
    struct timespec ts = { t, 0 };
    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        fprintf(stderr, "date: cannot set the date: %s\n", strerror(errno));
        return 1;
    }
    return show(default_format, t, utc);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool utc = false;
    int c;
    while ((c = getopt(argc, argv, "u")) != -1) {
        if (c != 'u') {
            fprintf(stderr, "usage: date [-u] [+format]\n"
                            "       date [-u] mmddhhmm[[cc]yy]\n");
            return 2;
        }
        utc = true;
    }
    if (argc - optind > 1) {
        fprintf(stderr, "usage: date [-u] [+format]\n"
                        "       date [-u] mmddhhmm[[cc]yy]\n");
        return 2;
    }
    if (utc) {
        if (setenv("TZ", "UTC0", 1) != 0) {
            fprintf(stderr, "date: %s\n", strerror(errno));
            return 1;
        }
    }
    tzset();
    if (optind < argc && argv[optind][0] != '+')
        return set_date(argv[optind], utc);
    const char *format = optind < argc ? argv[optind] + 1 : default_format;
    return show(format, time(NULL), utc);
}
