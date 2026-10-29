/*
 * cal.c - cal: print a calendar (POSIX.1-2024, XCU cal) [XSI]
 *
 * Synopsis: cal [[month] year]
 *
 * The Julian calendar up to September 2, 1752, the Gregorian one from
 * September 14, 1752.  A day is placed by its weekday, found from its
 * Julian day number, so September 1752 has its eleven days left out with
 * no gap in its weeks.
 *
 * Choices where the standard leaves room (the format is unspecified):
 *  - The format of the historical cal: a month is its name and year
 *    centered over the weekdays "Su Mo Tu We Th Fr Sa" and its weeks, each
 *    day "%2d", the columns separated by a space; a year is the year
 *    centered over the months, three to a row, the rows separated by an
 *    empty line.  Lines have no trailing blanks, and a month (a row of
 *    months) has as many week lines as it needs.
 *  - The names of the months and of the weekdays are the locale's
 *    (nl_langinfo() MON_1 ..., ABDAY_1 ...), a weekday cut to two columns.
 *  - The operands are decimal integers; a month that is not 1 to 12 or a
 *    year that is not 1 to 9999 is an error (status 1); more than two
 *    operands is a usage error (status 2).
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

/* the Julian day number of a date; Julian calendar before 1752-09-14 */
static long jdn(long y, int m, int d)
{
    long a = (14 - m) / 12, yy = y + 4800 - a, mm = m + 12 * a - 3;
    bool gregorian = y > 1752 || (y == 1752 && (m > 9 || (m == 9 && d >= 14)));
    if (gregorian)
        return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
    return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - 32083;
}

static int month_days(long y, int m)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m != 2)
        return days[m - 1];
    bool leap = y <= 1752 ? y % 4 == 0 : (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    return leap ? 29 : 28;
}

/* the 6 week lines of a month, 20 columns each (blank-padded) */
struct month {
    char title[256];            /* centered in 20 columns, no trailing blanks */
    char weeks[6][21];
    int nweeks;
};

static size_t width(const char *s)
{
    wchar_t w[256];
    size_t n = mbstowcs(w, s, 256);
    if (n == (size_t)-1)
        return strlen(s);
    int c = wcswidth(w, n);
    return c < 0 ? n : (size_t)c;
}

/* s cut to at most cols columns, into buf */
static void cut(const char *s, size_t cols, char *buf, size_t size)
{
    mbstate_t st;
    memset(&st, 0, sizeof st);
    size_t used = 0, i = 0, o = 0;
    while (s[i]) {
        wchar_t wc;
        size_t k = mbrtowc(&wc, s + i, strlen(s + i), &st);
        if (k == (size_t)-1 || k == (size_t)-2 || k == 0) {
            k = 1;
            wc = (unsigned char)s[i];
            memset(&st, 0, sizeof st);
        }
        int w = wcwidth(wc);
        if (w < 0)
            w = 1;
        if (used + (size_t)w > cols || o + k >= size)
            break;
        memcpy(buf + o, s + i, k);
        o += k;
        i += k;
        used += (size_t)w;
    }
    /* pad to cols */
    while (used < cols && o + 1 < size) {
        buf[o++] = ' ';
        used++;
    }
    buf[o] = '\0';
}

static void center(char *buf, size_t size, const char *s, size_t cols)
{
    size_t w = width(s), pad = w < cols ? (cols - w) / 2 : 0;
    snprintf(buf, size, "%*s%s", (int)pad, "", s);
}

static void make_month(struct month *mo, long y, int m, bool with_year)
{
    char name[200];
    if (with_year)
        snprintf(name, sizeof name, "%s %ld", nl_langinfo(MON_1 + m - 1), y);
    else
        snprintf(name, sizeof name, "%s", nl_langinfo(MON_1 + m - 1));
    center(mo->title, sizeof mo->title, name, 20);
    for (int w = 0; w < 6; w++) {
        memset(mo->weeks[w], ' ', 20);
        mo->weeks[w][20] = '\0';
    }
    int row = 0, last = -1;
    for (int d = 1; d <= month_days(y, m); d++) {
        if (y == 1752 && m == 9 && d > 2 && d < 14)
            continue;
        int wd = (int)((jdn(y, m, d) + 1) % 7);     /* 0 is Sunday */
        if (last >= 0 && wd <= last)
            row++;
        last = wd;
        char cell[12];
        snprintf(cell, sizeof cell, "%2d", d);
        memcpy(mo->weeks[row] + wd * 3, cell, 2);
    }
    mo->nweeks = row + 1;
}

static char header[128];        /* the weekdays */

static void make_header(void)
{
    size_t o = 0;
    for (int i = 0; i < 7; i++) {
        char two[32];
        cut(nl_langinfo(ABDAY_1 + i), 2, two, sizeof two);
        o += (size_t)snprintf(header + o, sizeof header - o, "%s%s", i ? " " : "", two);
    }
}

static void put_line(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == ' ')
        n--;
    s[n] = '\0';
    puts(s);
}

static long number(const char *s, const char *what, long lo, long hi)
{
    char *end;
    if (*s < '0' || *s > '9')
        goto bad;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (*end || errno || v < lo || v > hi)
        goto bad;
    return v;
bad:
    fprintf(stderr, "cal: %s: not a %s (%ld to %ld)\n", s, what, lo, hi);
    exit(1);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int i = 1;
    if (i < argc && strcmp(argv[i], "--") == 0)
        i++;
    else if (i < argc && argv[i][0] == '-' && argv[i][1]) {
        fprintf(stderr, "cal: %s: unknown option\nusage: cal [[month] year]\n", argv[i]);
        return 2;
    }
    int nops = argc - i;
    if (nops > 2) {
        fprintf(stderr, "usage: cal [[month] year]\n");
        return 2;
    }
    make_header();
    long year;
    int month;
    if (nops == 0) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        if (!tm) {
            fprintf(stderr, "cal: cannot get the date\n");
            return 1;
        }
        year = tm->tm_year + 1900L;
        month = tm->tm_mon + 1;
    } else if (nops == 1) {
        year = number(argv[i], "year", 1, 9999);
        month = 0;
    } else {
        month = (int)number(argv[i], "month", 1, 12);
        year = number(argv[i + 1], "year", 1, 9999);
    }

    char line[512];
    if (month) {
        struct month mo;
        make_month(&mo, year, month, true);
        put_line(mo.title);
        snprintf(line, sizeof line, "%s", header);
        put_line(line);
        for (int w = 0; w < mo.nweeks; w++) {
            snprintf(line, sizeof line, "%s", mo.weeks[w]);
            put_line(line);
        }
    } else {
        char ys[32];
        snprintf(ys, sizeof ys, "%ld", year);
        center(line, sizeof line, ys, 64);
        put_line(line);
        for (int r = 0; r < 4; r++) {
            putchar('\n');
            struct month mo[3];
            int n = 0;
            for (int k = 0; k < 3; k++) {
                make_month(&mo[k], year, r * 3 + k + 1, false);
                if (mo[k].nweeks > n)
                    n = mo[k].nweeks;
            }
            /* the names: each in its 20 columns, 2 between */
            size_t o = 0;
            for (int k = 0; k < 3; k++) {
                size_t w = width(mo[k].title);
                o += (size_t)snprintf(line + o, sizeof line - o, "%s%*s%s", mo[k].title,
                                      (int)(w < 20 ? 20 - w : 0), "", k < 2 ? "  " : "");
            }
            put_line(line);
            snprintf(line, sizeof line, "%s  %s  %s", header, header, header);
            put_line(line);
            for (int w = 0; w < n; w++) {
                snprintf(line, sizeof line, "%s  %s  %s", mo[0].weeks[w], mo[1].weeks[w], mo[2].weeks[w]);
                put_line(line);
            }
        }
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "cal: write error: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
