/*
 * wc — word, line, and byte or character count  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis: wc [-c|-m] [-lw] [file...]
 */

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

static bool opt_l, opt_w, opt_c, opt_m;
static bool any_opt;
static int  had_error;

typedef struct {
    unsigned long long lines;
    unsigned long long words;
    unsigned long long bytes;
    unsigned long long chars;
} counts_t;

static void count_bytes(FILE *f, const char *name, counts_t *c)
{
    char buf[8192];
    size_t n;
    bool in_word = false;

    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        c->bytes += n;
        for (size_t i = 0; i < n; i++) {
            unsigned char ch = (unsigned char)buf[i];
            if (ch == '\n') c->lines++;
            if (isspace(ch)) {
                in_word = false;
            } else if (!in_word) {
                c->words++;
                in_word = true;
            }
        }
    }
    if (ferror(f)) {
        fprintf(stderr, "wc: %s: %s\n", name, strerror(errno));
        had_error = 1;
    }
}

static void count_chars(FILE *f, const char *name, counts_t *c)
{
    wint_t wc;
    bool in_word = false;

    while ((wc = fgetwc(f)) != WEOF) {
        c->chars++;
        if (wc == L'\n') c->lines++;
        if (iswspace(wc)) {
            in_word = false;
        } else if (!in_word) {
            c->words++;
            in_word = true;
        }
    }
    if (ferror(f)) {
        fprintf(stderr, "wc: %s: %s\n", name, strerror(errno));
        had_error = 1;
    }
}

static void print_counts(const counts_t *c, const char *name)
{
    bool first = true;

#define EMIT(val) do {                              \
    if (!first) putchar(' ');                       \
    printf("%llu", (unsigned long long)(val));      \
    first = false;                                  \
} while (0)

    if (!any_opt || opt_l) EMIT(c->lines);
    if (!any_opt || opt_w) EMIT(c->words);
    if (!any_opt || opt_c) EMIT(c->bytes);
    if (opt_m)             EMIT(c->chars);

#undef EMIT

    if (name) printf(" %s", name);
    putchar('\n');
}

static void process_file(const char *path, counts_t *total)
{
    FILE *f;
    const char *name;

    if (path == NULL || strcmp(path, "-") == 0) {
        f    = stdin;
        name = NULL;
    } else {
        f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "wc: %s: %s\n", path, strerror(errno));
            had_error = 1;
            return;
        }
        name = path;
    }

    counts_t c = {0, 0, 0, 0};
    if (opt_m)
        count_chars(f, name ? name : "stdin", &c);
    else
        count_bytes(f, name ? name : "stdin", &c);

    print_counts(&c, name);

    total->lines += c.lines;
    total->words += c.words;
    total->bytes += c.bytes;
    total->chars += c.chars;

    if (f != stdin) fclose(f);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    int opt;
    while ((opt = getopt(argc, argv, "clmw")) != -1) {
        switch (opt) {
        case 'c': opt_c = true; opt_m = false; any_opt = true; break;
        case 'm': opt_m = true; opt_c = false; any_opt = true; break;
        case 'l': opt_l = true; any_opt = true; break;
        case 'w': opt_w = true; any_opt = true; break;
        default:
            fprintf(stderr, "usage: wc [-c|-m] [-lw] [file...]\n");
            return 1;
        }
    }
    argc -= optind;
    argv += optind;

    counts_t total = {0, 0, 0, 0};

    if (argc == 0) {
        process_file(NULL, &total);
    } else {
        for (int i = 0; i < argc; i++)
            process_file(argv[i], &total);
        if (argc > 1)
            print_counts(&total, "total");
    }

    return had_error ? 1 : 0;
}
