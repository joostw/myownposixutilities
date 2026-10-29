/*
 * tail.c - tail: copy the last part of a file (POSIX.1-2024, XCU tail)
 *
 * Synopsis: tail [-f] [-c number|-n number] [file]
 *           tail -r [-n number] [file]
 *
 * number may have a sign: '+' counts from the beginning (origin 1, and
 * then it must not be zero), '-' or none from the end (0 copies nothing).
 * With -r, number has no sign and is the number of lines to copy, last
 * line first.
 *
 * Counts from the end are kept in buffers that grow with the input, so
 * any count works without allocating it in advance.  A regular file is
 * read from its current offset, like any other input.
 * -f follows a regular file or a FIFO named by the operand; it is
 * ignored when standard input is a pipe or FIFO and no file operand (or
 * "-") is given.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool      opt_f, opt_r, opt_c, opt_n;
static bool      from_start;        /* '+': from the beginning */
static long long count = 10;
static int       had_error;
static const char *name = "standard input";

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        perror("tail");
        exit(1);
    }
    return q;
}

static void out(const void *p, size_t n)
{
    if (n && fwrite(p, 1, n, stdout) != n) {
        perror("tail: write error");
        exit(1);
    }
}

static void read_error(FILE *f)
{
    if (ferror(f)) {
        fprintf(stderr, "tail: %s: %s\n", name, strerror(errno));
        had_error = 1;
    }
}

static void copy_rest(FILE *f)
{
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        out(buf, n);
    read_error(f);
}

/* ---- from the beginning ---- */

static void from_start_bytes(FILE *f)
{
    long long skip = count - 1;
    /* a seekable input: seek from its current offset */
    if (skip > 0 && fseeko(f, (off_t)skip, SEEK_CUR) == 0)
        skip = 0;
    while (skip > 0) {
        char buf[8192];
        size_t want = skip > (long long)sizeof buf ? sizeof buf : (size_t)skip;
        size_t n = fread(buf, 1, want, f);
        if (n == 0) {
            read_error(f);
            return;
        }
        skip -= (long long)n;
    }
    copy_rest(f);
}

static void from_start_lines(FILE *f)
{
    long long skip = count - 1;
    int c;
    while (skip > 0 && (c = getc(f)) != EOF)
        if (c == '\n')
            skip--;
    copy_rest(f);
}

/* ---- from the end ---- */

/* the last count bytes: a buffer holding at most twice what is kept */
static void from_end_bytes(FILE *f)
{
    char *buf = NULL;
    size_t len = 0, cap = 0, keep;
    if (count == 0) {
        read_error(f);
        return;
    }
    keep = (unsigned long long)count > SIZE_MAX / 4 ? SIZE_MAX / 4 : (size_t)count;
    for (;;) {
        size_t n;
        if (cap - len < 8192) {
            if (len > keep) {
                memmove(buf, buf + (len - keep), keep);
                len = keep;
            }
            if (cap - len < 8192) {
                cap = cap ? cap * 2 : 16384;
                buf = xrealloc(buf, cap);
            }
        }
        n = fread(buf + len, 1, cap - len, f);
        if (n == 0)
            break;
        len += n;
    }
    read_error(f);
    if (len > keep)
        out(buf + (len - keep), keep);
    else
        out(buf, len);
    free(buf);
}

/* the input's lines, kept as offsets into one buffer */
typedef struct {
    char *data;
    size_t len, cap;
    size_t *start;      /* offsets of the lines kept */
    size_t nlines, lcap, first;
} Lines;

/* read lines, keeping the last max of them (all of them if max is 0) */
static void read_lines(FILE *f, Lines *L, unsigned long long max)
{
    int c;
    bool at_line_start = true;
    memset(L, 0, sizeof *L);
    while ((c = getc(f)) != EOF) {
        if (at_line_start) {
            if (L->nlines == L->lcap) {
                L->lcap = L->lcap ? L->lcap * 2 : 64;
                L->start = xrealloc(L->start, L->lcap * sizeof *L->start);
            }
            L->start[L->nlines++] = L->len;
            at_line_start = false;
            /* drop lines no longer needed, now and then */
            if (max && L->nlines - L->first > max)
                L->first = L->nlines - max;
            if (max && L->first > 4096 && L->first > L->nlines / 2) {
                size_t base = L->start[L->first];
                memmove(L->data, L->data + base, L->len - base);
                L->len -= base;
                for (size_t i = L->first; i < L->nlines; i++)
                    L->start[i - L->first] = L->start[i] - base;
                L->nlines -= L->first;
                L->first = 0;
            }
        }
        if (L->len == L->cap) {
            L->cap = L->cap ? L->cap * 2 : 8192;
            L->data = xrealloc(L->data, L->cap);
        }
        L->data[L->len++] = (char)c;
        if (c == '\n')
            at_line_start = true;
    }
    read_error(f);
    if (max && L->nlines - L->first > max)
        L->first = L->nlines - max;
}

static size_t line_end(const Lines *L, size_t i)
{
    return i + 1 < L->nlines ? L->start[i + 1] : L->len;
}

static void from_end_lines(FILE *f)
{
    Lines L;
    if (count == 0) {
        read_error(f);
        return;
    }
    read_lines(f, &L, (unsigned long long)count);
    if (L.nlines > L.first)
        out(L.data + L.start[L.first], L.len - L.start[L.first]);
    free(L.data);
    free(L.start);
}

static void reverse_lines(FILE *f)
{
    Lines L;
    if (opt_n && count == 0) {
        read_error(f);
        return;
    }
    read_lines(f, &L, opt_n ? (unsigned long long)count : 0);
    for (size_t i = L.nlines; i-- > L.first; )
        out(L.data + L.start[i], line_end(&L, i) - L.start[i]);
    free(L.data);
    free(L.start);
}

/* ---- -f ---- */

static void follow(FILE *f)
{
    char buf[8192];
    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, f);
        if (n > 0) {
            out(buf, n);
            if (fflush(stdout) == EOF) {
                perror("tail: write error");
                exit(1);
            }
            continue;
        }
        if (ferror(f)) {
            read_error(f);
            return;
        }
        clearerr(f);
        sleep(1);
    }
}

/* the option-argument: an optional sign (when allowed) and digits */
static bool parse_number(const char *s, bool sign_ok)
{
    const char *p = s;
    long long v = 0;
    from_start = false;
    if (*p == '+' || *p == '-') {
        if (!sign_ok)
            return false;
        from_start = *p == '+';
        p++;
    }
    if (!*p)
        return false;
    for (; *p; p++) {
        if (*p < '0' || *p > '9')
            return false;
        if (v > (LLONG_MAX - (*p - '0')) / 10)
            v = LLONG_MAX;              /* as good as infinite */
        else
            v = v * 10 + (*p - '0');
    }
    if (from_start && v == 0)
        return false;
    count = v;
    return true;
}

int main(int argc, char *argv[])
{
    const char *num = NULL;
    setlocale(LC_ALL, "");

    int opt;
    while ((opt = getopt(argc, argv, "c:fn:r")) != -1) {
        switch (opt) {
        case 'c':
            opt_c = true;
            opt_n = false;
            num = optarg;
            break;
        case 'f':
            opt_f = true;
            break;
        case 'n':
            opt_n = true;
            opt_c = false;
            num = optarg;
            break;
        case 'r':
            opt_r = true;
            break;
        default:
            goto usage;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc > 1) {
        fprintf(stderr, "tail: too many operands\n");
        goto usage;
    }
    if (opt_r && (opt_c || opt_f))
        goto usage;
    if (num && !parse_number(num, !opt_r)) {
        fprintf(stderr, "tail: invalid number: '%s'\n", num);
        return 1;
    }

    FILE *f;
    bool from_stdin = argc == 0 || strcmp(argv[0], "-") == 0;
    if (from_stdin) {
        f = stdin;
    } else {
        f = fopen(argv[0], "rb");
        if (!f) {
            fprintf(stderr, "tail: %s: %s\n", argv[0], strerror(errno));
            return 1;
        }
        name = argv[0];
    }

    /* -f is ignored for standard input that is a pipe or FIFO */
    if (opt_f && from_stdin) {
        struct stat st;
        if (fstat(fileno(f), &st) == 0 && S_ISFIFO(st.st_mode))
            opt_f = false;
    }

    if (opt_r)
        reverse_lines(f);
    else if (opt_c)
        from_start ? from_start_bytes(f) : from_end_bytes(f);
    else
        from_start ? from_start_lines(f) : from_end_lines(f);

    if (fflush(stdout) == EOF || ferror(stdout)) {
        perror("tail: write error");
        return 1;
    }
    if (opt_f && !had_error)
        follow(f);

    if (f != stdin)
        fclose(f);
    return had_error ? 1 : 0;

usage:
    fprintf(stderr, "usage: tail [-f] [-c number|-n number] [file]\n"
                    "       tail -r [-n number] [file]\n");
    return 1;
}
