/*
 * pr.c - pr: print files (POSIX.1-2024, XCU pr) [XSI]
 *
 * Synopsis:
 *   pr [+page] [-column] [-adfFmprt] [-e[char][gap]] [-h header]
 *      [-i[char][gap]] [-l lines] [-n[char][width]] [-o offset] [-s[char]]
 *      [-w width] [file...]
 *
 * A page is a header of five lines ("\n\n%s %s Page %d\n\n\n": the date
 * as date "+%b %e %H:%M %Y" writes it, the file or header, the page
 * number), the body, and a trailer of five empty lines; with -F (or -f) the
 * body is followed by a form feed instead of the empty lines that fill the
 * page and the trailer.  Multi-column output (-column, -m) has columns of
 * equal width, the lines cut to it; -e and -i are assumed for it.
 *
 * Choices where the standard leaves room:
 *  - The options are read by hand: grouped flags, +page and -column with
 *    any number of digits, the attached option-arguments of -e, -i, -n and
 *    -s, and -h, -l, -o and -w with their option-argument attached or as
 *    the next argument.
 *  - Column width: (width - (columns - 1)) / columns; a column is its line
 *    cut to that width and filled with spaces, then a space (or the -s
 *    character, without the filling); the last column is not filled.
 *    With -n, the number and its character come first in each column (in
 *    each line with -m).
 *  - The last page of -column output (not -a) shares its lines out among
 *    the columns as evenly as it can, the first columns taking one more;
 *    the other pages fill each column to the length of the page.
 *  - Line numbers count the lines of each file, from 1 (with +page, the
 *    lines of the pages not written too); with -m, the lines of output.
 *  - -d writes an empty line after each line of output (also with
 *    columns), and a page holds half as many lines.
 *  - The offset of -o goes before every line that is not empty, headers
 *    included.
 *  - The date is the modification time of the file, the current time for
 *    the standard input and with -m.  Another locale's names are used in
 *    the same format.
 *  - -column with -m is a usage error; so are unknown options and option-
 *    arguments that are not valid (status 2).  A file that cannot be read:
 *    a diagnostic (none with -r), the others still written, status 1.
 *    When the standard output is a terminal, diagnostics wait until the
 *    end.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

/* the options */
static long first_page = 1, columns = 1, length = 66, offset = 0, width = -1;
static bool o_a, o_d, o_F, o_m, o_p, o_r, o_t, o_f;
static bool expand_in;          /* -e */
static char in_tab = '\t';
static long in_gap = 8;
static bool tabs_out;           /* -i */
static char out_tab = '\t';
static long out_gap = 8;
static bool number;             /* -n */
static char num_char = '\t';
static long num_width = 5;
static bool o_s;
static char sep_char = '\t';
static const char *header;

static int status;
static bool defer;              /* diagnostics wait: the output is a terminal */
static char *deferred;
static size_t ndeferred;
static FILE *tty;

static void diag(const char *fmt, const char *a, const char *b)
{
    char msg[1024];
    int n = snprintf(msg, sizeof msg, "pr: ");
    n += snprintf(msg + n, sizeof msg - (size_t)n, fmt, a, b);
    snprintf(msg + n, sizeof msg - (size_t)n, "\n");
    if (!defer) {
        fputs(msg, stderr);
        return;
    }
    size_t l = strlen(msg);
    char *d = realloc(deferred, ndeferred + l + 1);
    if (!d)
        return;
    deferred = d;
    memcpy(deferred + ndeferred, msg, l + 1);
    ndeferred += l;
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "pr: out of memory\n");
        exit(1);
    }
    return p;
}

/* ---- a growing string ---- */

struct str {
    char *s;
    size_t n, cap;
};

static void sadd(struct str *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        char *ns = realloc(b->s, b->cap);
        if (!ns) {
            fprintf(stderr, "pr: out of memory\n");
            exit(1);
        }
        b->s = ns;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = '\0';
}

static void sch(struct str *b, char c)
{
    sadd(b, &c, 1);
}

/* the width of the multibyte character at s (n bytes left): its length in
   *len; a byte that is not a character is one column */
static int char_width(const char *s, size_t n, size_t *len)
{
    mbstate_t st;
    memset(&st, 0, sizeof st);
    wchar_t wc;
    size_t k = mbrtowc(&wc, s, n, &st);
    if (k == (size_t)-1 || k == (size_t)-2 || k == 0) {
        *len = 1;
        return 1;
    }
    *len = k;
    int w = wcwidth(wc);
    return w < 0 ? 0 : w;
}

/* text with its input tabs expanded (if -e), the text starting at column
   start of the output (after a line number), into b */
static void add_expanded(struct str *b, const char *t, size_t n, long start)
{
    if (!expand_in) {
        sadd(b, t, n);
        return;
    }
    long col = start;
    for (size_t i = 0; i < n;) {
        if (t[i] == in_tab) {
            /* to the next column gap*k+1 (counting from 1): col gap*k */
            long next = (col / in_gap + 1) * in_gap;
            for (; col < next; col++)
                sch(b, ' ');
            i++;
            continue;
        }
        size_t len;
        int w = char_width(t + i, n - i, &len);
        if (t[i] == '\b')
            w = col > 0 ? -1 : 0;
        sadd(b, t + i, len);
        col += w;
        i += len;
    }
}

/* the width of the number field */
static long num_field(void)
{
    return num_char == '\t' ? (num_width / 8 + 1) * 8 : num_width + 1;
}

/* ---- the input ---- */

struct src {
    FILE *f;
    const char *name;           /* for the header: "" for the standard input */
    time_t date;
    bool eof;
    long lineno;
    char *buf;
    size_t cap;
};

static bool open_src(struct src *s, const char *path)
{
    memset(s, 0, sizeof s[0]);
    if (strcmp(path, "-") == 0) {
        s->f = stdin;
        s->name = "";
        s->date = time(NULL);
        return true;
    }
    s->f = fopen(path, "r");
    if (!s->f) {
        if (!o_r)
            diag("%s: %s", path, strerror(errno));
        status = 1;
        return false;
    }
    struct stat st;
    s->name = path;
    s->date = fstat(fileno(s->f), &st) == 0 ? st.st_mtime : time(NULL);
    return true;
}

/* the next line, without its newline; NULL at the end */
static char *next_line(struct src *s, struct str *out)
{
    if (s->eof)
        return NULL;
    ssize_t n = getline(&s->buf, &s->cap, s->f);
    if (n < 0) {
        if (ferror(s->f)) {
            diag("%s: %s", s->name[0] ? s->name : "standard input", strerror(errno));
            status = 1;
        }
        s->eof = true;
        return NULL;
    }
    if (n > 0 && s->buf[n - 1] == '\n')
        n--;
    s->lineno++;
    out->n = 0;
    sadd(out, s->buf, (size_t)n);
    return out->s;
}

static void close_src(struct src *s)
{
    if (s->f && s->f != stdin)
        fclose(s->f);
    free(s->buf);
    s->f = NULL;
}

/* ---- output ---- */

static long page_no;
static bool writing;            /* the current page is at or after +page */
static bool header_on;          /* headers and trailers are written */

static void put_line(const char *s, size_t n)
{
    if (!writing)
        return;
    struct str b = { NULL, 0, 0 };
    sadd(&b, "", 0);
    if (n > 0)
        for (long i = 0; i < offset; i++)
            sch(&b, ' ');
    sadd(&b, s, n);
    if (tabs_out) {
        /* spaces that reach a tab stop become the output tab */
        struct str t = { NULL, 0, 0 };
        sadd(&t, "", 0);
        long col = 0, spaces = 0;
        for (size_t i = 0; i < b.n;) {
            if (b.s[i] == ' ') {
                spaces++;
                col++;
                if (col % out_gap == 0) {
                    if (spaces > 1)
                        sch(&t, out_tab);
                    else
                        sch(&t, ' ');
                    spaces = 0;
                }
                i++;
                continue;
            }
            for (; spaces > 0; spaces--)
                sch(&t, ' ');
            size_t len;
            int w = char_width(b.s + i, b.n - i, &len);
            if (b.s[i] == '\t')
                w = (int)(((col / 8) + 1) * 8 - col);
            sadd(&t, b.s + i, len);
            col += w;
            i += len;
        }
        for (; spaces > 0; spaces--)
            sch(&t, ' ');
        free(b.s);
        b = t;
    }
    sch(&b, '\n');
    if (fwrite(b.s, 1, b.n, stdout) != b.n) {
        fprintf(stderr, "pr: write error: %s\n", strerror(errno));
        exit(1);
    }
    free(b.s);
}

static void pause_for_tty(void)
{
    if (!isatty(1))
        return;
    fflush(stdout);
    if (!tty)
        tty = fopen("/dev/tty", "r");
    fputc('\a', stderr);
    if (tty) {
        int c;
        while ((c = getc(tty)) != EOF && c != '\n')
            ;
    }
}

static void begin_page(const char *name, time_t date)
{
    page_no++;
    writing = page_no >= first_page;
    if (!writing)
        return;
    if (o_p || (o_f && page_no == first_page))
        pause_for_tty();
    if (!header_on)
        return;
    char d[128];
    struct tm *tm = localtime(&date);
    if (!tm || strftime(d, sizeof d, "%b %e %H:%M %Y", tm) == 0)
        d[0] = '\0';
    put_line("", 0);
    put_line("", 0);
    struct str h = { NULL, 0, 0 };
    sadd(&h, "", 0);
    char pg[64];
    snprintf(pg, sizeof pg, " Page %ld", page_no);
    sadd(&h, d, strlen(d));
    sch(&h, ' ');
    const char *nm = header ? header : name;
    sadd(&h, nm, strlen(nm));
    sadd(&h, pg, strlen(pg));
    put_line(h.s, h.n);
    free(h.s);
    put_line("", 0);
    put_line("", 0);
}

/* the end of a page whose body had used lines; last: the end of the file */
static void end_page(long used, long body, bool last)
{
    if (!writing)
        return;
    if (o_F || o_f) {
        if (fputc('\f', stdout) == EOF)
            exit(1);
        return;
    }
    if (!header_on)
        return;                 /* -t: no filling */
    (void)last;
    for (long i = used; i < body; i++)
        put_line("", 0);
    for (int i = 0; i < 5; i++)
        put_line("", 0);
}

/* ---- the cells of columns ---- */

static long colw;               /* the width of a column */

/* the number field of line n; in columns, a tab after it is spaces to
   the field's width (its tab stop counted from the start of the column) */
static void add_number(struct str *b, long n)
{
    char num[64];
    snprintf(num, sizeof num, "%*ld", (int)num_width, n % 100000000000L);
    size_t l = strlen(num);
    if ((long)l > num_width)
        sadd(b, num + l - (size_t)num_width, (size_t)num_width);   /* the last digits */
    else
        sadd(b, num, l);
    if (num_char == '\t' && (columns > 1 || o_m))
        for (long i = num_width; i < num_field(); i++)
            sch(b, ' ');
    else
        sch(b, num_char);
}

/* add text to b, cut to max columns (counting from col); the columns used */
static long add_cut(struct str *b, const char *s, long col, long max)
{
    long start = col;
    for (size_t i = 0; s[i];) {
        size_t len;
        int w = char_width(s + i, strlen(s + i), &len);
        if (s[i] == '\t')
            w = (int)(((col / 8) + 1) * 8 - col);
        if (col - start + w > max)
            break;
        sadd(b, s + i, len);
        col += w;
        i += len;
    }
    return col - start;
}

/* a row of cells: each NULL (an empty column) or a line, with its number */
static void put_row(char **cells, long *nums, long ncells)
{
    struct str b = { NULL, 0, 0 };
    sadd(&b, "", 0);
    long last = ncells - 1;
    while (last >= 0 && !cells[last])
        last--;
    long col = 0;
    for (long k = 0; k <= last; k++) {
        long used = 0;
        if (cells[k] || (o_m && number && k == 0)) {
            if (number && (!o_m || k == 0)) {
                struct str nb = { NULL, 0, 0 };
                sadd(&nb, "", 0);
                add_number(&nb, nums[k]);
                used += add_cut(&b, nb.s, col, o_m ? num_field() : colw);
                free(nb.s);
            }
            if (cells[k]) {
                struct str e = { NULL, 0, 0 };
                sadd(&e, "", 0);
                add_expanded(&e, cells[k], strlen(cells[k]), used);
                used += add_cut(&b, e.s, col + used, (o_m && k == 0 && number ? colw + num_field() : colw) - used);
                free(e.s);
            }
        }
        col += used;
        if (k < last) {
            if (o_s)
                sch(&b, sep_char);
            else {
                long full = o_m && k == 0 && number ? colw + num_field() : colw;
                for (; used < full; used++, col++)
                    sch(&b, ' ');
                sch(&b, ' ');
            }
            col++;
        }
    }
    put_line(b.s, b.n);
    free(b.s);
    if (o_d)
        put_line("", 0);
}

/* ---- printing ---- */

static long body_lines(void)
{
    long body = header_on ? length - 10 : length;
    return body < 1 ? 1 : body;
}

/* one file, one column */
static void print_single(struct src *s)
{
    struct str line = { NULL, 0, 0 };
    char *l = next_line(s, &line);
    if (!l) {
        free(line.s);
        return;                 /* an empty file: nothing */
    }
    long body = body_lines();
    long per_page = o_d ? (body + 1) / 2 : body;
    while (l) {
        begin_page(s->name, s->date);
        long count = 0;
        while (l && count < per_page) {
            struct str b = { NULL, 0, 0 };
            sadd(&b, "", 0);
            if (number)
                add_number(&b, s->lineno);
            add_expanded(&b, line.s, line.n, number ? num_field() : 0);
            put_line(b.s, b.n);
            free(b.s);
            if (o_d)
                put_line("", 0);
            count++;
            l = next_line(s, &line);
        }
        long used = o_d ? count * 2 : count;
        end_page(used > body ? body : used, body, !l);
    }
    free(line.s);
}

/* one file, several columns (down, or across with -a) */
static void print_columns(struct src *s)
{
    long body = body_lines();
    long rows = o_d ? (body + 1) / 2 : body;
    long per_page = rows * columns;
    char **lines = xmalloc((size_t)per_page * sizeof *lines);
    long *nums = xmalloc((size_t)per_page * sizeof *nums);
    struct str line = { NULL, 0, 0 };
    char **cells = xmalloc((size_t)columns * sizeof *cells);
    long *cnums = xmalloc((size_t)columns * sizeof *cnums);
    bool any = false;
    for (;;) {
        long n = 0;
        while (n < per_page) {
            char *l = next_line(s, &line);
            if (!l)
                break;
            lines[n] = xmalloc(line.n + 1);
            memcpy(lines[n], line.s, line.n + 1);
            nums[n++] = s->lineno;
        }
        if (n == 0)
            break;
        any = true;
        begin_page(s->name, s->date);
        long prow;              /* the rows of this page */
        long start[1024], count[1024];
        if (o_a) {
            prow = (n + columns - 1) / columns;
        } else if (n == per_page) {
            prow = rows;
            for (long k = 0; k < columns; k++) {
                start[k] = k * rows;
                count[k] = rows;
            }
        } else {
            /* the last page: shared out, the first columns one more */
            long base = n / columns, extra = n % columns, at = 0;
            prow = base + (extra ? 1 : 0);
            for (long k = 0; k < columns; k++) {
                start[k] = at;
                count[k] = base + (k < extra ? 1 : 0);
                at += count[k];
            }
        }
        for (long r = 0; r < prow; r++) {
            for (long k = 0; k < columns; k++) {
                long idx;
                bool have;
                if (o_a) {
                    idx = r * columns + k;
                    have = idx < n;
                } else {
                    idx = start[k] + r;
                    have = r < count[k];
                }
                cells[k] = have ? lines[idx] : NULL;
                cnums[k] = have ? nums[idx] : 0;
            }
            put_row(cells, cnums, columns);
        }
        long used = o_d ? prow * 2 : prow;
        for (long i = 0; i < n; i++)
            free(lines[i]);
        bool last = n < per_page;
        end_page(used > body ? body : used, body, last);
        if (last)
            break;
    }
    (void)any;
    free(lines);
    free(nums);
    free(cells);
    free(cnums);
    free(line.s);
}

/* -m: the files side by side */
static void print_merged(struct src *srcs, long nsrc)
{
    long body = body_lines();
    long rows = o_d ? (body + 1) / 2 : body;
    struct str *line = xmalloc((size_t)nsrc * sizeof *line);
    memset(line, 0, (size_t)nsrc * sizeof *line);
    char **cells = xmalloc((size_t)nsrc * sizeof *cells);
    long *nums = xmalloc((size_t)nsrc * sizeof *nums);
    long outline = 0;
    bool more = true;
    /* the first row decides whether there is anything at all */
    for (long k = 0; k < nsrc; k++)
        cells[k] = srcs[k].f ? next_line(&srcs[k], &line[k]) : NULL;
    more = false;
    for (long k = 0; k < nsrc; k++)
        more |= cells[k] != NULL;
    while (more) {
        begin_page("", time(NULL));
        long r = 0;
        while (more && r < rows) {
            outline++;
            for (long k = 0; k < nsrc; k++)
                nums[k] = outline;
            put_row(cells, nums, nsrc);
            r++;
            for (long k = 0; k < nsrc; k++)
                cells[k] = srcs[k].f ? next_line(&srcs[k], &line[k]) : NULL;
            more = false;
            for (long k = 0; k < nsrc; k++)
                more |= cells[k] != NULL;
        }
        long used = o_d ? r * 2 : r;
        end_page(used > body ? body : used, body, !more);
    }
    for (long k = 0; k < nsrc; k++)
        free(line[k].s);
    free(line);
    free(cells);
    free(nums);
}

/* ---- options ---- */

static void usage(void)
{
    fprintf(stderr, "usage: pr [+page] [-column] [-adfFmprt] [-e[char][gap]] [-h header]\n"
                    "          [-i[char][gap]] [-l lines] [-n[char][width]] [-o offset]\n"
                    "          [-s[char]] [-w width] [file...]\n");
    exit(2);
}

static long posnum(const char *s, const char *what, bool zero_ok)
{
    char *end;
    if (!isdigit((unsigned char)*s))
        goto bad;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (*end || errno || v > 100000000 || (v == 0 && !zero_ok))
        goto bad;
    return v;
bad:
    fprintf(stderr, "pr: %s %s: not a valid number\n", what, s);
    exit(2);
}

/* [char][number]: an optional non-digit, then digits */
static const char *char_num(const char *p, char *c, long *num, bool *have_num)
{
    *have_num = false;
    if (*p && !isdigit((unsigned char)*p))
        *c = *p++;
    if (isdigit((unsigned char)*p)) {
        errno = 0;
        char *end;
        *num = strtol(p, &end, 10);
        if (errno || *num > 1000000)
            usage();
        *have_num = true;
        p = end;
    }
    return p;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    defer = isatty(1);
    int i = 1;
    bool have_cols = false;
    for (; i < argc; i++) {
        char *a = argv[i];
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (a[0] == '+' && isdigit((unsigned char)a[1])) {
            first_page = posnum(a + 1, "+page", false);
            continue;
        }
        if (a[0] != '-' || !a[1])
            break;
        for (char *p = a + 1; *p;) {
            char c = *p++;
            if (isdigit((unsigned char)c)) {
                char *end;
                columns = strtol(p - 1, &end, 10);
                if (columns < 1 || columns > 1000) {
                    fprintf(stderr, "pr: -%ld: not a valid number of columns\n", columns);
                    return 2;
                }
                have_cols = columns > 1 || have_cols;
                p = end;
                continue;
            }
            bool hn;
            long v;
            switch (c) {
            case 'a': o_a = true; break;
            case 'd': o_d = true; break;
            case 'f': o_f = true; break;
            case 'F': o_F = true; break;
            case 'm': o_m = true; break;
            case 'p': o_p = true; break;
            case 'r': o_r = true; break;
            case 't': o_t = true; break;
            case 'e':
                expand_in = true;
                p = (char *)char_num(p, &in_tab, &v, &hn);
                if (hn)
                    in_gap = v ? v : 8;
                break;
            case 'i':
                tabs_out = true;
                p = (char *)char_num(p, &out_tab, &v, &hn);
                if (hn)
                    out_gap = v ? v : 8;
                break;
            case 'n':
                number = true;
                p = (char *)char_num(p, &num_char, &v, &hn);
                if (hn) {
                    if (v < 1)
                        usage();
                    num_width = v;
                }
                break;
            case 's':
                o_s = true;
                if (*p)
                    sep_char = *p++;
                break;
            case 'h': case 'l': case 'o': case 'w': {
                const char *arg = *p ? p : (i + 1 < argc ? argv[++i] : NULL);
                if (!arg)
                    usage();
                if (c == 'h')
                    header = arg;
                else if (c == 'l')
                    length = posnum(arg, "-l", false);
                else if (c == 'o')
                    offset = posnum(arg, "-o", true);
                else
                    width = posnum(arg, "-w", false);
                p += strlen(p);
                break;
            }
            default:
                fprintf(stderr, "pr: -%c: unknown option\n", c);
                usage();
            }
        }
    }
    if (have_cols && o_m) {
        fprintf(stderr, "pr: -column and -m cannot be used together\n");
        return 2;
    }
    header_on = !o_t && length > 10;
    long ncol = o_m ? argc - i : columns;
    if (ncol < 1)
        ncol = 1;
    if (width < 0)
        width = o_s ? 512 : 72;
    if (columns > 1 || o_m) {
        expand_in = true;
        if (!o_s)
            tabs_out = true;
        colw = (width - (ncol - 1) - (o_m && number ? num_field() : 0)) / ncol;
        if (colw < 1) {
            fprintf(stderr, "pr: the width %ld is too small for %ld columns\n", width, ncol);
            return 2;
        }
    }

    if (o_m) {
        long nsrc = argc - i;
        if (nsrc == 0)
            nsrc = 1;
        struct src *srcs = xmalloc((size_t)nsrc * sizeof *srcs);
        for (long k = 0; k < nsrc; k++)
            if (!open_src(&srcs[k], argc - i ? argv[i + k] : "-"))
                srcs[k].f = NULL;
        print_merged(srcs, nsrc);
        for (long k = 0; k < nsrc; k++)
            close_src(&srcs[k]);
    } else {
        int nfiles = argc - i;
        for (int k = 0; k < (nfiles ? nfiles : 1); k++) {
            struct src s;
            if (!open_src(&s, nfiles ? argv[i + k] : "-"))
                continue;
            page_no = 0;
            if (columns > 1)
                print_columns(&s);
            else
                print_single(&s);
            close_src(&s);
        }
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "pr: write error: %s\n", strerror(errno));
        return 1;
    }
    if (ndeferred)
        fputs(deferred, stderr);
    return status;
}
