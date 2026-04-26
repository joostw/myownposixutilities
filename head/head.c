/*
 * head — copy the first part of files  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis: head [-c number|-n number] [file...]
 */

#include <errno.h>
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

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    int opt;
    while ((opt = getopt(argc, argv, "c:n:")) != -1) {
        switch (opt) {
        case 'c': {
            char *end;
            use_bytes = true;
            count = strtoll(optarg, &end, 10);
            if (*end != '\0' || count <= 0) {
                fprintf(stderr, "head: invalid byte count: '%s'\n", optarg);
                return 1;
            }
            break;
        }
        case 'n': {
            char *end;
            use_bytes = false;
            count = strtoll(optarg, &end, 10);
            if (*end != '\0' || count <= 0) {
                fprintf(stderr, "head: invalid line count: '%s'\n", optarg);
                return 1;
            }
            break;
        }
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
                name = "standard input";
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
        }
    }

    if (fflush(stdout) != 0) {
        fprintf(stderr, "head: write error\n");
        had_error = 1;
    }

    return had_error ? 1 : 0;
}
