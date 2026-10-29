/*
 * head.c - head: copy the first part of files (POSIX.1-2024, XCU head)
 *
 * Synopsis: head [-c number|-n number] [file...]
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool use_bytes = false;
static long long count = 10; /* default: 10 lines */
static int had_error;

static void head_stream(FILE *f, const char *name)
{
    char buf[8192];
    long long remaining = count;

    if (use_bytes) {
        while (remaining > 0) {
            size_t want = (remaining < (long long)sizeof buf)
                          ? (size_t)remaining : sizeof buf;
            size_t n = fread(buf, 1, want, f);
            if (n == 0) break;
            if (fwrite(buf, 1, n, stdout) != n) {
                fprintf(stderr, "head: write error\n");
                had_error = 1;
                return;
            }
            remaining -= (long long)n;
        }
    } else {
        /* Line mode: copy up to 'remaining' newlines */
        size_t n;
        while (remaining > 0 &&
               (n = fread(buf, 1, sizeof buf, f)) > 0) {
            /* Find how many complete lines fit within remaining */
            size_t write_up_to = 0;
            for (size_t i = 0; i < n && remaining > 0; i++) {
                write_up_to = i + 1;
                if (buf[i] == '\n') remaining--;
            }
            if (fwrite(buf, 1, write_up_to, stdout) != write_up_to) {
                fprintf(stderr, "head: write error\n");
                had_error = 1;
                return;
            }
        }
    }

    if (ferror(f)) {
        fprintf(stderr, "head: %s: %s\n", name, strerror(errno));
        had_error = 1;
    }
}

/* a positive decimal integer: digits only; values too large saturate */
static bool positive(const char *s, long long *v)
{
    long long n = 0;
    if (!*s)
        return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return false;
        n = n > (LLONG_MAX - (*s - '0')) / 10 ? LLONG_MAX : n * 10 + (*s - '0');
    }
    *v = n;
    return n > 0;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    int opt;
    while ((opt = getopt(argc, argv, "c:n:")) != -1) {
        switch (opt) {
        case 'c':
        case 'n':
            use_bytes = opt == 'c';
            if (!positive(optarg, &count)) {
                fprintf(stderr, "head: invalid %s count: '%s'\n",
                        use_bytes ? "byte" : "line", optarg);
                return 1;
            }
            break;
        default:
            fprintf(stderr, "usage: head [-c number|-n number] [file...]\n");
            return 1;
        }
    }
    argc -= optind;
    argv += optind;

    bool multi = (argc > 1);
    bool first = true;

    if (argc == 0) {
        head_stream(stdin, "stdin");
    } else {
        for (int i = 0; i < argc; i++) {
            FILE *f;
            const char *name;

            if (strcmp(argv[i], "-") == 0) {
                f    = stdin;
                name = "-";     /* the header names the operand */
            } else {
                f = fopen(argv[i], "rb");
                if (!f) {
                    fprintf(stderr, "head: %s: %s\n", argv[i], strerror(errno));
                    had_error = 1;
                    continue;
                }
                name = argv[i];
            }

            if (multi) {
                if (!first) printf("\n");
                printf("==> %s <==\n", name);
                first = false;
            }

            head_stream(f, name);

            if (f != stdin) fclose(f);
            else clearerr(stdin);
        }
    }

    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "head: write error\n");
        had_error = 1;
    }

    return had_error ? 1 : 0;
}
