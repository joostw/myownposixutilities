#define _POSIX_C_SOURCE 200809L

/*
 * grep — search files for a pattern  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis (from the standard):
 *   grep [-E|-F] [-c|-l|-q] [-insvx] -e pattern_list
 *        [-e pattern_list]... [-f pattern_file]... [file...]
 *   grep [-E|-F] [-c|-l|-q] [-insvx] [-e pattern_list]...
 *        -f pattern_file [-f pattern_file]... [file...]
 *   grep [-E|-F] [-c|-l|-q] [-insvx] pattern_list [file...]
 *
 * Options:
 *   -E  Extended Regular Expressions (ERE)
 *   -F  Fixed strings
 *   -c  Write count of selected lines only
 *   -e  Specify pattern(s) on the command line
 *   -f  Read pattern(s) from file
 *   -i  Case-insensitive matching
 *   -l  Write only names of files containing matches
 *   -n  Prefix each output line with its line number
 *   -q  Quiet: no output; exit 0 if any line selected
 *   -s  Suppress errors for nonexistent / unreadable files
 *   -v  Invert the sense of matching
 *   -x  Whole-line match (entire line must match)
 *
 * Exit status: 0 = match found, 1 = no match, >1 = error.
 */

#include "posix_re.h"

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Name reported when searching standard input (POSIX locale string). */
#define STDIN_NAME "(standard input)"

typedef enum { OUT_NORMAL, OUT_COUNT, OUT_FILES, OUT_QUIET } out_mode_t;

/* ------------------------------------------------------------------ */
/*  Pattern-spec accumulator                                            */
/*                                                                      */
/*  We collect -e / -f arguments in a linked list during option parsing */
/*  so that -E / -F / -i can appear anywhere among the options (as     */
/*  XBD 12.2 Utility Syntax Guidelines permit) without us needing to   */
/*  compile patterns before we know the mode.                          */
/* ------------------------------------------------------------------ */

typedef struct pat_spec {
    const char      *str;
    bool             is_file;
    struct pat_spec *next;
} pat_spec_t;

static pat_spec_t *specs_head;
static pat_spec_t *specs_tail;
static int         n_specs;

static int add_spec(const char *str, bool is_file)
{
    pat_spec_t *s = malloc(sizeof *s);
    if (!s) { perror("grep"); return -1; }
    s->str     = str;
    s->is_file = is_file;
    s->next    = NULL;
    if (specs_tail) specs_tail->next = s; else specs_head = s;
    specs_tail = s;
    n_specs++;
    return 0;
}

static void free_specs(void)
{
    for (pat_spec_t *s = specs_head; s; ) {
        pat_spec_t *next = s->next;
        free(s);
        s = next;
    }
}

/* ------------------------------------------------------------------ */
/*  File processing                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    bool found;  /* at least one line was selected */
    bool error;  /* I/O error reading the file */
} result_t;

/*
 * grep_file — apply the pattern set to every line of fp.
 *
 * name        : displayed name (path or STDIN_NAME)
 * prefix_name : prefix each output line (or count) with "name:"
 */
static result_t grep_file(const char *name, FILE *fp,
                           pset_t *ps, bool invert, bool whole,
                           bool show_num, out_mode_t outmode,
                           bool prefix_name, bool suppress)
{
    result_t  res     = {false, false};
    char     *linebuf = NULL;
    size_t    cap     = 0;
    ssize_t   nread;
    long      linenum = 0;
    long      count   = 0;

    (void)suppress;   /* file-open errors already handled by caller */

    while ((nread = getline(&linebuf, &cap, fp)) >= 0) {
        linenum++;

        /* len is the matchable length: excludes the trailing '\n'. */
        size_t len = (size_t)nread;
        if (len > 0 && linebuf[len - 1] == '\n') len--;

        /*
         * NUL-terminate at len so that regex/strstr see only the
         * line content without the '\n'.  Save and restore so that
         * fwrite can still write the original line (including '\n').
         */
        char saved   = linebuf[len];
        linebuf[len] = '\0';
        bool selected = pset_match(ps, linebuf, len, whole);
        linebuf[len] = saved;

        if (invert) selected = !selected;
        if (!selected) continue;

        res.found = true;
        count++;

        switch (outmode) {
        case OUT_QUIET:
            /* No output; stop immediately. */
            goto done;

        case OUT_FILES:
        case OUT_COUNT:
            /* Accumulate; output after the loop. */
            break;

        case OUT_NORMAL:
            if (prefix_name) printf("%s:", name);
            if (show_num)    printf("%ld:", linenum);
            /* Write the line exactly as read (including '\n' if present). */
            fwrite(linebuf, 1, (size_t)nread, stdout);
            break;
        }
    }

done:
    if (ferror(fp)) {
        fprintf(stderr, "grep: %s: %s\n", name, strerror(errno));
        res.error = true;
    }

    if (outmode == OUT_FILES && res.found)
        printf("%s\n", name);

    if (outmode == OUT_COUNT) {
        if (prefix_name) printf("%s:", name);
        printf("%ld\n", count);
    }

    free(linebuf);
    return res;
}

/* ------------------------------------------------------------------ */
/*  Usage                                                               */
/* ------------------------------------------------------------------ */

static void usage(void)
{
    fprintf(stderr,
        "usage: grep [-EF] [-c|-l|-q] [-insvx] -e pattern [-e pattern]..."
        " [-f file]... [file...]\n"
        "       grep [-EF] [-c|-l|-q] [-insvx] [-e pattern]..."
        " -f file [-f file]... [file...]\n"
        "       grep [-EF] [-c|-l|-q] [-insvx] pattern [file...]\n");
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    pat_mode_t patmode  = PAT_BRE;
    out_mode_t outmode  = OUT_NORMAL;
    bool       icase    = false;
    bool       invert   = false;
    bool       whole    = false;
    bool       show_num = false;
    bool       suppress = false;

    int opt;
    while ((opt = getopt(argc, argv, "EFce:f:ilnqsvx")) != -1) {
        switch (opt) {
        case 'E': patmode = PAT_ERE;   break;
        case 'F': patmode = PAT_FIXED; break;
        case 'c': outmode = OUT_COUNT; break;
        case 'l': outmode = OUT_FILES; break;
        case 'q': outmode = OUT_QUIET; break;
        case 'i': icase    = true;     break;
        case 'n': show_num = true;     break;
        case 's': suppress = true;     break;
        case 'v': invert   = true;     break;
        case 'x': whole    = true;     break;
        case 'e':
            if (add_spec(optarg, false) != 0) return 2;
            break;
        case 'f':
            if (add_spec(optarg, true) != 0) return 2;
            break;
        default:
            usage();
            return 2;
        }
    }

    /* Build the pattern set now that we know mode and flags. */
    pset_t *ps = pset_new(patmode, icase);
    if (!ps) { perror("grep"); return 2; }

    if (n_specs == 0) {
        /* No -e / -f: first operand is the pattern. */
        if (optind >= argc) {
            usage();
            pset_free(ps);
            return 2;
        }
        if (pset_add_str(ps, argv[optind++], "pattern") != 0) {
            pset_free(ps);
            return 2;
        }
    } else {
        for (pat_spec_t *s = specs_head; s; s = s->next) {
            int rc = s->is_file ? pset_add_file(ps, s->str)
                                : pset_add_str(ps, s->str, "-e argument");
            if (rc != 0) {
                free_specs();
                pset_free(ps);
                return 2;
            }
        }
        free_specs();
    }

    /* Remaining operands are files to search. */
    int  n_files     = argc - optind;
    bool prefix_name = n_files > 1;   /* SUSv5: prefix when >1 file argument */
    bool found_match = false;
    bool had_error   = false;

    if (n_files == 0) {
        /* No file operands: search standard input. */
        result_t r = grep_file(STDIN_NAME, stdin, ps,
                               invert, whole, show_num, outmode,
                               false, suppress);
        found_match = r.found;
        had_error   = r.error;
    } else {
        for (int i = optind; i < argc; i++) {
            const char *path = argv[i];
            FILE       *fp;

            if (strcmp(path, "-") == 0) {
                fp   = stdin;
                path = STDIN_NAME;
            } else {
                fp = fopen(path, "r");
                if (!fp) {
                    if (!suppress)
                        fprintf(stderr, "grep: %s: %s\n",
                                path, strerror(errno));
                    had_error = true;
                    continue;
                }
            }

            result_t r = grep_file(path, fp, ps,
                                   invert, whole, show_num, outmode,
                                   prefix_name, suppress);
            if (fp != stdin) fclose(fp);

            found_match |= r.found;
            had_error   |= r.error;

            /* -q: exit as soon as the first match is found. */
            if (found_match && outmode == OUT_QUIET) break;
        }
    }

    pset_free(ps);

    /* Flush stdout and check for write errors. */
    if (fflush(stdout) != 0 || ferror(stdout)) {
        perror("grep");
        had_error = true;
    }

    /* Exit status per SUSv5:
     *   0  one or more lines selected (and output written without error)
     *   1  no lines selected
     *  >1  an error occurred
     * Special case: -q exits 0 on a match even if errors were detected. */
    if (outmode == OUT_QUIET) return found_match ? 0 : (had_error ? 2 : 1);
    if (had_error) return 2;
    return found_match ? 0 : 1;
}
