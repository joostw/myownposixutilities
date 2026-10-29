/*
 * uniq.c - uniq: report or filter out repeated lines in a file
 *   (POSIX.1-2024, XCU uniq)
 *
 * Synopsis: uniq [-c|-d|-u] [-f fields] [-s chars] [input_file [output_file]]
 *
 * -d and -u suppress lines (not repeated, repeated); with both, nothing is
 * written.  -s counts characters of the current locale.  An output_file
 * of "-" is the standard output.
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

static void die(const char *msg)
{
    fprintf(stderr, "uniq: %s\n", msg);
    exit(1);
}

/* Return pointer to the comparison key within line:
   skip 'fields' blank-separated fields, then skip 'chars' characters. */
static const char *compare_key(const char *line, int fields, int chars)
{
    const char *p = line;

    /* skip 'fields' fields: each field is [blank]*[non-blank]* */
    for (int f = 0; f < fields; f++) {
        while (*p && isblank((unsigned char)*p)) p++;
        while (*p && !isblank((unsigned char)*p)) p++;
    }

    /* skip 'chars' characters (not bytes) */
    for (int c = 0; c < chars && *p; c++) {
        int l = mblen(p, MB_CUR_MAX);
        p += l > 0 ? l : 1;
    }

    return p;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    bool opt_c = false, opt_d = false, opt_u = false;
    int  skip_fields = 0, skip_chars = 0;

    int opt;
    while ((opt = getopt(argc, argv, "cdf:s:u")) != -1) {
        switch (opt) {
        case 'c': opt_c = true; break;
        case 'd': opt_d = true; break;
        case 'u': opt_u = true; break;
        case 'f': {
            char *end;
            skip_fields = (int)strtol(optarg, &end, 10);
            if (*end || skip_fields < 0) die("invalid field count");
            break;
        }
        case 's': {
            char *end;
            skip_chars = (int)strtol(optarg, &end, 10);
            if (*end || skip_chars < 0) die("invalid char count");
            break;
        }
        default:
            fputs("Usage: uniq [-c|-d|-u] [-f fields] [-s chars] [input [output]]\n",
                  stderr);
            return 1;
        }
    }

    /* remaining operands: [input_file [output_file]] */
    FILE *in  = stdin;
    FILE *out = stdout;
    int   nop = argc - optind;

    if (nop >= 1 && strcmp(argv[optind], "-") != 0) {
        in = fopen(argv[optind], "r");
        if (!in) { fprintf(stderr, "uniq: %s: %s\n", argv[optind], strerror(errno)); return 1; }
    }
    if (nop >= 2 && strcmp(argv[optind + 1], "-") != 0) {
        out = fopen(argv[optind + 1], "w");
        if (!out) { fprintf(stderr, "uniq: %s: %s\n", argv[optind+1], strerror(errno)); return 1; }
    }
    if (nop > 2) die("too many operands");

    char   *prev = NULL, *curr = NULL;
    size_t  prev_cap = 0, curr_cap = 0;
    ssize_t prev_n = 0, curr_n;
    long    count = 0;

    /* Helper: emit the previous line (if any) according to flags */
    /* -d suppresses lines that are not repeated, -u lines that are */
    #define EMIT() do { \
        if (prev_n > 0) { \
            bool dup = (count > 1); \
            if (!(opt_d && !dup) && !(opt_u && dup)) { \
                if (opt_c) fprintf(out, "%ld %s", count, prev); \
                else        fputs(prev, out); \
            } \
        } \
    } while (0)

    while ((curr_n = getline(&curr, &curr_cap, in)) >= 0) {
        /* Strip trailing newline for comparison, keep it in the buffer */
        size_t clen = (size_t)curr_n;
        bool has_nl = (clen > 0 && curr[clen-1] == '\n');

        if (prev_n <= 0) {
            /* first line */
            char *tmp = prev; prev = curr; curr = tmp;
            size_t ts = prev_cap; prev_cap = curr_cap; curr_cap = ts;
            prev_n = curr_n;
            count  = 1;
            continue;
        }

        /* compare keys (without trailing newline) */
        size_t plen = (size_t)prev_n;
        bool prev_nl = (plen > 0 && prev[plen-1] == '\n');

        /* build NUL-terminated strings for comparison */
        if (prev_nl) prev[plen-1] = '\0';
        if (has_nl)  curr[clen-1] = '\0';

        const char *pk = compare_key(prev, skip_fields, skip_chars);
        const char *ck = compare_key(curr, skip_fields, skip_chars);
        bool same = (strcmp(pk, ck) == 0);

        if (prev_nl) prev[plen-1] = '\n';
        if (has_nl)  curr[clen-1] = '\n';

        if (same) {
            count++;
        } else {
            EMIT();
            char *tmp = prev; prev = curr; curr = tmp;
            size_t ts = prev_cap; prev_cap = curr_cap; curr_cap = ts;
            prev_n = curr_n;
            count  = 1;
        }
    }
    EMIT();

    #undef EMIT

    free(prev); free(curr);
    if (ferror(in))  { perror("uniq"); return 1; }
    if (in  != stdin)  fclose(in);
    if (fflush(out) == EOF || ferror(out) || (out != stdout && fclose(out) == EOF)) {
        perror("uniq: write error");
        return 1;
    }
    return 0;
}
