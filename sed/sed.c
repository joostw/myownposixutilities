/*
 * sed.c - sed: stream editor (POSIX.1-2024, XCU sed)
 *
 * Synopsis: sed [-En] script [file...]
 *           sed [-En] -e script [-e script]... [-f script_file]... [file...]
 *           sed [-En] [-e script]... -f script_file [-f script_file]... [file...]
 *
 * Notes on the standard's choices, as implemented:
 *   - ^ and $ in an RE match only at the ends of the pattern space, and
 *     \n matches an embedded <newline>; '.' matches it too.
 *   - the delimiter does not end an RE inside a bracket expression; an
 *     escaped delimiter is the literal character (kept escaped when it is
 *     special in the RE, which is the literal character as well).
 *   - an empty RE is the last RE used at run time (by an address or s).
 *   - s///g does not match an empty string right after a previous match;
 *     with both g and a number n, the nth and all later ones are replaced.
 *   - the t flag is cleared whenever an input line is read (n, N, and a
 *     new cycle) and when t is executed.
 *   - N with no next line quits without writing the pattern space; n and q
 *     write it (unless -n) as the end of the script does.
 *   - l writes non-printable characters (<newline> included, since the
 *     standard says \n does not apply) as octal bytes, and printable
 *     multibyte characters as they are; it folds output lines at 70
 *     columns.
 *   - y and l work on the characters of the locale.
 *   - an input file that cannot be read is reported and skipped; the exit
 *     status is then 2.  Write errors give status 4.
 *   - commands that may be followed by ';' must be followed by ';', a
 *     <newline>, '}', '#' or the end of the script.
 *   - extensions: "a text", "i text" and "c text" on one line; labels end
 *     at ';' as well as <newline>; the w file /dev/stdout is the standard
 *     output.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

/* ---- dynamic strings ---- */

typedef struct { char *buf; size_t len, cap; } dstr_t;

static void oom(void)
{
    perror("sed");
    exit(4);
}

static void dstr_init(dstr_t *d)
{
    d->cap = 256;
    d->buf = malloc(d->cap);
    if (!d->buf)
        oom();
    d->len = 0;
    d->buf[0] = '\0';
}

static void dstr_ensure(dstr_t *d, size_t need)
{
    if (need < d->cap)
        return;
    size_t nc = d->cap * 2;
    while (nc <= need)
        nc *= 2;
    char *nb = realloc(d->buf, nc);
    if (!nb)
        oom();
    d->buf = nb;
    d->cap = nc;
}

static void dstr_set(dstr_t *d, const char *s, size_t n)
{
    dstr_ensure(d, n + 1);
    memmove(d->buf, s, n);
    d->buf[n] = '\0';
    d->len = n;
}

static void dstr_append(dstr_t *d, const char *s, size_t n)
{
    dstr_ensure(d, d->len + n + 1);
    memcpy(d->buf + d->len, s, n);
    d->len += n;
    d->buf[d->len] = '\0';
}

static void dstr_addc(dstr_t *d, char c) { dstr_append(d, &c, 1); }

static void dstr_free(dstr_t *d) { free(d->buf); d->buf = NULL; }

static char *xstrdup(const char *s)
{
    char *t = strdup(s);
    if (!t)
        oom();
    return t;
}

static void *xcalloc(size_t n, size_t s)
{
    void *p = calloc(n, s);
    if (!p)
        oom();
    return p;
}

/* ---- the script ---- */

typedef enum { ADDR_NONE, ADDR_LINE, ADDR_LAST, ADDR_RE } addr_type_t;

typedef struct {
    addr_type_t type;
    long        lineno;
    regex_t    *re;         /* NULL: the empty RE */
} addr_t;

typedef enum {
    CMD_LBRACE, CMD_RBRACE,
    CMD_a, CMD_b, CMD_c, CMD_d, CMD_D,
    CMD_g, CMD_G, CMD_h, CMD_H,
    CMD_i, CMD_l, CMD_n, CMD_N,
    CMD_p, CMD_P, CMD_q,
    CMD_r, CMD_s, CMD_t, CMD_w,
    CMD_x, CMD_y, CMD_LABEL, CMD_EQUALS
} cmd_type_t;

/* a character for y: its bytes (a multibyte character or one byte) */
typedef struct { char b[MB_LEN_MAX]; unsigned char n; } ychar_t;

typedef struct cmd {
    addr_t      addr[2];
    int         naddr;
    bool        negated;
    cmd_type_t  type;

    char       *text;       /* a, c, i */
    char       *label;      /* b, t, : */
    char       *rfile;      /* r */
    FILE       *wfp;        /* w, and the s w flag */

    /* s */
    regex_t    *s_re;       /* NULL: the empty RE */
    char       *s_repl;
    size_t      s_repl_len;
    long        s_nth;      /* the occurrence (1 if not given) */
    bool        s_global;
    bool        s_print;

    /* y */
    ychar_t    *y_from, *y_to;
    size_t      y_len;

    bool        in_range;

    struct cmd *target;     /* b, t */
    struct cmd *end_brace;  /* { */
    struct cmd *next;
} cmd_t;

static cmd_t *script, *script_tail;
static bool   opt_n, opt_E;
static int    status;

static long   line_number;
static dstr_t pattern_space, hold_space;
static bool   sub_made;
static regex_t *last_re;    /* the last RE used */

static void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
#include <stdarg.h>
static void die(const char *fmt, ...)
{
    va_list ap;
    fputs("sed: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

/* ---- output ---- */

static void out_bytes(FILE *f, const char *s, size_t n)
{
    if (n && fwrite(s, 1, n, f) != n) {
        perror("sed: write error");
        exit(4);
    }
}

static void out_line(FILE *f, const char *s, size_t n)
{
    out_bytes(f, s, n);
    if (putc('\n', f) == EOF) {
        perror("sed: write error");
        exit(4);
    }
}

static void out_ps(FILE *f) { out_line(f, pattern_space.buf, pattern_space.len); }

/* ---- w files: each created before processing begins ---- */

typedef struct wf { char *path; FILE *fp; struct wf *next; } wf_t;
static wf_t *wfiles;

static FILE *wfile_get(const char *path)
{
    if (strcmp(path, "/dev/stdout") == 0)
        return stdout;
    for (wf_t *w = wfiles; w; w = w->next)
        if (strcmp(w->path, path) == 0)
            return w->fp;
    wf_t *w = xcalloc(1, sizeof *w);
    w->path = xstrdup(path);
    w->fp = fopen(path, "w");
    if (!w->fp)
        die("%s: %s", path, strerror(errno));
    w->next = wfiles;
    wfiles = w;
    return w->fp;
}

/* ---- the append queue (a and r) ---- */

typedef struct aitem { bool is_file; char *text; struct aitem *next; } aitem_t;
static aitem_t *aq_head, *aq_tail;

static void aq_add(bool is_file, char *s)
{
    aitem_t *it = xcalloc(1, sizeof *it);
    it->is_file = is_file;
    it->text = s;           /* owned by the command */
    if (aq_tail)
        aq_tail->next = it;
    else
        aq_head = it;
    aq_tail = it;
}

static void aq_flush(void)
{
    while (aq_head) {
        aitem_t *it = aq_head;
        aq_head = it->next;
        if (!aq_head)
            aq_tail = NULL;
        if (it->is_file) {
            /* as of now; a file that cannot be read is an empty one */
            FILE *fp = fopen(it->text, "r");
            if (fp) {
                char buf[8192];
                size_t n;
                while ((n = fread(buf, 1, sizeof buf, fp)) > 0)
                    out_bytes(stdout, buf, n);
                fclose(fp);
            }
        } else {
            out_line(stdout, it->text, strlen(it->text));
        }
        free(it);
    }
}

/* ---- regular expressions ---- */

static regex_t *compile_re(const char *src, int extra)
{
    if (!*src)
        return NULL;            /* empty: the last RE used */
    regex_t *re = xcalloc(1, sizeof *re);
    int rc = regcomp(re, src, (opt_E ? REG_EXTENDED : 0) | extra);
    if (rc != 0) {
        char buf[256];
        regerror(rc, re, buf, sizeof buf);
        die("%s: %s", src, buf);
    }
    return re;
}

static regex_t *use_re(regex_t *re)
{
    if (!re) {
        if (!last_re)
            die("no previous regular expression");
        re = last_re;
    }
    last_re = re;
    return re;
}

/* match in s[0..len) from start; offsets in m are from s */
static bool re_match(regex_t *re, const char *s, size_t len, size_t start,
                     size_t nm, regmatch_t *m)
{
#ifdef REG_STARTEND
    m[0].rm_so = (regoff_t)start;
    m[0].rm_eo = (regoff_t)len;
    return regexec(re, s, nm, m, REG_STARTEND) == 0;
#else
    /* no REG_STARTEND: a pattern space with a NUL is cut there */
    if (regexec(re, s + start, nm, m, start ? REG_NOTBOL : 0) != 0)
        return false;
    for (size_t i = 0; i < nm; i++)
        if (m[i].rm_so >= 0) {
            m[i].rm_so += (regoff_t)start;
            m[i].rm_eo += (regoff_t)start;
        }
    return true;
#endif
}

/* ---- parsing ---- */

static const char *p;           /* the parse cursor */

static void skip_blanks(void) { while (*p == ' ' || *p == '\t') p++; }

static bool is_special(char c)
{
    return strchr(opt_E ? ".[\\()*+?{|^$" : ".[\\*^$", c) != NULL;
}

/* after a command: only ';', <newline>, '}', '#' or the end */
static void end_command(char verb)
{
    skip_blanks();
    if (*p == ';' || *p == '\n')
        p++;
    else if (*p && *p != '}' && *p != '#')
        die("extra characters after command '%c'", verb);
}

/*
 * The RE after its opening delimiter, up to the closing one: bracket
 * expressions copied as they are, \n made a <newline>, the escaped
 * delimiter made the character.
 */
static void scan_re(char delim, dstr_t *d)
{
    for (;;) {
        if (!*p || *p == '\n')
            die("unterminated regular expression");
        if (*p == delim) {
            p++;
            return;
        }
        if (*p == '[') {
            const char *q = p + 1;
            if (*q == '^')
                q++;
            if (*q == ']')
                q++;
            while (*q && *q != ']' && *q != '\n') {
                if (q[0] == '[' && (q[1] == ':' || q[1] == '=' || q[1] == '.')) {
                    char t = q[1];
                    q += 2;
                    while (*q && *q != '\n' && !(q[0] == t && q[1] == ']'))
                        q++;
                    if (*q == t)
                        q += 2;
                } else {
                    q++;
                }
            }
            if (*q != ']')
                die("unterminated bracket expression");
            dstr_append(d, p, (size_t)(q + 1 - p));
            p = q + 1;
            continue;
        }
        if (*p == '\\') {
            char c = p[1];
            if (!c || c == '\n')
                die("unterminated regular expression");
            if (c == delim) {
                if (is_special(c))
                    dstr_addc(d, '\\');
                dstr_addc(d, c);
            } else if (c == 'n') {
                dstr_addc(d, '\n');
            } else {
                dstr_append(d, p, 2);
            }
            p += 2;
            continue;
        }
        dstr_addc(d, *p++);
    }
}

static bool parse_addr(addr_t *a)
{
    memset(a, 0, sizeof *a);
    if (*p == '$') {
        a->type = ADDR_LAST;
        p++;
        return true;
    }
    if (isdigit((unsigned char)*p)) {
        a->type = ADDR_LINE;
        while (isdigit((unsigned char)*p)) {
            if (a->lineno > (LONG_MAX - 9) / 10)
                die("line number too large");
            a->lineno = a->lineno * 10 + (*p++ - '0');
        }
        if (a->lineno == 0)
            die("invalid line address 0");
        return true;
    }
    char delim;
    if (*p == '/') {
        delim = '/';
        p++;
    } else if (*p == '\\' && p[1] && p[1] != '\n' && p[1] != '\\') {
        delim = p[1];
        p += 2;
    } else {
        return false;
    }
    dstr_t raw;
    dstr_init(&raw);
    scan_re(delim, &raw);
    a->type = ADDR_RE;
    a->re = compile_re(raw.buf, 0);
    dstr_free(&raw);
    return true;
}

/*
 * The text of a, c and i: after "a\" and its <newline> (or, as an
 * extension, on the same line), lines ending in an escaped <newline>;
 * a backslash before any other character is removed.
 */
static char *parse_text(void)
{
    dstr_t t;
    dstr_init(&t);
    skip_blanks();
    if (*p == '\\') {
        p++;
        if (*p == '\n')
            p++;
        else
            skip_blanks();
    }
    while (*p && *p != '\n') {
        if (*p == '\\') {
            if (p[1] == '\0') {
                p++;
                break;
            }
            dstr_addc(&t, p[1]);        /* <newline>, <backslash> or other */
            p += 2;
            continue;
        }
        dstr_addc(&t, *p++);
    }
    if (*p == '\n')
        p++;
    char *s = xstrdup(t.buf);
    dstr_free(&t);
    return s;
}

static char *parse_label(bool brace_ends)
{
    skip_blanks();
    const char *start = p;
    while (*p && *p != ';' && *p != '\n' && !(brace_ends && *p == '}'))
        p++;
    const char *end = p;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    if (*p == ';' || *p == '\n')
        p++;
    char *l = xcalloc((size_t)(end - start) + 1, 1);
    memcpy(l, start, (size_t)(end - start));
    return l;
}

/* r and w: the file name ends the command */
static char *parse_filename(void)
{
    skip_blanks();
    const char *start = p;
    while (*p && *p != '\n')
        p++;
    char *f = xcalloc((size_t)(p - start) + 1, 1);
    memcpy(f, start, (size_t)(p - start));
    if (*p == '\n')
        p++;
    if (!*f)
        die("missing file name");
    return f;
}

static void parse_s(cmd_t *c)
{
    char delim = *p;
    if (!delim || delim == '\n' || delim == '\\')
        die("s: invalid delimiter");
    p++;

    dstr_t re_src;
    dstr_init(&re_src);
    scan_re(delim, &re_src);

    /*
     * The replacement, with its escapes kept for do_subst(): "\&",
     * "\\", "\<digit>" and an escaped <newline>; the escaped delimiter
     * becomes the character (escaped still if it is '&', literal).
     */
    dstr_t repl;
    dstr_init(&repl);
    for (;;) {
        if (!*p)
            die("s: unterminated replacement");
        if (*p == '\n')
            die("s: unterminated replacement (use \\ before a <newline>)");
        if (*p == delim) {
            p++;
            break;
        }
        if (*p == '\\' && p[1]) {
            if (p[1] == delim && delim != '&' && delim != '\\' && !isdigit((unsigned char)delim)) {
                dstr_addc(&repl, delim);
            } else if (p[1] == '\n') {
                dstr_addc(&repl, '\n');
            } else {
                dstr_append(&repl, p, 2);
            }
            p += 2;
            continue;
        }
        dstr_addc(&repl, *p++);
    }

    c->s_nth = 1;
    bool have_nth = false, icase = false;
    for (;;) {
        char f = *p;
        if (f == 'g') {
            if (c->s_global)
                die("s: multiple g flags");
            c->s_global = true;
            p++;
        } else if (f == 'p') {
            if (c->s_print)
                die("s: multiple p flags");
            c->s_print = true;
            p++;
        } else if (f == 'i' || f == 'I') {
            icase = true;
            p++;
        } else if (isdigit((unsigned char)f)) {
            if (have_nth)
                die("s: multiple number flags");
            long n = 0;
            while (isdigit((unsigned char)*p)) {
                if (n > (LONG_MAX - 9) / 10)
                    die("s: number too large");
                n = n * 10 + (*p++ - '0');
            }
            if (n == 0)
                die("s: the number flag must not be zero");
            c->s_nth = n;
            have_nth = true;
        } else if (f == 'w') {
            p++;
            char *file = parse_filename();
            c->wfp = wfile_get(file);
            free(file);
            break;
        } else {
            end_command('s');
            break;
        }
    }

    c->s_re = compile_re(re_src.buf, icase ? REG_ICASE : 0);
    if (c->s_re) {
        /* a back-reference beyond the RE's subexpressions is an error */
        for (size_t i = 0; i + 1 < repl.len; i++)
            if (repl.buf[i] == '\\') {
                if (isdigit((unsigned char)repl.buf[i + 1]) &&
                    (size_t)(repl.buf[i + 1] - '0') > c->s_re->re_nsub)
                    die("s: invalid reference \\%c in the replacement", repl.buf[i + 1]);
                i++;
            }
    }
    c->s_repl = malloc(repl.len + 1);
    if (!c->s_repl)
        oom();
    memcpy(c->s_repl, repl.buf, repl.len + 1);
    c->s_repl_len = repl.len;
    dstr_free(&re_src);
    dstr_free(&repl);
}

/* the length of the character at s (n bytes left): 1 for a byte that is not one */
static size_t charlen(const char *s, size_t n)
{
    if (n == 0)
        return 0;
    if (MB_CUR_MAX == 1)
        return 1;
    mbstate_t st;
    memset(&st, 0, sizeof st);
    size_t k = mbrtowc(NULL, s, n, &st);
    return k == (size_t)-1 || k == (size_t)-2 || k == 0 ? 1 : k;
}

static void parse_y(cmd_t *c)
{
    char delim = *p;
    if (!delim || delim == '\n' || delim == '\\')
        die("y: invalid delimiter");
    p++;
    ychar_t *s[2] = { NULL, NULL };
    size_t n[2] = { 0, 0 };
    for (int part = 0; part < 2; part++) {
        size_t cap = 0;
        for (;;) {
            if (!*p || *p == '\n')
                die("y: unterminated string");
            if (*p == delim) {
                p++;
                break;
            }
            ychar_t y;
            memset(&y, 0, sizeof y);
            if (*p == '\\' && p[1] == 'n' && delim != 'n') {
                y.b[0] = '\n';
                y.n = 1;
                p += 2;
            } else if (*p == '\\' && (p[1] == '\\' || p[1] == delim)) {
                y.b[0] = p[1];
                y.n = 1;
                p += 2;
            } else if (*p == '\\' && p[1] == 'n') {
                /* the delimiter is 'n': \n is an 'n' */
                y.b[0] = 'n';
                y.n = 1;
                p += 2;
            } else if (*p == '\\') {
                die("y: unknown escape \\%c", p[1] ? p[1] : ' ');
            } else {
                size_t k = charlen(p, strlen(p));
                memcpy(y.b, p, k);
                y.n = (unsigned char)k;
                p += k;
            }
            if (n[part] == cap) {
                cap = cap ? cap * 2 : 16;
                s[part] = realloc(s[part], cap * sizeof *s[part]);
                if (!s[part])
                    oom();
            }
            s[part][n[part]++] = y;
        }
    }
    if (n[0] != n[1])
        die("y: strings of different lengths");
    for (size_t i = 0; i < n[0]; i++)
        for (size_t j = i + 1; j < n[0]; j++)
            if (s[0][i].n == s[0][j].n && memcmp(s[0][i].b, s[0][j].b, s[0][i].n) == 0)
                die("y: a character appears twice in string1");
    c->y_from = s[0];
    c->y_to = s[1];
    c->y_len = n[0];
    end_command('y');
}

static cmd_t *new_cmd(void) { return xcalloc(1, sizeof(cmd_t)); }

static void append_cmd(cmd_t *c)
{
    if (script_tail)
        script_tail->next = c;
    else
        script = c;
    script_tail = c;
}

static void parse_commands(int depth);

static void parse_one_command(int depth)
{
    if (*p == '#') {
        while (*p && *p != '\n')
            p++;
        return;
    }

    cmd_t *c = new_cmd();
    if (parse_addr(&c->addr[0])) {
        c->naddr = 1;
        skip_blanks();
        if (*p == ',') {
            p++;
            skip_blanks();
            if (!parse_addr(&c->addr[1]))
                die("expected an address after ','");
            c->naddr = 2;
        }
    }
    skip_blanks();
    if (*p == '!') {
        c->negated = true;
        p++;
        skip_blanks();
        if (*p == '!')
            die("multiple '!'");
    }

    char verb = *p;
    if (!verb || verb == '\n' || verb == ';') {
        if (c->naddr)
            die("missing command");
        free(c);
        return;
    }
    p++;

    int max_addr = 2;
    switch (verb) {
    case ':': case '}':           max_addr = 0; break;
    case 'a': case 'i': case 'q':
    case 'r': case '=':           max_addr = 1; break;
    }
    if (c->naddr > max_addr) {
        if (max_addr == 0)
            die("command '%c' takes no address", verb);
        die("command '%c' takes at most one address", verb);
    }

    switch (verb) {
    case '{':
        c->type = CMD_LBRACE;
        append_cmd(c);
        parse_commands(depth + 1);
        return;
    case 'a': c->type = CMD_a; c->text = parse_text(); break;
    case 'c': c->type = CMD_c; c->text = parse_text(); break;
    case 'i': c->type = CMD_i; c->text = parse_text(); break;
    case 'b': c->type = CMD_b; c->label = parse_label(true); break;
    case 't': c->type = CMD_t; c->label = parse_label(true); break;
    case ':':
        c->type = CMD_LABEL;
        c->label = parse_label(false);
        if (!*c->label)
            die("':' needs a label");
        break;
    case 'r': c->type = CMD_r; c->rfile = parse_filename(); break;
    case 'w': {
        c->type = CMD_w;
        char *f = parse_filename();
        c->wfp = wfile_get(f);
        free(f);
        break;
    }
    case 's': c->type = CMD_s; parse_s(c); break;
    case 'y': c->type = CMD_y; parse_y(c); break;
    case 'd': c->type = CMD_d; end_command(verb); break;
    case 'D': c->type = CMD_D; end_command(verb); break;
    case 'g': c->type = CMD_g; end_command(verb); break;
    case 'G': c->type = CMD_G; end_command(verb); break;
    case 'h': c->type = CMD_h; end_command(verb); break;
    case 'H': c->type = CMD_H; end_command(verb); break;
    case 'l': c->type = CMD_l; end_command(verb); break;
    case 'n': c->type = CMD_n; end_command(verb); break;
    case 'N': c->type = CMD_N; end_command(verb); break;
    case 'p': c->type = CMD_p; end_command(verb); break;
    case 'P': c->type = CMD_P; end_command(verb); break;
    case 'q': c->type = CMD_q; end_command(verb); break;
    case 'x': c->type = CMD_x; end_command(verb); break;
    case '=': c->type = CMD_EQUALS; end_command(verb); break;
    default:
        die("unknown command '%c'", verb);
    }
    append_cmd(c);
}

static void parse_commands(int depth)
{
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == ';' || *p == '\n')
            p++;
        if (!*p) {
            if (depth > 0)
                die("unmatched '{'");
            return;
        }
        if (*p == '}') {
            if (depth == 0)
                die("unexpected '}'");
            p++;
            cmd_t *c = new_cmd();
            c->type = CMD_RBRACE;
            append_cmd(c);
            end_command('}');
            return;
        }
        parse_one_command(depth);
    }
}

static void resolve(void)
{
    cmd_t **stack = NULL;
    size_t depth = 0, cap = 0;
    for (cmd_t *c = script; c; c = c->next) {
        if (c->type == CMD_LBRACE) {
            if (depth == cap) {
                cap = cap ? cap * 2 : 16;
                stack = realloc(stack, cap * sizeof *stack);
                if (!stack)
                    oom();
            }
            stack[depth++] = c;
        } else if (c->type == CMD_RBRACE) {
            stack[--depth]->end_brace = c;
        }
    }
    free(stack);

    for (cmd_t *c = script; c; c = c->next) {
        if (c->type == CMD_LABEL)
            for (cmd_t *d = c->next; d; d = d->next)
                if (d->type == CMD_LABEL && strcmp(d->label, c->label) == 0)
                    die("duplicate label '%s'", c->label);
        if ((c->type == CMD_b || c->type == CMD_t) && c->label[0]) {
            cmd_t *d;
            for (d = script; d; d = d->next)
                if (d->type == CMD_LABEL && strcmp(d->label, c->label) == 0)
                    break;
            if (!d)
                die("can't find label '%s'", c->label);
            c->target = d;
        }
    }
}

/* ---- input: the concatenation of the files, with one line of lookahead ---- */

static char **in_files;
static int    in_nfiles, in_cur;
static FILE  *in_fp;
static dstr_t look;
static bool   have_look, look_done;     /* look holds the next line / no more */
static char  *rd_buf;
static size_t rd_cap;

/* the next line of all the input; false at its end */
static bool read_raw(dstr_t *d)
{
    for (;;) {
        if (!in_fp) {
            if (in_cur >= in_nfiles)
                return false;
            const char *name = in_files[in_cur];
            if (strcmp(name, "-") == 0) {
                in_fp = stdin;
            } else if (!(in_fp = fopen(name, "r"))) {
                fprintf(stderr, "sed: %s: %s\n", name, strerror(errno));
                status = 2;
                in_cur++;
                continue;
            }
        }
        ssize_t n = getline(&rd_buf, &rd_cap, in_fp);
        if (n >= 0) {
            size_t len = (size_t)n;
            if (len && rd_buf[len - 1] == '\n')
                len--;
            dstr_set(d, rd_buf, len);
            return true;
        }
        if (ferror(in_fp)) {
            fprintf(stderr, "sed: %s: %s\n", in_files[in_cur], strerror(errno));
            status = 2;
        }
        if (in_fp == stdin)
            clearerr(stdin);
        else
            fclose(in_fp);
        in_fp = NULL;
        in_cur++;
    }
}

/* read the next input line into d: false if there is none */
static bool next_line(dstr_t *d)
{
    if (have_look) {
        dstr_set(d, look.buf, look.len);
        have_look = false;
    } else if (look_done || !read_raw(d)) {
        look_done = true;
        return false;
    }
    line_number++;
    sub_made = false;
    return true;
}

static bool is_last_line(void)
{
    if (have_look)
        return false;
    if (look_done)
        return true;
    have_look = read_raw(&look);
    look_done = !have_look;
    return look_done;
}

/* ---- execution ---- */

static bool addr_match(const addr_t *a)
{
    switch (a->type) {
    case ADDR_LINE:
        return line_number == a->lineno;
    case ADDR_LAST:
        return is_last_line();
    case ADDR_RE: {
        regmatch_t m[1];
        return re_match(use_re(a->re), pattern_space.buf, pattern_space.len, 0, 1, m);
    }
    default:
        return false;
    }
}

static bool cmd_selected(cmd_t *c)
{
    bool sel;
    if (c->naddr == 0) {
        sel = true;
    } else if (c->naddr == 1) {
        sel = addr_match(&c->addr[0]);
    } else if (c->in_range) {
        sel = true;
        if (c->addr[1].type == ADDR_LINE) {
            if (line_number >= c->addr[1].lineno)
                c->in_range = false;
        } else if (addr_match(&c->addr[1])) {
            c->in_range = false;
        }
    } else if (addr_match(&c->addr[0])) {
        sel = true;
        /* a line number not after this line: only this line */
        if (c->addr[1].type == ADDR_LINE && c->addr[1].lineno <= line_number)
            c->in_range = false;
        else if (c->addr[1].type == ADDR_LAST && is_last_line())
            c->in_range = false;
        else
            c->in_range = true;
    } else {
        sel = false;
    }
    return c->negated ? !sel : sel;
}

#define L_WIDTH 70

static void cmd_l(void)
{
    const char *s = pattern_space.buf;
    size_t n = pattern_space.len, i = 0;
    int col = 0;
    while (i < n) {
        char esc[4 * MB_LEN_MAX + 1];
        size_t elen = 0;
        int width;
        unsigned char ch = (unsigned char)s[i];
        size_t k = 1;
        const char *e = NULL;
        switch (ch) {
        case '\\': e = "\\\\"; break;
        case '\a': e = "\\a"; break;
        case '\b': e = "\\b"; break;
        case '\f': e = "\\f"; break;
        case '\r': e = "\\r"; break;
        case '\t': e = "\\t"; break;
        case '\v': e = "\\v"; break;
        }
        if (e) {
            elen = strlen(e);
            memcpy(esc, e, elen);
            width = (int)elen;
        } else {
            wchar_t w;
            mbstate_t st;
            memset(&st, 0, sizeof st);
            size_t r = mbrtowc(&w, s + i, n - i, &st);
            bool printable = false;
            if (r != (size_t)-1 && r != (size_t)-2 && r != 0) {
                k = r;
                printable = iswprint((wint_t)w) != 0;
            }
            if (printable) {
                memcpy(esc, s + i, k);
                elen = k;
                width = wcwidth(w);
                if (width < 0)
                    width = 1;
            } else {
                for (size_t j = 0; j < k; j++)
                    elen += (size_t)snprintf(esc + elen, sizeof esc - elen, "\\%03o",
                                             (unsigned char)s[i + j]);
                width = (int)elen;
            }
        }
        if (col + width >= L_WIDTH) {
            out_bytes(stdout, "\\\n", 2);
            col = 0;
        }
        out_bytes(stdout, esc, elen);
        col += width;
        i += k;
    }
    out_bytes(stdout, "$\n", 2);
}

/* append the replacement for match m of src to d */
static void append_repl(dstr_t *d, const cmd_t *c, const char *src, const regmatch_t *m)
{
    const char *r = c->s_repl, *end = r + c->s_repl_len;
    while (r < end) {
        if (*r == '&') {
            dstr_append(d, src + m[0].rm_so, (size_t)(m[0].rm_eo - m[0].rm_so));
            r++;
        } else if (*r == '\\' && r + 1 < end) {
            char x = r[1];
            if (x >= '0' && x <= '9') {
                int i = x - '0';
                if (m[i].rm_so >= 0)
                    dstr_append(d, src + m[i].rm_so, (size_t)(m[i].rm_eo - m[i].rm_so));
            } else {
                dstr_addc(d, x);        /* \&, \\, \<newline>, others: the character */
            }
            r += 2;
        } else {
            dstr_addc(d, *r++);
        }
    }
}

static bool do_subst(cmd_t *c)
{
    regex_t *re = use_re(c->s_re);
    const char *src = pattern_space.buf;
    size_t len = pattern_space.len;
    dstr_t res;
    dstr_init(&res);
    bool changed = false;
    long occur = 0;
    size_t pos = 0, copied = 0;     /* src[0..copied) is in res */
    size_t prev_end = (size_t)-1;
    regmatch_t m[10];

    while (pos <= len) {
        for (int i = 0; i < 10; i++)
            m[i].rm_so = m[i].rm_eo = -1;
        if (!re_match(re, src, len, pos, 10, m))
            break;
        size_t so = (size_t)m[0].rm_so, eo = (size_t)m[0].rm_eo;
        if (so == eo && so == prev_end) {
            /* an empty match right after the previous match: not one */
            if (so >= len)
                break;
            pos = so + charlen(src + so, len - so);
            continue;
        }
        occur++;
        bool apply = c->s_global ? occur >= c->s_nth : occur == c->s_nth;
        if (apply) {
            dstr_append(&res, src + copied, so - copied);
            append_repl(&res, c, src, m);
            copied = eo;
            changed = true;
        }
        prev_end = eo;
        if (so == eo) {
            if (eo >= len)
                break;
            pos = eo + charlen(src + eo, len - eo);
        } else {
            pos = eo;
        }
        if (apply && !c->s_global)
            break;
    }
    if (changed) {
        dstr_append(&res, src + copied, len - copied);
        dstr_set(&pattern_space, res.buf, res.len);
    }
    dstr_free(&res);
    return changed;
}

static void do_y(const cmd_t *c)
{
    dstr_t res;
    dstr_init(&res);
    const char *s = pattern_space.buf;
    size_t n = pattern_space.len;
    for (size_t i = 0; i < n; ) {
        size_t k = charlen(s + i, n - i);
        size_t j;
        for (j = 0; j < c->y_len; j++)
            if (c->y_from[j].n == k && memcmp(c->y_from[j].b, s + i, k) == 0)
                break;
        if (j < c->y_len)
            dstr_append(&res, c->y_to[j].b, c->y_to[j].n);
        else
            dstr_append(&res, s + i, k);
        i += k;
    }
    dstr_set(&pattern_space, res.buf, res.len);
    dstr_free(&res);
}

typedef enum { EX_END, EX_NEW_CYCLE, EX_RESTART, EX_QUIT, EX_QUIT_SILENT } ex_result_t;

static ex_result_t execute(void)
{
    cmd_t *c = script;
    while (c) {
        if (c->type == CMD_RBRACE) {
            c = c->next;
            continue;
        }
        if (!cmd_selected(c)) {
            if (c->type == CMD_LBRACE)
                c = c->end_brace->next;
            else
                c = c->next;
            continue;
        }

        switch (c->type) {
        case CMD_LBRACE:
            break;
        case CMD_a:
            aq_add(false, c->text);
            break;
        case CMD_b:
            if (!c->target)
                return EX_END;
            c = c->target;
            continue;
        case CMD_c:
            if (c->naddr < 2 || c->negated || !c->in_range)
                out_line(stdout, c->text, strlen(c->text));
            return EX_NEW_CYCLE;
        case CMD_d:
            return EX_NEW_CYCLE;
        case CMD_D: {
            char *nl = memchr(pattern_space.buf, '\n', pattern_space.len);
            if (!nl)
                return EX_NEW_CYCLE;
            size_t off = (size_t)(nl - pattern_space.buf) + 1;
            dstr_set(&pattern_space, pattern_space.buf + off, pattern_space.len - off);
            return EX_RESTART;
        }
        case CMD_g:
            dstr_set(&pattern_space, hold_space.buf, hold_space.len);
            break;
        case CMD_G:
            dstr_addc(&pattern_space, '\n');
            dstr_append(&pattern_space, hold_space.buf, hold_space.len);
            break;
        case CMD_h:
            dstr_set(&hold_space, pattern_space.buf, pattern_space.len);
            break;
        case CMD_H:
            dstr_addc(&hold_space, '\n');
            dstr_append(&hold_space, pattern_space.buf, pattern_space.len);
            break;
        case CMD_i:
            out_line(stdout, c->text, strlen(c->text));
            break;
        case CMD_l:
            cmd_l();
            break;
        case CMD_n:
            if (is_last_line())
                return EX_QUIT;     /* the end of the script writes it */
            if (!opt_n)
                out_ps(stdout);
            aq_flush();
            next_line(&pattern_space);
            break;
        case CMD_N: {
            if (is_last_line())
                return EX_QUIT_SILENT;
            aq_flush();
            dstr_t nl;
            dstr_init(&nl);
            next_line(&nl);
            dstr_addc(&pattern_space, '\n');
            dstr_append(&pattern_space, nl.buf, nl.len);
            dstr_free(&nl);
            break;
        }
        case CMD_p:
            out_ps(stdout);
            break;
        case CMD_P: {
            char *nl = memchr(pattern_space.buf, '\n', pattern_space.len);
            out_line(stdout, pattern_space.buf,
                     nl ? (size_t)(nl - pattern_space.buf) : pattern_space.len);
            break;
        }
        case CMD_q:
            return EX_QUIT;
        case CMD_r:
            aq_add(true, c->rfile);
            break;
        case CMD_s:
            if (do_subst(c)) {
                sub_made = true;
                if (c->s_print)
                    out_ps(stdout);
                if (c->wfp)
                    out_ps(c->wfp);
            }
            break;
        case CMD_t:
            if (sub_made) {
                sub_made = false;
                if (!c->target)
                    return EX_END;
                c = c->target;
                continue;
            }
            break;
        case CMD_w:
            out_ps(c->wfp);
            break;
        case CMD_x: {
            dstr_t tmp = pattern_space;
            pattern_space = hold_space;
            hold_space = tmp;
            break;
        }
        case CMD_y:
            do_y(c);
            break;
        case CMD_EQUALS:
            if (printf("%ld\n", line_number) < 0) {
                perror("sed: write error");
                exit(4);
            }
            break;
        case CMD_LABEL:
        case CMD_RBRACE:
            break;
        }
        c = c->next;
    }
    return EX_END;
}

static void process(void)
{
    if (!next_line(&pattern_space))
        return;
    for (;;) {
        ex_result_t r = execute();
        if (r == EX_QUIT_SILENT) {
            aq_flush();
            return;
        }
        if ((r == EX_END || r == EX_QUIT) && !opt_n)
            out_ps(stdout);
        aq_flush();
        if (r == EX_QUIT)
            return;
        if (r == EX_RESTART)
            continue;           /* the rest of the pattern space, no new input */
        if (!next_line(&pattern_space))
            return;
    }
}

static void usage(void)
{
    fputs("usage: sed [-En] script [file...]\n"
          "       sed [-En] -e script [-e script]... [-f script_file]... [file...]\n"
          "       sed [-En] [-e script]... -f script_file [-f script_file]... [file...]\n",
          stderr);
    exit(1);
}

int main(int argc, char *argv[])
{
    dstr_t combined;
    bool have_script = false, prev_e = false;
    int opt;

    setlocale(LC_ALL, "");
    dstr_init(&combined);

    while ((opt = getopt(argc, argv, "Ene:f:")) != -1) {
        switch (opt) {
        case 'E': opt_E = true; break;
        case 'n': opt_n = true; break;
        case 'e':
            if (prev_e)
                dstr_addc(&combined, '\n');
            dstr_append(&combined, optarg, strlen(optarg));
            have_script = true;
            prev_e = true;
            break;
        case 'f': {
            FILE *fp = strcmp(optarg, "-") == 0 ? stdin : fopen(optarg, "r");
            if (!fp)
                die("%s: %s", optarg, strerror(errno));
            if (prev_e)
                dstr_addc(&combined, '\n');
            char buf[8192];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, fp)) > 0)
                dstr_append(&combined, buf, n);
            if (ferror(fp))
                die("%s: %s", optarg, strerror(errno));
            if (fp != stdin)
                fclose(fp);
            have_script = true;
            prev_e = false;
            break;
        }
        default:
            usage();
        }
    }
    if (!have_script) {
        if (optind >= argc)
            usage();
        dstr_append(&combined, argv[optind], strlen(argv[optind]));
        optind++;
    }
    if (strlen(combined.buf) != combined.len)
        die("the script contains a NUL byte");

    if (combined.buf[0] == '#' && combined.buf[1] == 'n')
        opt_n = true;
    p = combined.buf;
    parse_commands(0);
    resolve();

    static char *stdin_name[] = { "-" };
    if (optind >= argc) {
        in_files = stdin_name;
        in_nfiles = 1;
    } else {
        in_files = argv + optind;
        in_nfiles = argc - optind;
    }
    dstr_init(&pattern_space);
    dstr_init(&hold_space);
    dstr_init(&look);

    process();

    for (wf_t *w = wfiles; w; w = w->next)
        if (fclose(w->fp) == EOF) {
            fprintf(stderr, "sed: %s: %s\n", w->path, strerror(errno));
            status = 4;
        }
    if (fflush(stdout) == EOF || ferror(stdout)) {
        perror("sed: write error");
        return 4;
    }
    return status;
}
