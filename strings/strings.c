/*
 * strings.c - strings: find printable strings in files (POSIX.1-2024, XCU
 * strings)
 *
 * Synopsis: strings [-a] [-t format] [-n number] [file...]
 *
 * A string is a run of at least number (4) printable characters of the
 * locale (iswprint(), characters decoded with mbrtowc(): a multibyte
 * character counts as one); each one is written on a line, with -t its
 * byte offset in the file first.
 *
 * Choices where the standard leaves room:
 *  - Files are always scanned whole; -a changes nothing.
 *  - Besides the strings that end with a newline or a NUL, the standard's,
 *    a run that ends with any other byte that is not printable, or at the
 *    end of the file, is written too (as other implementations do).
 *  - A tab is not printable (it is not in the class print), so it ends a
 *    string.
 *  - The operand "-" is a file named "-".
 *  - A file that cannot be read: a diagnostic, the others still done,
 *    status 1.  Usage errors: status 2.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

static long minlen = 4;
static char tfmt;               /* 0, 'd', 'o' or 'x' */
static int status;

/* the run: its bytes, its characters, where it began */
static char *run;
static size_t rlen, rcap, rchars;
static unsigned long long rstart;

static void flush(void)
{
    if (rchars >= (size_t)minlen) {
        if (tfmt == 'd')
            printf("%llu ", rstart);
        else if (tfmt == 'o')
            printf("%llo ", rstart);
        else if (tfmt == 'x')
            printf("%llx ", rstart);
        fwrite(run, 1, rlen, stdout);
        putchar('\n');
    }
    rlen = rchars = 0;
}

static void add(const char *s, size_t n, unsigned long long off)
{
    if (rlen == 0)
        rstart = off;
    if (rlen + n > rcap) {
        rcap = (rlen + n) * 2;
        char *r = realloc(run, rcap);
        if (!r) {
            fprintf(stderr, "strings: out of memory\n");
            exit(1);
        }
        run = r;
    }
    memcpy(run + rlen, s, n);
    rlen += n;
    rchars++;
}

static void scan(FILE *f, const char *name)
{
    unsigned char buf[65536 + MB_LEN_MAX];
    size_t have = 0;
    unsigned long long off = 0;   /* of buf[0] */
    bool eof = false;
    mbstate_t st;
    memset(&st, 0, sizeof st);
    for (;;) {
        if (!eof && have < MB_LEN_MAX) {
            size_t k = fread(buf + have, 1, sizeof buf - have, f);
            have += k;
            if (k == 0)
                eof = true;
        }
        if (have == 0)
            break;
        size_t i = 0;
        while (i < have && (eof || have - i >= MB_LEN_MAX)) {
            wchar_t wc;
            size_t k = mbrtowc(&wc, (char *)buf + i, have - i, &st);
            if (k == (size_t)-1 || k == (size_t)-2) {
                memset(&st, 0, sizeof st);
                flush();
                i++;
                continue;
            }
            if (k == 0)
                k = 1;
            if (wc != L'\0' && iswprint((wint_t)wc))
                add((char *)buf + i, k, off + i);
            else
                flush();
            i += k;
        }
        memmove(buf, buf + i, have - i);
        have -= i;
        off += i;
        if (eof && have == 0)
            break;
    }
    flush();
    if (ferror(f)) {
        fprintf(stderr, "strings: %s: %s\n", name, strerror(errno));
        status = 1;
    }
}

static void usage(void)
{
    fprintf(stderr, "usage: strings [-a] [-t format] [-n number] [file...]\n");
    exit(2);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "at:n:")) != -1) {
        switch (c) {
        case 'a': break;
        case 't':
            if ((optarg[0] != 'd' && optarg[0] != 'o' && optarg[0] != 'x') || optarg[1]) {
                fprintf(stderr, "strings: -t %s: the format is d, o or x\n", optarg);
                return 2;
            }
            tfmt = optarg[0];
            break;
        case 'n': {
            char *end;
            errno = 0;
            minlen = strtol(optarg, &end, 10);
            if (optarg[0] < '0' || optarg[0] > '9' || *end || errno || minlen < 1) {
                fprintf(stderr, "strings: -n %s: not a positive number\n", optarg);
                return 2;
            }
            break;
        }
        default: usage();
        }
    }
    if (optind == argc)
        scan(stdin, "standard input");
    for (int i = optind; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            fprintf(stderr, "strings: %s: %s\n", argv[i], strerror(errno));
            status = 1;
            continue;
        }
        scan(f, argv[i]);
        fclose(f);
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "strings: write error: %s\n", strerror(errno));
        return 1;
    }
    return status;
}
