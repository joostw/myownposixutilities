/*
 * wc.c - wc: word, line, and byte or character count (POSIX.1-2024, XCU wc)
 *
 * Synopsis: wc [-c|-m] [-lw] [file...]
 */

#define _POSIX_C_SOURCE 200809L

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

/*
 * One pass over the input.  In a locale with multibyte characters, words
 * are made of characters (iswspace() decides), and with -m characters
 * are counted with mbrtowc(): a byte that is not part of a valid
 * character is not a character (it still belongs to a word), and
 * counting goes on.
 */
static void count_stream(FILE *f, const char *name, counts_t *c)
{
    unsigned char buf[65536];
    size_t n;
    bool in_word = false;
    bool mb = MB_CUR_MAX > 1;
    mbstate_t st;
    memset(&st, 0, sizeof st);

    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        c->bytes += n;
        if (!mb) {
            for (size_t i = 0; i < n; i++) {
                unsigned char ch = buf[i];
                if (ch == '\n') c->lines++;
                if (isspace(ch)) in_word = false;
                else if (!in_word) { c->words++; in_word = true; }
            }
            c->chars += n;
            continue;
        }
        size_t i = 0;
        while (i < n) {
            wchar_t wc;
            size_t r = mbrtowc(&wc, (const char *)buf + i, n - i, &st);
            if (r == (size_t)-2) {          /* continues in the next block */
                i = n;
                break;
            }
            if (r == (size_t)-1) {          /* not a character: one byte */
                memset(&st, 0, sizeof st);
                wc = (wchar_t)-1;
                r = 1;
            } else if (r == 0)
                r = 1;                      /* a NUL */
            i += r;
            if (wc != (wchar_t)-1)
                c->chars++;                 /* a byte that is not a character is not counted */
            if (wc == L'\n') c->lines++;
            if (wc != (wchar_t)-1 && iswspace((wint_t)wc)) in_word = false;
            else if (!in_word) { c->words++; in_word = true; }
        }
    }
    if (mb && !mbsinit(&st) && !in_word)    /* an incomplete character at the end */
        c->words++;
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
        name = path;        /* "-" as an operand is named; no operand, no name */
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
    count_stream(f, name ? name : "standard input", &c);
    if (f == stdin)
        clearerr(stdin);    /* "-" may be given again */

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

    if (fflush(stdout) == EOF || ferror(stdout)) {
        perror("wc: write error");
        return 1;
    }
    return had_error ? 1 : 0;
}
