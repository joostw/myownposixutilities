#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/*  Dynamic string                                                      */
/* ------------------------------------------------------------------ */

typedef struct { char *buf; size_t len, cap; } dstr_t;

static void dstr_init(dstr_t *d)
{
    d->cap = 256;
    d->buf = malloc(d->cap);
    d->len = 0;
    if (!d->buf) { perror("sed"); exit(1); }
    d->buf[0] = '\0';
}

static void dstr_ensure(dstr_t *d, size_t need)
{
    if (need < d->cap) return;
    size_t nc = d->cap * 2;
    while (nc <= need) nc *= 2;
    char *nb = realloc(d->buf, nc);
    if (!nb) { perror("sed"); exit(1); }
    d->buf = nb; d->cap = nc;
}

static void dstr_set(dstr_t *d, const char *s, size_t n)
{
    dstr_ensure(d, n + 1);
    memcpy(d->buf, s, n); d->buf[n] = '\0'; d->len = n;
}

static void dstr_append(dstr_t *d, const char *s, size_t n)
{
    dstr_ensure(d, d->len + n + 1);
    memcpy(d->buf + d->len, s, n); d->len += n; d->buf[d->len] = '\0';
}

static void dstr_free(dstr_t *d) { free(d->buf); d->buf = NULL; }

/* ------------------------------------------------------------------ */
/*  Address                                                             */
/* ------------------------------------------------------------------ */

typedef enum { ADDR_NONE, ADDR_LINE, ADDR_LAST, ADDR_RE } addr_type_t;

typedef struct {
    addr_type_t  type;
    long         lineno;
    regex_t      re;
    bool         re_valid;
} addr_t;

/* ------------------------------------------------------------------ */
/*  Commands                                                            */
/* ------------------------------------------------------------------ */

typedef enum {
    CMD_LBRACE, CMD_RBRACE,
    CMD_a, CMD_b, CMD_c, CMD_d, CMD_D,
    CMD_g, CMD_G, CMD_h, CMD_H,
    CMD_i, CMD_l, CMD_n, CMD_N,
    CMD_p, CMD_P, CMD_q,
    CMD_r, CMD_s, CMD_t, CMD_w,
    CMD_x, CMD_y, CMD_LABEL,
    CMD_EQUALS, CMD_COMMENT, CMD_EMPTY
} cmd_type_t;

typedef struct cmd {
    addr_t      addr[2];
    int         naddr;
    bool        negated;
    cmd_type_t  type;

    /* a, c, i */
    char       *text;

    /* b, t, : */
    char       *label;

    /* r */
    char       *rfile;

    /* w */
    char       *wfile;
    FILE       *wfp;

    /* s */
    regex_t     s_re;
    bool        s_re_valid;
    char       *s_repl;
    int         s_nth;      /* 0 = first, -1 = global */
    bool        s_print;
    bool        s_icase;
    char       *s_wfile;
    FILE       *s_wfp;

    /* y */
    char       *y_from;
    char       *y_to;
    size_t      y_len;

    /* range state (per-command, reset each new input file? no — line numbers
       are cumulative across files per spec) */
    bool        in_range;
    long        range_start;  /* line where range opened */

    /* resolved branch target */
    struct cmd *target;

    /* matching } for { */
    struct cmd *end_brace;

    struct cmd *next;
} cmd_t;

/* ------------------------------------------------------------------ */
/*  Global state                                                        */
/* ------------------------------------------------------------------ */

static cmd_t   *script;
static cmd_t   *script_tail;

static bool     opt_n    = false;
static bool     opt_E    = false;
static int      cflags_base = 0;  /* REG_EXTENDED when -E */

static long     line_number  = 0;
static bool     last_line    = false;

static dstr_t   pattern_space;
static dstr_t   hold_space;

/* append queue (a / r commands) */
typedef struct aitem { bool is_file; char *text; struct aitem *next; } aitem_t;
static aitem_t *aq_head, *aq_tail;

/* last used regex for empty-RE reuse */
static regex_t *last_re = NULL;

static bool     sub_made = false;  /* cleared each new input line; set by s */

/* wfile registry: created before processing begins */
typedef struct wf { char *path; FILE *fp; struct wf *next; } wf_t;
static wf_t    *wfiles;

/* ------------------------------------------------------------------ */
/*  Error helpers                                                       */
/* ------------------------------------------------------------------ */

static void die(const char *msg)
{
    fprintf(stderr, "sed: %s\n", msg);
    exit(1);
}

/* ------------------------------------------------------------------ */
/*  wfile registry                                                      */
/* ------------------------------------------------------------------ */

static FILE *wfile_get(const char *path)
{
    for (wf_t *w = wfiles; w; w = w->next)
        if (strcmp(w->path, path) == 0) return w->fp;

    wf_t *w = malloc(sizeof *w);
    if (!w) { perror("sed"); exit(1); }
    w->path = strdup(path);
    w->fp   = fopen(path, "w");
    if (!w->fp) {
        fprintf(stderr, "sed: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    w->next  = wfiles;
    wfiles   = w;
    return w->fp;
}

/* ------------------------------------------------------------------ */
/*  Append queue                                                        */
/* ------------------------------------------------------------------ */

static void aq_add(bool is_file, const char *s)
{
    aitem_t *it = malloc(sizeof *it);
    if (!it) { perror("sed"); exit(1); }
    it->is_file = is_file;
    it->text    = strdup(s);
    it->next    = NULL;
    if (aq_tail) aq_tail->next = it; else aq_head = it;
    aq_tail = it;
}

static void aq_flush(void)
{
    while (aq_head) {
        aitem_t *it = aq_head;
        aq_head = it->next;
        if (!aq_head) aq_tail = NULL;

        if (it->is_file) {
            FILE *fp = fopen(it->text, "r");
            if (fp) {
                char buf[4096]; size_t n;
                while ((n = fread(buf, 1, sizeof buf, fp)) > 0)
                    fwrite(buf, 1, n, stdout);
                fclose(fp);
            }
            /* missing file is silently ignored per spec */
        } else {
            fputs(it->text, stdout);
            putchar('\n');
        }
        free(it->text); free(it);
    }
}

/* ------------------------------------------------------------------ */
/*  Regex compilation helper                                            */
/* ------------------------------------------------------------------ */

/* Convert sed RE source (with \n → newline, \delim → delim) into a
   compiled regex_t. empty_ok: if src is empty, set *out_re = NULL (caller
   must use last_re). Returns allocated regex_t or NULL. */
static regex_t *compile_re(const char *src, size_t srclen, int extra_cflags)
{
    if (srclen == 0) return NULL;  /* empty: reuse last */

    /* Translate \n to real newline in pattern. */
    dstr_t tmp;
    dstr_init(&tmp);
    for (size_t i = 0; i < srclen; i++) {
        if (src[i] == '\\' && i + 1 < srclen && src[i+1] == 'n') {
            dstr_append(&tmp, "\n", 1);
            i++;
        } else {
            dstr_append(&tmp, src + i, 1);
        }
    }

    regex_t *re = malloc(sizeof *re);
    if (!re) { perror("sed"); exit(1); }
    int cflags = cflags_base | REG_NEWLINE | extra_cflags;
    int rc = regcomp(re, tmp.buf, cflags);
    if (rc != 0) {
        char buf[256];
        regerror(rc, re, buf, sizeof buf);
        fprintf(stderr, "sed: %s\n", buf);
        exit(1);
    }
    dstr_free(&tmp);
    return re;
}

/* ------------------------------------------------------------------ */
/*  Script parsing                                                      */
/* ------------------------------------------------------------------ */

static const char *p;   /* parse cursor */

static void skip_blanks(void) { while (*p == ' ' || *p == '\t') p++; }

/* skip optional semicolons and blanks between commands */
static void skip_sep(void)
{
    for (;;) {
        skip_blanks();
        if (*p == ';' || *p == '\n') { p++; continue; }
        break;
    }
}

/* Parse an address starting at p. Returns true if an address was found. */
static bool parse_addr(addr_t *a)
{
    memset(a, 0, sizeof *a);
    skip_blanks();
    if (*p == '$') {
        a->type = ADDR_LAST;
        p++;
        return true;
    }
    if (isdigit((unsigned char)*p)) {
        a->type   = ADDR_LINE;
        a->lineno = 0;
        while (isdigit((unsigned char)*p))
            a->lineno = a->lineno * 10 + (*p++ - '0');
        return true;
    }
    char delim;
    if (*p == '/') {
        delim = '/';
        p++;  /* consume opening delimiter */
    } else if (*p == '\\' && *(p+1) != '\0' && *(p+1) != '\n') {
        p++;          /* skip backslash */
        delim = *p++; /* get delimiter; p now points at start of RE */
    } else {
        return false;
    }

    /* collect raw pattern until unescaped closing delim */
    dstr_t raw; dstr_init(&raw);
    while (*p && *p != '\n') {
        if (*p == '\\' && *(p+1) != '\0' && *(p+1) != '\n') {
            dstr_append(&raw, p, 2); p += 2;  /* keep escape intact for regcomp */
        } else if (*p == delim) {
            break;
        } else {
            dstr_append(&raw, p++, 1);
        }
    }
    if (*p != delim) die("unterminated address regex");
    p++;  /* consume closing delim */

    a->type = ADDR_RE;
    regex_t *re = compile_re(raw.buf, raw.len, 0);
    if (re) { a->re = *re; free(re); a->re_valid = true; last_re = &a->re; }
    dstr_free(&raw);
    return true;
}

/* Parse text argument for a, c, i commands.
   Handles the traditional \<newline>text form and the single-line form. */
static char *parse_text(void)
{
    dstr_t t; dstr_init(&t);
    /* Two forms:
       1)  a\<newline>line1\<newline>line2<newline>    (classic)
       2)  a text on same line (extension, common)
       The spec requires form 1 but we accept both.         */
    if (*p == '\\') {
        p++;  /* skip backslash */
        if (*p == '\n') p++;  /* skip newline */
    } else {
        /* skip optional single space */
        if (*p == ' ') p++;
    }
    /* Read lines until one doesn't end with backslash-newline */
    for (;;) {
        const char *start = p;
        while (*p && *p != '\n') p++;
        size_t len = (size_t)(p - start);
        if (len > 0 && start[len-1] == '\\') {
            /* continuation: add line without trailing backslash, add \n */
            dstr_append(&t, start, len - 1);
            dstr_append(&t, "\n", 1);
            if (*p == '\n') p++;
        } else {
            dstr_append(&t, start, len);
            if (*p == '\n') p++;
            break;
        }
    }
    char *s = strdup(t.buf);
    dstr_free(&t);
    return s;
}

/* Parse a label (for b, t, :). Reads until semicolon, newline, or } */
static char *parse_label(void)
{
    skip_blanks();
    const char *start = p;
    while (*p && *p != ';' && *p != '\n' && *p != '}') p++;
    /* trim trailing blanks */
    const char *end = p;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
    size_t len = (size_t)(end - start);
    char *l = malloc(len + 1);
    if (!l) { perror("sed"); exit(1); }
    memcpy(l, start, len); l[len] = '\0';
    return l;
}

/* Parse a filename (for r, w).
   Per spec the filename terminates the command: consume to end of line. */
static char *parse_filename(void)
{
    while (*p == ' ' || *p == '\t') p++;
    const char *start = p;
    while (*p && *p != '\n') p++;
    size_t len = (size_t)(p - start);
    while (len > 0 && (start[len-1] == ' ' || start[len-1] == '\t')) len--;
    char *f = malloc(len + 1);
    if (!f) { perror("sed"); exit(1); }
    memcpy(f, start, len); f[len] = '\0';
    return f;
}

/* Parse s command: s/RE/replacement/flags */
static cmd_t *parse_s(cmd_t *cmd)
{
    if (!*p) die("s: missing delimiter");
    char delim = *p++;

    /* collect RE source */
    dstr_t re_src; dstr_init(&re_src);
    while (*p && *p != '\n') {
        if (*p == '\\' && *(p+1) != '\0') {
            if (*(p+1) == delim) { dstr_append(&re_src, &delim, 1); p += 2; }
            else { dstr_append(&re_src, p, 2); p += 2; }
        } else if (*p == delim) { break; }
        else { dstr_append(&re_src, p++, 1); }
    }
    if (*p != delim) die("s: unterminated RE");
    p++;

    /* collect replacement */
    dstr_t repl; dstr_init(&repl);
    while (*p && *p != '\n') {
        if (*p == '\\' && *(p+1) != '\0') {
            if (*(p+1) == delim) { dstr_append(&repl, &delim, 1); p += 2; }
            else if (*(p+1) == '\n') { dstr_append(&repl, "\n", 1); p += 2; }
            else { dstr_append(&repl, p, 2); p += 2; }
        } else if (*p == delim) { break; }
        else { dstr_append(&repl, p++, 1); }
    }
    if (*p != delim) die("s: unterminated replacement");
    p++;

    /* flags */
    cmd->s_nth    = 1;   /* first occurrence by default */
    cmd->s_print  = false;
    cmd->s_icase  = false;
    cmd->s_wfile  = NULL;
    bool has_g    = false;

    while (*p && *p != '\n' && *p != ';' && *p != '}') {
        char f = *p++;
        if (f == 'g') { has_g = true; cmd->s_nth = -1; }
        else if (f == 'p') { cmd->s_print = true; }
        else if (f == 'i' || f == 'I') { cmd->s_icase = true; }
        else if (isdigit((unsigned char)f)) {
            cmd->s_nth = f - '0';
            while (isdigit((unsigned char)*p)) cmd->s_nth = cmd->s_nth*10 + (*p++ - '0');
        } else if (f == 'w') {
            cmd->s_wfile = parse_filename();
            break;
        } else {
            /* unknown flag: treat as error or skip */
            fprintf(stderr, "sed: unknown s flag '%c'\n", f);
            exit(1);
        }
    }
    (void)has_g;

    int extra = cmd->s_icase ? REG_ICASE : 0;
    regex_t *re = compile_re(re_src.buf, re_src.len, extra);
    if (re) { cmd->s_re = *re; free(re); cmd->s_re_valid = true; last_re = &cmd->s_re; }
    cmd->s_repl = strdup(repl.buf);
    dstr_free(&re_src); dstr_free(&repl);
    return cmd;
}

/* Parse y command: y/str1/str2/ */
static cmd_t *parse_y(cmd_t *cmd)
{
    if (!*p) die("y: missing delimiter");
    char delim = *p++;

    dstr_t from, to; dstr_init(&from); dstr_init(&to);

    for (int part = 0; part < 2; part++) {
        dstr_t *d = (part == 0) ? &from : &to;
        while (*p && *p != '\n') {
            if (*p == '\\' && *(p+1) != '\0') {
                if (*(p+1) == 'n') { dstr_append(d, "\n", 1); p += 2; }
                else if (*(p+1) == delim) { dstr_append(d, &delim, 1); p += 2; }
                else if (*(p+1) == '\\') { dstr_append(d, "\\", 1); p += 2; }
                else { dstr_append(d, p, 2); p += 2; }
            } else if (*p == delim) { p++; break; }
            else { dstr_append(d, p++, 1); }
        }
    }

    if (from.len != to.len) die("y: strings have different lengths");

    cmd->y_from = strdup(from.buf);
    cmd->y_to   = strdup(to.buf);
    cmd->y_len  = from.len;
    dstr_free(&from); dstr_free(&to);
    return cmd;
}

static cmd_t *new_cmd(void)
{
    cmd_t *c = calloc(1, sizeof *c);
    if (!c) { perror("sed"); exit(1); }
    return c;
}

static void append_cmd(cmd_t *c)
{
    if (script_tail) script_tail->next = c; else script = c;
    script_tail = c;
}

/* Forward declaration */
static void parse_commands(int depth);

static void parse_one_command(int depth)
{
    skip_blanks();
    if (!*p || *p == '\n' || *p == ';') return;
    if (*p == '#') {
        p++;
        while (*p && *p != '\n') p++;
        return;
    }

    cmd_t *c = new_cmd();

    /* parse addresses */
    if (parse_addr(&c->addr[0])) {
        c->naddr = 1;
        skip_blanks();
        if (*p == ',') {
            p++;
            skip_blanks();
            if (!parse_addr(&c->addr[1]))
                die("expected address after ','");
            c->naddr = 2;
        }
    }

    skip_blanks();
    if (*p == '!') { c->negated = true; p++; }
    skip_blanks();

    char cmd = *p++;

    /* validate address count per spec address annotations */
    int max_addr;
    switch (cmd) {
    case ':': case '#':
        max_addr = 0; break;
    case 'a': case 'i': case 'q': case 'r': case '=':
        max_addr = 1; break;
    default:
        max_addr = 2; break;
    }
    if (c->naddr > max_addr) {
        fprintf(stderr, "sed: command '%c' accepts at most %d address(es)\n",
                cmd, max_addr);
        exit(1);
    }

    switch (cmd) {
    case '{':
        c->type = CMD_LBRACE;
        append_cmd(c);
        skip_sep();
        parse_commands(depth + 1);
        return;
    case 'a': c->type = CMD_a; c->text  = parse_text();      break;
    case 'b': c->type = CMD_b; c->label = parse_label();     break;
    case 'c': c->type = CMD_c; c->text  = parse_text();      break;
    case 'd': c->type = CMD_d;                                break;
    case 'D': c->type = CMD_D;                                break;
    case 'g': c->type = CMD_g;                                break;
    case 'G': c->type = CMD_G;                                break;
    case 'h': c->type = CMD_h;                                break;
    case 'H': c->type = CMD_H;                                break;
    case 'i': c->type = CMD_i; c->text  = parse_text();      break;
    case 'l': c->type = CMD_l;                                break;
    case 'n': c->type = CMD_n;                                break;
    case 'N': c->type = CMD_N;                                break;
    case 'p': c->type = CMD_p;                                break;
    case 'P': c->type = CMD_P;                                break;
    case 'q': c->type = CMD_q;                                break;
    case 'r': c->type = CMD_r; c->rfile = parse_filename();   break;
    case 's': c->type = CMD_s; parse_s(c);                    break;
    case 't': c->type = CMD_t; c->label = parse_label();     break;
    case 'w': c->type = CMD_w; c->wfile = parse_filename();   break;
    case 'x': c->type = CMD_x;                                break;
    case 'y': c->type = CMD_y; parse_y(c);                    break;
    case ':': c->type = CMD_LABEL; c->label = parse_label();  break;
    case '=': c->type = CMD_EQUALS;                           break;
    case '\0': die("unexpected end of script");               break;
    default:
        fprintf(stderr, "sed: unknown command '%c'\n", cmd);
        exit(1);
    }
    append_cmd(c);
}

static void parse_commands(int depth)
{
    for (;;) {
        skip_sep();
        if (!*p) {
            if (depth > 0) die("unmatched {");
            break;
        }
        if (*p == '}') {
            if (depth == 0) die("unexpected }");
            p++;  /* consume } */
            cmd_t *c = new_cmd(); c->type = CMD_RBRACE;
            append_cmd(c);
            return;
        }
        parse_one_command(depth);
    }
}

/* Check if #n appears as the very first two characters of the combined
   script (after joining all -e/-f sources). We do this before parse. */
static void check_hash_n(const char *script_src)
{
    if (script_src[0] == '#' && script_src[1] == 'n')
        opt_n = true;
}

/* ------------------------------------------------------------------ */
/*  Post-parse: resolve branches, link braces, open wfiles             */
/* ------------------------------------------------------------------ */

static void resolve(void)
{
    /* Link { to matching } */
    cmd_t *stack[256];
    int    depth = 0;
    for (cmd_t *c = script; c; c = c->next) {
        if (c->type == CMD_LBRACE) {
            if (depth >= 256) die("too deeply nested {}");
            stack[depth++] = c;
        } else if (c->type == CMD_RBRACE) {
            if (depth == 0) die("unmatched }");
            stack[--depth]->end_brace = c;
        }
    }
    if (depth) die("unmatched {");

    /* Resolve b / t labels */
    for (cmd_t *c = script; c; c = c->next) {
        if ((c->type == CMD_b || c->type == CMD_t) && c->label && c->label[0]) {
            bool found = false;
            for (cmd_t *d = script; d; d = d->next) {
                if (d->type == CMD_LABEL && strcmp(d->label, c->label) == 0) {
                    c->target = d->next;  /* branch to cmd after : */
                    found = true; break;
                }
            }
            if (!found) {
                fprintf(stderr, "sed: label '%s' not found\n", c->label);
                exit(1);
            }
        }
    }

    /* Pre-open wfiles (created before processing begins) */
    for (cmd_t *c = script; c; c = c->next) {
        if (c->type == CMD_w && c->wfile)
            c->wfp = wfile_get(c->wfile);
        if (c->type == CMD_s && c->s_wfile)
            c->s_wfp = wfile_get(c->s_wfile);
    }
}

/* ------------------------------------------------------------------ */
/*  Address matching                                                    */
/* ------------------------------------------------------------------ */

static bool addr_match(const addr_t *a, long lineno, bool is_last)
{
    switch (a->type) {
    case ADDR_LINE: return lineno == a->lineno;
    case ADDR_LAST: return is_last;
    case ADDR_RE: {
        regex_t *re = a->re_valid ? (regex_t *)&a->re : last_re;
        if (!re) die("no previous regular expression");
        return regexec(re, pattern_space.buf, 0, NULL, 0) == 0;
    }
    default: return false;
    }
}

static bool cmd_selected(cmd_t *c)
{
    bool sel;
    if (c->naddr == 0) {
        sel = true;
    } else if (c->naddr == 1) {
        sel = addr_match(&c->addr[0], line_number, last_line);
    } else {
        /* two-address range */
        if (c->in_range) {
            sel = true;
            if (addr_match(&c->addr[1], line_number, last_line)) {
                c->in_range = false;
            } else if (c->addr[1].type == ADDR_LINE &&
                       line_number > c->addr[1].lineno) {
                /* past end line: close range */
                c->in_range = false;
            }
        } else {
            if (addr_match(&c->addr[0], line_number, last_line)) {
                /* check if addr2 is already past */
                if (c->addr[1].type == ADDR_LINE &&
                    c->addr[1].lineno <= line_number) {
                    sel = true;  /* one line only */
                    c->in_range = false;
                } else {
                    c->in_range = true;
                    c->range_start = line_number;
                    sel = true;
                }
            } else {
                sel = false;
            }
        }
    }
    return c->negated ? !sel : sel;
}

/* ------------------------------------------------------------------ */
/*  l command                                                           */
/* ------------------------------------------------------------------ */

#define L_WIDTH 70

static void cmd_l(void)
{
    const char *s   = pattern_space.buf;
    int         col = 0;

    while (*s) {
        char esc[5] = {0};
        int  elen   = 0;
        unsigned char ch = (unsigned char)*s++;

        switch (ch) {
        case '\\': esc[0]='\\'; esc[1]='\\'; elen=2; break;
        case '\a': esc[0]='\\'; esc[1]='a';  elen=2; break;
        case '\b': esc[0]='\\'; esc[1]='b';  elen=2; break;
        case '\f': esc[0]='\\'; esc[1]='f';  elen=2; break;
        case '\r': esc[0]='\\'; esc[1]='r';  elen=2; break;
        case '\t': esc[0]='\\'; esc[1]='t';  elen=2; break;
        case '\v': esc[0]='\\'; esc[1]='v';  elen=2; break;
        default:
            if (isprint(ch)) { esc[0]=(char)ch; elen=1; }
            else { snprintf(esc, sizeof esc, "\\%03o", ch); elen=4; }
            break;
        }
        /* fold if needed */
        if (col + elen >= L_WIDTH) {
            putchar('\\'); putchar('\n'); col = 0;
        }
        fwrite(esc, 1, elen, stdout);
        col += elen;
    }
    if (col + 1 >= L_WIDTH) { putchar('\\'); putchar('\n'); col = 0; }
    putchar('$'); putchar('\n');
}

/* ------------------------------------------------------------------ */
/*  s command                                                           */
/* ------------------------------------------------------------------ */

#define MAX_SUB 10

static bool do_subst(cmd_t *c)
{
    regex_t *re = c->s_re_valid ? &c->s_re : last_re;
    if (!re) die("no previous regular expression");

    const char *src    = pattern_space.buf;
    size_t      srclen = pattern_space.len;
    dstr_t      result; dstr_init(&result);

    bool        changed = false;
    int         occur   = 0;
    size_t      pos     = 0;

    while (pos <= srclen) {
        regmatch_t m[MAX_SUB];
        int rc = regexec(re, src + pos, MAX_SUB, m, pos > 0 ? REG_NOTBOL : 0);
        if (rc != 0) {
            /* no more matches */
            dstr_append(&result, src + pos, srclen - pos);
            break;
        }
        if (m[0].rm_so < 0) {
            dstr_append(&result, src + pos, srclen - pos);
            break;
        }

        occur++;

        bool apply = (c->s_nth == -1) || (occur == c->s_nth);

        /* append everything before the match */
        dstr_append(&result, src + pos, (size_t)m[0].rm_so);

        if (apply) {
            changed = true;
            /* build replacement */
            const char *r = c->s_repl;
            while (*r) {
                if (*r == '&') {
                    dstr_append(&result, src + pos + m[0].rm_so,
                                (size_t)(m[0].rm_eo - m[0].rm_so));
                    r++;
                } else if (*r == '\\' && *(r+1) >= '1' && *(r+1) <= '9') {
                    int idx = *(r+1) - '0';
                    if (idx < MAX_SUB && m[idx].rm_so >= 0)
                        dstr_append(&result, src + pos + m[idx].rm_so,
                                    (size_t)(m[idx].rm_eo - m[idx].rm_so));
                    r += 2;
                } else if (*r == '\\' && *(r+1) == 'n') {
                    dstr_append(&result, "\n", 1); r += 2;
                } else if (*r == '\\' && *(r+1) != '\0') {
                    r++;  /* skip backslash */
                    dstr_append(&result, r++, 1);
                } else {
                    dstr_append(&result, r++, 1);
                }
            }
        } else {
            /* not this occurrence: keep original match */
            dstr_append(&result, src + pos + m[0].rm_so,
                        (size_t)(m[0].rm_eo - m[0].rm_so));
        }

        size_t newpos = pos + (size_t)m[0].rm_eo;
        if ((size_t)m[0].rm_eo == (size_t)m[0].rm_so) {
            /* zero-length match: advance one char to avoid infinite loop */
            if (pos + (size_t)m[0].rm_so < srclen)
                dstr_append(&result, src + pos + m[0].rm_so, 1);
            newpos = pos + (size_t)m[0].rm_so + 1;
        }
        pos = newpos;

        if (c->s_nth != -1) {
            /* not global: after applying (or skipping) the target occurrence,
               append rest and stop */
            if (apply) {
                dstr_append(&result, src + pos, srclen - pos);
                break;
            }
        }
    }

    if (!changed) {
        dstr_free(&result);
        return false;
    }

    /* update last_re */
    last_re = re;
    dstr_set(&pattern_space, result.buf, result.len);
    dstr_free(&result);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Execution engine                                                    */
/* ------------------------------------------------------------------ */

typedef enum { EX_NEXT, EX_NEW_CYCLE, EX_RESTART, EX_QUIT } ex_result_t;

/* We pass a function pointer or use a global input state */
typedef struct {
    const char **files;
    int          nfiles;
    int          cur;
    FILE        *fp;
    char        *peek_line;
    size_t       peek_cap;
    ssize_t      peek_n;
    bool         peeked;
    bool         eof;
} input_t;

static input_t input;

static bool input_open_next(void)
{
    while (input.cur < input.nfiles) {
        const char *name = (const char *)input.files[input.cur];
        if (strcmp(name, "-") == 0) {
            input.fp = stdin;
            return true;
        }
        input.fp = fopen(name, "r");
        if (input.fp) return true;
        fprintf(stderr, "sed: %s: %s\n", name, strerror(errno));
        exit(1);
    }
    input.eof = true;
    return false;
}

static bool input_getline(dstr_t *d)
{
    if (input.eof) return false;
    if (!input.fp && !input_open_next()) return false;

again:
    /* consume a previously peeked line */
    if (input.peeked) {
        input.peeked = false;
        if (input.peek_n <= 0) {
            /* current file exhausted during peek */
            if (input.fp != stdin) fclose(input.fp);
            input.fp = NULL; input.cur++;
            if (!input_open_next()) return false;
            goto again;
        }
        size_t len = (size_t)input.peek_n;
        if (len > 0 && input.peek_line[len-1] == '\n') len--;
        dstr_set(d, input.peek_line, len);
        free(input.peek_line); input.peek_line = NULL; input.peek_cap = 0;
        return true;
    }

    ssize_t n = getline(&input.peek_line, &input.peek_cap, input.fp);
    if (n < 0) {
        if (ferror(input.fp)) { perror("sed"); exit(1); }
        if (input.fp != stdin) fclose(input.fp);
        input.fp = NULL; input.cur++;
        if (!input_open_next()) return false;
        goto again;
    }
    size_t len = (size_t)n;
    if (len > 0 && input.peek_line[len-1] == '\n') len--;
    dstr_set(d, input.peek_line, len);
    /* don't keep line in peek_line since we consumed it directly */
    return true;
}

/* Peek at the next input line to determine if current line is last.
   Must be called AFTER input_getline has opened the current file. */
static bool is_last_line(void)
{
    if (input.eof) return true;
    if (input.peeked) {
        if (input.peek_n > 0) return false;
        /* peek_n <= 0 means current file at EOF; last iff no more files */
        return (input.cur + 1 >= input.nfiles);
    }

    if (!input.fp) return true;

    /* Read ahead one line */
    input.peek_n = getline(&input.peek_line, &input.peek_cap, input.fp);
    input.peeked = true;
    if (input.peek_n > 0) return false;
    /* EOF on current file */
    return (input.cur + 1 >= input.nfiles);
}

static ex_result_t execute(cmd_t *start);

static ex_result_t execute(cmd_t *start)
{
    cmd_t *c = start;
    while (c) {
        if (c->type == CMD_RBRACE) return EX_NEXT;  /* end of block */

        if (!cmd_selected(c)) {
            /* skip block */
            if (c->type == CMD_LBRACE) {
                c = c->end_brace ? c->end_brace->next : NULL;
                continue;
            }
            c = c->next;
            continue;
        }

        switch (c->type) {
        case CMD_LBRACE: {
            ex_result_t r = execute(c->next);
            if (r != EX_NEXT) return r;
            c = c->end_brace ? c->end_brace->next : NULL;
            continue;
        }
        case CMD_a:
            aq_add(false, c->text);
            break;
        case CMD_b:
            if (c->target) { c = c->target; continue; }
            return EX_NEXT;  /* branch to end of script */
        case CMD_c:
            /* for 2-addr, only output text at end of range (when in_range
               was just cleared by cmd_selected) or for 0/1-addr always */
            if (c->naddr < 2 || !c->in_range)
                { fputs(c->text, stdout); putchar('\n'); }
            dstr_set(&pattern_space, "", 0);
            return EX_NEW_CYCLE;
        case CMD_d:
            dstr_set(&pattern_space, "", 0);
            return EX_NEW_CYCLE;
        case CMD_D: {
            char *nl = strchr(pattern_space.buf, '\n');
            if (!nl) {
                dstr_set(&pattern_space, "", 0);
                return EX_NEW_CYCLE;
            }
            /* remove up to and including first newline */
            size_t off = (size_t)(nl - pattern_space.buf) + 1;
            memmove(pattern_space.buf, pattern_space.buf + off,
                    pattern_space.len - off + 1);
            pattern_space.len -= off;
            return EX_RESTART;
        }
        case CMD_g:
            dstr_set(&pattern_space, hold_space.buf, hold_space.len);
            break;
        case CMD_G:
            dstr_append(&pattern_space, "\n", 1);
            dstr_append(&pattern_space, hold_space.buf, hold_space.len);
            break;
        case CMD_h:
            dstr_set(&hold_space, pattern_space.buf, pattern_space.len);
            break;
        case CMD_H:
            dstr_append(&hold_space, "\n", 1);
            dstr_append(&hold_space, pattern_space.buf, pattern_space.len);
            break;
        case CMD_i:
            fputs(c->text, stdout); putchar('\n');
            break;
        case CMD_l:
            cmd_l();
            break;
        case CMD_n:
            if (!opt_n) {
                fputs(pattern_space.buf, stdout); putchar('\n');
            }
            aq_flush();
            sub_made = false;
            if (!input_getline(&pattern_space)) return EX_QUIT;
            line_number++;
            last_line = is_last_line();
            break;
        case CMD_N:
            aq_flush();
            {
                dstr_t next_line; dstr_init(&next_line);
                if (!input_getline(&next_line)) {
                    /* N with no next line: branch to end, quit */
                    dstr_free(&next_line);
                    return EX_QUIT;
                }
                line_number++;
                last_line = is_last_line();
                dstr_append(&pattern_space, "\n", 1);
                dstr_append(&pattern_space, next_line.buf, next_line.len);
                dstr_free(&next_line);
            }
            break;
        case CMD_p:
            fputs(pattern_space.buf, stdout); putchar('\n');
            break;
        case CMD_P: {
            char *nl = strchr(pattern_space.buf, '\n');
            if (nl) fwrite(pattern_space.buf, 1, (size_t)(nl - pattern_space.buf), stdout);
            else    fputs(pattern_space.buf, stdout);
            putchar('\n');
            break;
        }
        case CMD_q:
            if (!opt_n) {
                fputs(pattern_space.buf, stdout); putchar('\n');
            }
            aq_flush();
            return EX_QUIT;
        case CMD_r:
            aq_add(true, c->rfile);
            break;
        case CMD_s: {
            bool changed = do_subst(c);
            if (changed) {
                sub_made = true;
                if (c->s_print) { fputs(pattern_space.buf, stdout); putchar('\n'); }
                if (c->s_wfp)   { fputs(pattern_space.buf, c->s_wfp); putc('\n', c->s_wfp); }
            }
            break;
        }
        case CMD_t:
            if (sub_made) {
                sub_made = false;
                if (c->target) { c = c->target; continue; }
                return EX_NEXT;
            }
            break;
        case CMD_w:
            if (c->wfp) { fputs(pattern_space.buf, c->wfp); putc('\n', c->wfp); }
            break;
        case CMD_x: {
            dstr_t tmp;
            tmp          = pattern_space;
            pattern_space = hold_space;
            hold_space    = tmp;
            break;
        }
        case CMD_y: {
            for (size_t i = 0; i < pattern_space.len; i++) {
                for (size_t j = 0; j < c->y_len; j++) {
                    if (pattern_space.buf[i] == c->y_from[j]) {
                        pattern_space.buf[i] = c->y_to[j];
                        break;
                    }
                }
            }
            break;
        }
        case CMD_LABEL:
        case CMD_COMMENT:
        case CMD_EMPTY:
        case CMD_RBRACE:
            break;
        case CMD_EQUALS:
            printf("%ld\n", line_number);
            break;
        default:
            break;
        }
        c = c->next;
    }
    return EX_NEXT;
}

/* ------------------------------------------------------------------ */
/*  Main processing loop                                                */
/* ------------------------------------------------------------------ */

static void process(void)
{
    if (!input_getline(&pattern_space)) return;
    line_number++;

    for (;;) {
        last_line = is_last_line();
        sub_made  = false;

    restart:;
        ex_result_t r = execute(script);
        if (r == EX_RESTART) goto restart;
        if (r == EX_QUIT) return;

        if (r != EX_NEW_CYCLE && !opt_n) {
            fputs(pattern_space.buf, stdout); putchar('\n');
        }
        aq_flush();

        if (!input_getline(&pattern_space)) break;
        line_number++;
    }
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */

static void usage(void)
{
    fputs("Usage: sed [-En] script [file...]\n"
          "       sed [-En] -e script [-e script]... [-f script_file]... [file...]\n"
          "       sed [-En] [-e script]... -f script_file [-f script_file]... [file...]\n",
          stderr);
    exit(1);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    /* collect -e / -f in order */
    dstr_t combined; dstr_init(&combined);
    bool have_script = false;
    bool need_newline = false;  /* insert \n between -e sources */

    int opt;
    while ((opt = getopt(argc, argv, "Ene:f:")) != -1) {
        switch (opt) {
        case 'E': opt_E = true; cflags_base = REG_EXTENDED; break;
        case 'n': opt_n = true; break;
        case 'e':
            if (need_newline) dstr_append(&combined, "\n", 1);
            dstr_append(&combined, optarg, strlen(optarg));
            have_script   = true;
            need_newline  = true;
            break;
        case 'f': {
            FILE *fp = fopen(optarg, "r");
            if (!fp) {
                fprintf(stderr, "sed: %s: %s\n", optarg, strerror(errno));
                exit(1);
            }
            char buf[4096]; size_t n;
            if (need_newline) dstr_append(&combined, "\n", 1);
            while ((n = fread(buf, 1, sizeof buf, fp)) > 0)
                dstr_append(&combined, buf, n);
            fclose(fp);
            have_script  = true;
            need_newline = false; /* -f files already end with newline */
            break;
        }
        default: usage();
        }
    }

    if (!have_script) {
        if (optind >= argc) usage();
        dstr_append(&combined, argv[optind], strlen(argv[optind]));
        optind++;
    }

    check_hash_n(combined.buf);
    p = combined.buf;
    parse_commands(0);
    resolve();

    /* set up input */
    memset(&input, 0, sizeof input);
    static const char *stdin_arr[1] = { "-" };
    if (optind >= argc) {
        input.files  = stdin_arr;
        input.nfiles = 1;
    } else {
        input.files  = (const char **)(argv + optind);
        input.nfiles = argc - optind;
    }

    dstr_init(&pattern_space);
    dstr_init(&hold_space);

    process();

    /* flush any remaining append queue items */
    aq_flush();

    dstr_free(&pattern_space);
    dstr_free(&hold_space);
    dstr_free(&combined);

    return 0;
}
