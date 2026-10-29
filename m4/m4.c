/*
 * m4.c - m4: macro processor (POSIX.1-2024, XCU m4)
 *
 * Synopsis:
 *   m4 [-s] [-D name[=val]]... [-U name]... [file...]
 *
 * The input is a stack of blocks: the files (operands and include) and
 * the texts pushed back to be read again (expansions).  Tokens are, in
 * the order they are tried: comments (copied, not scanned), names
 * (letters, digits and underscores, not starting with a digit), quoted
 * strings (the outer quotes removed) and single characters.  A name that
 * is a macro is called, with arguments when a ( follows at once; its
 * expansion is pushed back and read again.  The built-in macros are
 * those of the standard.
 *
 * Choices where the standard leaves room:
 *  - Options may come after operands (as the standard allows); all the
 *    -D and -U are done in order before the first file is read.  -s
 *    writes "#line N "file"" (or "#line N" in the same file) before an
 *    output line that does not come from the next line of the input.
 *  - The built-ins whose behaviour is unspecified without "(" (define,
 *    ifelse, len, ...) are then not called: the name is just text.
 *  - define replaces the current definition only (pushdef'd ones stay).
 *    defn of a built-in gives a token that define and pushdef take as
 *    that built-in; anywhere else it is dropped.
 *  - changequote with one argument: that and the default end quote; an
 *    empty begin quote turns quoting off.  changecom with an empty
 *    argument turns comments off.
 *  - Numbers (eval, incr, decr, ...) are 32-bit, wrapping around; eval
 *    knows the C operators of the standard with their precedence, shifts
 *    count modulo 32, and division by zero is an error.  Leading and
 *    trailing blanks of a numeric argument are allowed.
 *  - divert takes numbers above 9 too (up to 1023); undivert with an
 *    argument that is not a number writes that file, unscanned.
 *  - len, index, substr and translit count bytes; translit takes ranges
 *    (a-z).  mkstemp gives the name quoted, so it is not rescanned.
 *  - m4exit discards the diversions and the m4wrap text.  The m4wrap
 *    texts are read in the order they were given.
 *  - dumpdef writes "name:<tab>text" (a built-in as <name>), sorted when
 *    there are no arguments; traceon writes "m4trace: -depth- name".
 *  - syscmd runs sh -c; standard output is flushed first.  sysval is the
 *    exit status, or 128 plus the signal number.
 *  - Errors (an unreadable file, a non-numeric argument, ...) write
 *    "m4: file:line: message" and make the exit status 1; m4 goes on.
 *    An end of file in a quoted string or an argument list ends m4.
 *    Usage errors exit with status 2.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- strings ---- */

struct buf {
    char *s;
    size_t n, cap;
};

static void bgrow(struct buf *b, size_t extra)
{
    if (b->n + extra + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 64;
        while (c < b->n + extra + 1)
            c *= 2;
        b->s = realloc(b->s, c);
        if (!b->s) {
            fprintf(stderr, "m4: %s\n", strerror(ENOMEM));
            exit(1);
        }
        b->cap = c;
    }
}

static void bputn(struct buf *b, const char *s, size_t n)
{
    bgrow(b, n);
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = '\0';
}

static void bputs(struct buf *b, const char *s)
{
    bputn(b, s, strlen(s));
}

static void bputc(struct buf *b, int c)
{
    char ch = (char)c;
    bputn(b, &ch, 1);
}

static char *xstrdup(const char *s)
{
    char *p = strdup(s);
    if (!p) {
        fprintf(stderr, "m4: %s\n", strerror(ENOMEM));
        exit(1);
    }
    return p;
}

static char *xstrndup(const char *s, size_t n)
{
    char *p = malloc(n + 1);
    if (!p) {
        fprintf(stderr, "m4: %s\n", strerror(ENOMEM));
        exit(1);
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ---- the input ---- */

struct block {
    FILE *f;                    /* a file, or NULL for text */
    char *name;                 /* of the file */
    long line;
    char *text;                 /* the text, and where reading is */
    size_t pos, len;
};

static struct block *stack;
static size_t depth, stack_cap;
static int status;
static bool sync_lines;

/* the innermost file, for messages and #line */
static const char *cur_name = "stdin";
static long cur_line = 1;

static void update_where(void)
{
    for (size_t i = depth; i-- > 0; )
        if (stack[i].f) {
            cur_name = stack[i].name;
            cur_line = stack[i].line;
            return;
        }
}

static void error(const char *fmt, const char *arg)
{
    update_where();
    fprintf(stderr, "m4: %s:%ld: ", cur_name, cur_line);
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    status = 1;
}

static struct block *push_block(void)
{
    if (depth == stack_cap) {
        stack_cap = stack_cap ? 2 * stack_cap : 64;
        stack = realloc(stack, stack_cap * sizeof *stack);
        if (!stack) {
            fprintf(stderr, "m4: %s\n", strerror(ENOMEM));
            exit(1);
        }
    }
    struct block *b = &stack[depth++];
    memset(b, 0, sizeof *b);
    return b;
}

/* push text to be read next */
static void push_text(const char *s, size_t n)
{
    if (n == 0)
        return;
    /* the top block is text that has been read up to pos: put s before
       what is left of it, when there is room */
    if (depth > 0 && !stack[depth - 1].f && stack[depth - 1].pos >= n) {
        struct block *b = &stack[depth - 1];
        b->pos -= n;
        memcpy(b->text + b->pos, s, n);
        return;
    }
    struct block *b = push_block();
    b->text = xstrndup(s, n);
    b->len = n;
}

static void push_file(FILE *f, const char *name)
{
    struct block *b = push_block();
    b->f = f;
    b->name = xstrdup(name);
    b->line = 1;
}

static void pop_block(void)
{
    struct block *b = &stack[--depth];
    if (b->f) {
        if (b->f != stdin)
            fclose(b->f);
        else
            clearerr(stdin);
        free(b->name);
    } else {
        free(b->text);
    }
}

/* the next input character, or EOF when all is read */
static int next(void)
{
    while (depth > 0) {
        struct block *b = &stack[depth - 1];
        if (b->f) {
            int c = getc(b->f);
            if (c != EOF) {
                if (c == '\n')
                    b->line++;
                return c;
            }
            if (ferror(b->f)) {
                error("read error: %s", strerror(errno));
            }
        } else if (b->pos < b->len) {
            return (unsigned char)b->text[b->pos++];
        }
        pop_block();
    }
    return EOF;
}

static void unread(int c)
{
    if (c == EOF)
        return;
    char ch = (char)c;
    if (depth > 0 && stack[depth - 1].f) {
        /* back into the file: count the line again */
        if (ungetc(c, stack[depth - 1].f) != EOF) {
            if (c == '\n')
                stack[depth - 1].line--;
            return;
        }
    }
    push_text(&ch, 1);
}

static int peek(void)
{
    int c = next();
    unread(c);
    return c;
}

/* does s come next (its first character c read already)?  if so it is
   read; if not, nothing more is */
static bool match(int c, const char *s)
{
    if (!*s || c != (unsigned char)s[0])
        return false;
    if (!s[1])
        return true;
    size_t n = strlen(s), i;
    char got[64];
    for (i = 1; i < n && i < sizeof got; i++) {
        int d = next();
        if (d != (unsigned char)s[i]) {
            unread(d);
            /* push back what matched, last first */
            while (i-- > 1)
                unread((unsigned char)got[i]);
            return false;
        }
        got[i] = (char)d;
    }
    return true;
}

/* ---- the output ---- */

#define MAXDIV 1024
static struct buf divs[MAXDIV];
static int divnum;
static bool at_line_start = true;
static long out_line;           /* the input line of the next output line */
static const char *out_name;

static void out_byte(int c)
{
    if (divnum < 0)
        return;
    if (divnum > 0) {
        bputc(&divs[divnum], c);
        return;
    }
    if (sync_lines && at_line_start) {
        update_where();
        if (!out_name || strcmp(out_name, cur_name) != 0) {
            printf("#line %ld \"%s\"\n", cur_line, cur_name);
            out_name = cur_name;
        } else if (out_line != cur_line) {
            printf("#line %ld\n", cur_line);
        }
        out_line = cur_line;
    }
    putchar(c);
    at_line_start = c == '\n';
    if (at_line_start)
        out_line++;
}

/* text for the output, without the built-in tokens */
static void out_text(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\001') {
            const char *e = memchr(s + i, '\002', n - i);
            if (e) {
                i = (size_t)(e - s);
                continue;
            }
        }
        out_byte((unsigned char)s[i]);
    }
}

/* ---- the macros ---- */

enum builtin {
    B_NONE, B_CHANGECOM, B_CHANGEQUOTE, B_DECR, B_DEFINE, B_DEFN, B_DIVERT,
    B_DIVNUM, B_DNL, B_DUMPDEF, B_ERRPRINT, B_EVAL, B_IFDEF, B_IFELSE,
    B_INCLUDE, B_INCR, B_INDEX, B_LEN, B_M4EXIT, B_M4WRAP, B_MKSTEMP,
    B_POPDEF, B_PUSHDEF, B_SHIFT, B_SINCLUDE, B_SUBSTR, B_SYSCMD, B_SYSVAL,
    B_TRACEON, B_TRACEOFF, B_TRANSLIT, B_UNDEFINE, B_UNDIVERT
};

static const struct {
    const char *name;
    enum builtin b;
    bool blind;                 /* only with arguments */
} builtins[] = {
    { "changecom", B_CHANGECOM, false }, { "changequote", B_CHANGEQUOTE, false },
    { "decr", B_DECR, true }, { "define", B_DEFINE, true }, { "defn", B_DEFN, true },
    { "divert", B_DIVERT, false }, { "divnum", B_DIVNUM, false }, { "dnl", B_DNL, false },
    { "dumpdef", B_DUMPDEF, false }, { "errprint", B_ERRPRINT, true },
    { "eval", B_EVAL, true }, { "ifdef", B_IFDEF, true }, { "ifelse", B_IFELSE, true },
    { "include", B_INCLUDE, true }, { "incr", B_INCR, true }, { "index", B_INDEX, true },
    { "len", B_LEN, true }, { "m4exit", B_M4EXIT, false }, { "m4wrap", B_M4WRAP, true },
    { "mkstemp", B_MKSTEMP, true }, { "popdef", B_POPDEF, true },
    { "pushdef", B_PUSHDEF, true }, { "shift", B_SHIFT, true },
    { "sinclude", B_SINCLUDE, true }, { "substr", B_SUBSTR, true },
    { "syscmd", B_SYSCMD, true }, { "sysval", B_SYSVAL, false },
    { "traceon", B_TRACEON, false }, { "traceoff", B_TRACEOFF, false },
    { "translit", B_TRANSLIT, true }, { "undefine", B_UNDEFINE, true },
    { "undivert", B_UNDIVERT, false },
};
#define NBUILTINS (sizeof builtins / sizeof builtins[0])

struct def {
    enum builtin b;             /* or B_NONE for text */
    char *text;
    struct def *prev;           /* pushdef'd */
};

struct macro {
    char *name;
    struct def *def;            /* NULL: not defined */
    bool traced;
    struct macro *next;
};

#define HASHSIZE 1021
static struct macro *table[HASHSIZE];
static bool trace_all;

static unsigned hash(const char *s)
{
    unsigned h = 0;
    while (*s)
        h = h * 31 + (unsigned char)*s++;
    return h % HASHSIZE;
}

static struct macro *lookup(const char *name, bool create)
{
    unsigned h = hash(name);
    for (struct macro *m = table[h]; m; m = m->next)
        if (strcmp(m->name, name) == 0)
            return m;
    if (!create)
        return NULL;
    struct macro *m = calloc(1, sizeof *m);
    if (!m)
        exit(1);
    m->name = xstrdup(name);
    m->next = table[h];
    table[h] = m;
    return m;
}

static const char *builtin_name(enum builtin b)
{
    for (size_t i = 0; i < NBUILTINS; i++)
        if (builtins[i].b == b)
            return builtins[i].name;
    return "?";
}

/* a built-in token, as defn makes it: \1name\2 alone */
static enum builtin token_builtin(const char *s)
{
    size_t n = strlen(s);
    if (n < 3 || s[0] != '\001' || s[n - 1] != '\002')
        return B_NONE;
    for (size_t i = 0; i < NBUILTINS; i++)
        if (strlen(builtins[i].name) == n - 2 && strncmp(builtins[i].name, s + 1, n - 2) == 0)
            return builtins[i].b;
    return B_NONE;
}

static void define(const char *name, const char *text, bool push)
{
    struct macro *m = lookup(name, true);
    struct def *d = malloc(sizeof *d);
    if (!d)
        exit(1);
    d->b = token_builtin(text);
    d->text = d->b == B_NONE ? xstrdup(text) : NULL;
    if (push || !m->def) {
        d->prev = m->def;
    } else {
        d->prev = m->def->prev;
        free(m->def->text);
        free(m->def);
    }
    m->def = d;
}

static void undefine(const char *name)
{
    struct macro *m = lookup(name, false);
    if (!m)
        return;
    while (m->def) {
        struct def *d = m->def;
        m->def = d->prev;
        free(d->text);
        free(d);
    }
}

static void popdef(const char *name)
{
    struct macro *m = lookup(name, false);
    if (m && m->def) {
        struct def *d = m->def;
        m->def = d->prev;
        free(d->text);
        free(d);
    }
}

/* ---- tokens ---- */

static char *lquote, *rquote, *bcomm, *ecomm;

enum { T_EOF, T_WORD, T_STRING, T_COMMENT, T_CHAR };

static bool is_word_start(int c)
{
    return c != EOF && (isalpha(c) || c == '_');
}

static bool is_word(int c)
{
    return c != EOF && (isalnum(c) || c == '_');
}

/* the next token into t (cleared first) */
static int token(struct buf *t)
{
    t->n = 0;
    bgrow(t, 1);
    t->s[0] = '\0';
    int c = next();
    if (c == EOF)
        return T_EOF;
    if (c == '\001') {
        /* a built-in token from defn, whole */
        bputc(t, c);
        while ((c = next()) != EOF) {
            bputc(t, c);
            if (c == '\002')
                break;
        }
        return T_STRING;
    }
    if (*bcomm && match(c, bcomm)) {
        bputs(t, bcomm);
        for (;;) {
            int d = next();
            if (d == EOF) {
                error("end of file in a comment%s", "");
                return T_COMMENT;
            }
            if (match(d, ecomm)) {
                bputs(t, ecomm);
                return T_COMMENT;
            }
            bputc(t, d);
        }
    }
    if (is_word_start(c)) {
        bputc(t, c);
        while (is_word(c = next()))
            bputc(t, c);
        unread(c);
        return T_WORD;
    }
    if (*lquote && match(c, lquote)) {
        int level = 1;
        for (;;) {
            int d = next();
            if (d == EOF) {
                error("end of file in a quoted string%s", "");
                exit(1);
            }
            if (match(d, rquote)) {
                if (--level == 0)
                    return T_STRING;
                bputs(t, rquote);
            } else if (match(d, lquote)) {
                level++;
                bputs(t, lquote);
            } else {
                bputc(t, d);
            }
        }
    }
    bputc(t, c);
    return T_CHAR;
}

/* ---- calling macros ---- */

struct args {
    char **v;
    int n;                      /* v[0] is the name, then n arguments */
};

static void expand_macro(struct macro *m, struct buf *sink);

/* a macro called while collecting arguments or at the top: its result
   is pushed back (rescanned) or, for text not to be rescanned, given to
   the sink (NULL: the output) */
static void give(struct buf *sink, const char *s, size_t n)
{
    if (sink)
        bputn(sink, s, n);
    else
        out_text(s, n);
}

/* collect the arguments after "(" */
static void collect(struct args *a)
{
    int cap = 8;
    a->v = realloc(a->v, (size_t)cap * sizeof *a->v);
    a->n = 0;
    struct buf arg = { 0 }, t = { 0 };
    bool skip = true;
    int paren = 0;
    bputs(&arg, "");
    for (;;) {
        int k = token(&t);
        if (k == T_EOF) {
            error("end of file in the arguments of %s", a->v[0]);
            exit(1);
        }
        if (k == T_WORD) {
            struct macro *m = lookup(t.s, false);
            if (m && m->def) {
                expand_macro(m, &arg);
                continue;
            }
        }
        if (k == T_CHAR) {
            int c = (unsigned char)t.s[0];
            if (skip && isspace(c))
                continue;
            if (paren == 0 && (c == ',' || c == ')')) {
                if (a->n + 2 >= cap) {
                    cap *= 2;
                    a->v = realloc(a->v, (size_t)cap * sizeof *a->v);
                }
                a->v[++a->n] = xstrdup(arg.s);
                arg.n = 0;
                arg.s[0] = '\0';
                skip = true;
                if (c == ')')
                    break;
                continue;
            }
            if (c == '(')
                paren++;
            else if (c == ')')
                paren--;
        }
        skip = false;
        bputn(&arg, t.s, t.n);
    }
    free(arg.s);
    free(t.s);
}

static const char *arg(const struct args *a, int i)
{
    return i <= a->n ? a->v[i] : "";
}

/* a number from an argument; false (with a message) if it is not one */
static bool numeric(const struct args *a, int i, long *v)
{
    const char *s = arg(a, i), *p = s;
    while (isspace((unsigned char)*p))
        p++;
    bool neg = false;
    if (*p == '-' || *p == '+')
        neg = *p++ == '-';
    if (!isdigit((unsigned char)*p))
        goto bad;
    unsigned long x = 0;
    while (isdigit((unsigned char)*p))
        x = (x * 10 + (unsigned long)(*p++ - '0')) & 0xffffffffUL;
    while (isspace((unsigned char)*p))
        p++;
    if (*p)
        goto bad;
    if (neg)
        x = (0 - x) & 0xffffffffUL;
    *v = x > 0x7fffffffUL ? (long)x - 0x100000000L : (long)x;
    return true;
bad:
    {
        char msg[200];
        snprintf(msg, sizeof msg, "%s: non-numeric argument `%%s'", a->v[0]);
        error(msg, s);
    }
    return false;
}

static void push_num(long v)
{
    char s[32];
    snprintf(s, sizeof s, "%ld", v);
    push_text(s, strlen(s));
}

static long wrap32(long v)
{
    unsigned long x = (unsigned long)v & 0xffffffffUL;
    return x > 0x7fffffffUL ? (long)x - 0x100000000L : (long)x;
}

/* ---- eval ---- */

static const char *ep;
static const char *eerr;
static int eskip;               /* in a branch not evaluated */

static void ews(void)
{
    while (isspace((unsigned char)*ep))
        ep++;
}

static long e_cond(void);

static long e_primary(void)
{
    ews();
    if (*ep == '(') {
        ep++;
        long v = e_cond();
        ews();
        if (*ep != ')') {
            if (!eerr)
                eerr = "missing )";
            return 0;
        }
        ep++;
        return v;
    }
    if (*ep == '+' ) { ep++; return e_primary(); }
    if (*ep == '-') { ep++; return wrap32(-e_primary()); }
    if (*ep == '~') { ep++; return wrap32(~e_primary()); }
    if (*ep == '!' && ep[1] != '=') { ep++; return !e_primary(); }
    if (!isdigit((unsigned char)*ep)) {
        if (!eerr)
            eerr = "bad expression";
        return 0;
    }
    int base = 10;
    if (ep[0] == '0' && (ep[1] == 'x' || ep[1] == 'X'))
        base = 16, ep += 2;
    else if (ep[0] == '0')
        base = 8;
    unsigned long v = 0;
    int digits = 0;
    for (;; ep++) {
        int c = (unsigned char)*ep, d;
        if (isdigit(c))
            d = c - '0';
        else if (isalpha(c))
            d = tolower(c) - 'a' + 10;
        else
            break;
        if (d >= base) {
            if (!eerr)
                eerr = "bad number";
            return 0;
        }
        v = (v * (unsigned long)base + (unsigned long)d) & 0xffffffffUL;
        digits++;
    }
    if (base == 16 && digits == 0 && !eerr)
        eerr = "bad number";
    return wrap32((long)v);
}

static long e_mul(void)
{
    long v = e_primary();
    for (;;) {
        ews();
        char op = *ep;
        if ((op != '*' && op != '/' && op != '%') || ep[1] == '=')
            return v;
        ep++;
        long r = e_primary();
        if (op == '*')
            v = wrap32((long)((unsigned long)v * (unsigned long)r));
        else if (r == 0) {
            if (!eskip && !eerr)
                eerr = "divide by zero";
            v = 0;
        } else if (v == -2147483648L && r == -1)
            v = op == '/' ? v : 0;
        else
            v = op == '/' ? v / r : v % r;
    }
}

static long e_add(void)
{
    long v = e_mul();
    for (;;) {
        ews();
        if (*ep == '+' && ep[1] != '+')
            ep++, v = wrap32(v + e_mul());
        else if (*ep == '-')
            ep++, v = wrap32(v - e_mul());
        else
            return v;
    }
}

static long e_shift(void)
{
    long v = e_add();
    for (;;) {
        ews();
        if (ep[0] == '<' && ep[1] == '<')
            ep += 2, v = wrap32((long)((unsigned long)v << (e_add() & 31)));
        else if (ep[0] == '>' && ep[1] == '>')
            ep += 2, v = v >> (e_add() & 31);
        else
            return v;
    }
}

static long e_rel(void)
{
    long v = e_shift();
    for (;;) {
        ews();
        if (ep[0] == '<' && ep[1] == '=')
            ep += 2, v = v <= e_shift();
        else if (ep[0] == '>' && ep[1] == '=')
            ep += 2, v = v >= e_shift();
        else if (ep[0] == '<' && ep[1] != '<')
            ep++, v = v < e_shift();
        else if (ep[0] == '>' && ep[1] != '>')
            ep++, v = v > e_shift();
        else
            return v;
    }
}

static long e_eq(void)
{
    long v = e_rel();
    for (;;) {
        ews();
        if (ep[0] == '=' && ep[1] == '=')
            ep += 2, v = v == e_rel();
        else if (ep[0] == '!' && ep[1] == '=')
            ep += 2, v = v != e_rel();
        else
            return v;
    }
}

static long e_band(void)
{
    long v = e_eq();
    for (;;) {
        ews();
        if (ep[0] == '&' && ep[1] != '&')
            ep++, v &= e_eq();
        else
            return v;
    }
}

static long e_bxor(void)
{
    long v = e_band();
    for (;;) {
        ews();
        if (*ep == '^')
            ep++, v ^= e_band();
        else
            return v;
    }
}

static long e_bor(void)
{
    long v = e_bxor();
    for (;;) {
        ews();
        if (ep[0] == '|' && ep[1] != '|')
            ep++, v |= e_bxor();
        else
            return v;
    }
}

static long e_land(void)
{
    long v = e_bor();
    for (;;) {
        ews();
        if (ep[0] == '&' && ep[1] == '&') {
            ep += 2;
            if (!v)
                eskip++;
            long r = e_bor();
            if (!v)
                eskip--;
            v = v && r;
        } else {
            return v;
        }
    }
}

static long e_cond(void)
{
    long v = e_land();
    for (;;) {
        ews();
        if (ep[0] == '|' && ep[1] == '|') {
            ep += 2;
            if (v)
                eskip++;
            long r = e_land();
            if (v)
                eskip--;
            v = v || r;
        } else {
            return v;
        }
    }
}

static void do_eval(const struct args *a)
{
    long radix = 10, width = 1;
    if (a->n >= 2 && *arg(a, 2) && !numeric(a, 2, &radix))
        return;
    if (a->n >= 3 && !numeric(a, 3, &width))
        return;
    if (radix < 2 || radix > 36) {
        error("eval: radix out of range: %s", arg(a, 2));
        return;
    }
    if (width < 0) {
        error("eval: negative width: %s", arg(a, 3));
        return;
    }
    ep = arg(a, 1);
    eerr = NULL;
    eskip = 0;
    long v = e_cond();
    ews();
    if (!eerr && *ep)
        eerr = "bad expression";
    if (eerr) {
        char msg[300];
        snprintf(msg, sizeof msg, "eval: %s in `%%s'", eerr);
        error(msg, arg(a, 1));
        return;
    }
    char digits[80];
    int n = 0;
    unsigned long u = v < 0 ? (unsigned long)(-v) : (unsigned long)v;
    do {
        int d = (int)(u % (unsigned long)radix);
        digits[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        u /= (unsigned long)radix;
    } while (u);
    struct buf b = { 0 };
    if (v < 0)
        bputc(&b, '-');
    for (long i = n; i < width; i++)
        bputc(&b, '0');
    while (n > 0)
        bputc(&b, digits[--n]);
    push_text(b.s, b.n);
    free(b.s);
}

/* ---- the built-ins ---- */

static struct buf wraps;        /* the m4wrap text, in order */
static int sysval;

static void quoted(struct buf *b, const char *s)
{
    bputs(b, lquote);
    bputs(b, s);
    bputs(b, rquote);
}

static void set_quotes(const char *l, const char *r)
{
    free(lquote);
    free(rquote);
    lquote = xstrdup(l);
    rquote = xstrdup(*l ? r : "");
}

static void set_comments(const char *b, const char *e)
{
    free(bcomm);
    free(ecomm);
    bcomm = xstrdup(b);
    ecomm = xstrdup(*b ? e : "");
}

static int cmp_macros(const void *a, const void *b)
{
    return strcmp((*(struct macro *const *)a)->name, (*(struct macro *const *)b)->name);
}

static void dump(struct macro *m)
{
    if (!m || !m->def)
        return;
    if (m->def->b != B_NONE)
        fprintf(stderr, "%s:\t<%s>\n", m->name, builtin_name(m->def->b));
    else
        fprintf(stderr, "%s:\t%s\n", m->name, m->def->text);
}

/* the characters of a translit argument, with ranges a-z */
static void expand_set(const char *s, struct buf *b)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i++) {
        if (i + 2 < n && s[i + 1] == '-') {
            unsigned char from = (unsigned char)s[i], to = (unsigned char)s[i + 2];
            if (from <= to)
                for (int c = from; c <= to; c++)
                    bputc(b, c);
            else
                for (int c = from; c >= to; c--)
                    bputc(b, c);
            i += 2;
        } else {
            bputc(b, (unsigned char)s[i]);
        }
    }
}

static void undivert_one(int n)
{
    if (n <= 0 || n >= MAXDIV || n == divnum)
        return;
    struct buf *d = &divs[n];
    if (d->n) {
        char *s = d->s;
        size_t len = d->n;
        d->s = NULL, d->n = d->cap = 0;
        if (divnum == 0)
            out_text(s, len);
        else if (divnum > 0)
            bputn(&divs[divnum], s, len);
        free(s);
    }
}

static void flush_all(void);

static void call_builtin(enum builtin b, struct args *a, bool parens, struct buf *sink)
{
    long v, w;
    struct buf r = { 0 };
    switch (b) {
    case B_NONE:
        break;
    case B_CHANGECOM:
        if (a->n == 0)
            set_comments("", "");
        else
            set_comments(arg(a, 1), a->n >= 2 ? arg(a, 2) : "\n");
        break;
    case B_CHANGEQUOTE:
        if (a->n == 0)
            set_quotes("`", "'");
        else
            set_quotes(arg(a, 1), a->n >= 2 ? arg(a, 2) : "'");
        break;
    case B_DECR:
    case B_INCR:
        if (numeric(a, 1, &v))
            push_num(wrap32(b == B_INCR ? v + 1 : v - 1));
        break;
    case B_DEFINE:
    case B_PUSHDEF:
        if (a->n >= 1)
            define(arg(a, 1), arg(a, 2), b == B_PUSHDEF);
        break;
    case B_DEFN:
        for (int i = 1; i <= a->n; i++) {
            struct macro *m = lookup(arg(a, i), false);
            if (!m || !m->def)
                continue;
            if (m->def->b != B_NONE) {
                bputc(&r, '\001');
                bputs(&r, builtin_name(m->def->b));
                bputc(&r, '\002');
            } else {
                quoted(&r, m->def->text);
            }
        }
        push_text(r.s, r.n);
        break;
    case B_DIVERT:
        v = 0;
        if (a->n >= 1 && *arg(a, 1) && !numeric(a, 1, &v))
            break;
        if (v >= MAXDIV) {
            error("divert: diversion number too large: %s", arg(a, 1));
            v = -1;
        }
        divnum = (int)v;
        break;
    case B_DIVNUM:
        push_num(divnum);
        break;
    case B_DNL:
        for (int c; (c = next()) != EOF && c != '\n'; )
            ;
        break;
    case B_DUMPDEF:
        fflush(stdout);
        if (a->n == 0) {
            size_t n = 0;
            struct macro **all = NULL;
            for (int h = 0; h < HASHSIZE; h++)
                for (struct macro *m = table[h]; m; m = m->next)
                    if (m->def) {
                        all = realloc(all, (n + 1) * sizeof *all);
                        all[n++] = m;
                    }
            if (n)
                qsort(all, n, sizeof *all, cmp_macros);
            for (size_t i = 0; i < n; i++)
                dump(all[i]);
            free(all);
        } else {
            for (int i = 1; i <= a->n; i++) {
                struct macro *m = lookup(arg(a, i), false);
                if (!m || !m->def)
                    error("dumpdef: undefined macro `%s'", arg(a, i));
                dump(m);
            }
        }
        break;
    case B_ERRPRINT:
        fflush(stdout);
        for (int i = 1; i <= a->n; i++)
            fprintf(stderr, "%s%s", i > 1 ? " " : "", arg(a, i));
        fflush(stderr);
        break;
    case B_EVAL:
        do_eval(a);
        break;
    case B_IFDEF: {
        struct macro *m = lookup(arg(a, 1), false);
        const char *s = m && m->def ? arg(a, 2) : arg(a, 3);
        push_text(s, strlen(s));
        break;
    }
    case B_IFELSE: {
        int i = 1;
        while (a->n - i + 1 >= 3) {
            if (strcmp(arg(a, i), arg(a, i + 1)) == 0) {
                push_text(arg(a, i + 2), strlen(arg(a, i + 2)));
                break;
            }
            if (a->n - i + 1 <= 5) {
                push_text(arg(a, i + 3), strlen(arg(a, i + 3)));
                break;
            }
            i += 3;
        }
        break;
    }
    case B_INCLUDE:
    case B_SINCLUDE: {
        FILE *f = fopen(arg(a, 1), "r");
        if (!f) {
            if (b == B_INCLUDE) {
                char msg[200];
                snprintf(msg, sizeof msg, "cannot open `%%s': %s", strerror(errno));
                error(msg, arg(a, 1));
            }
            break;
        }
        push_file(f, arg(a, 1));
        break;
    }
    case B_INDEX: {
        const char *p = strstr(arg(a, 1), arg(a, 2));
        push_num(p ? (long)(p - arg(a, 1)) : -1);
        break;
    }
    case B_LEN:
        push_num((long)strlen(arg(a, 1)));
        break;
    case B_M4EXIT:
        v = 0;
        if (a->n >= 1 && *arg(a, 1) && !numeric(a, 1, &v))
            v = 1;
        if (v == 0 && status)
            v = status;
        fflush(stdout);
        exit((int)(v & 255));
    case B_M4WRAP:
        bputs(&wraps, arg(a, 1));
        break;
    case B_MKSTEMP: {
        char *t = xstrdup(arg(a, 1));
        int fd = mkstemp(t);
        if (fd < 0) {
            char msg[200];
            snprintf(msg, sizeof msg, "mkstemp: cannot create a file from `%%s': %s", strerror(errno));
            error(msg, arg(a, 1));
        } else {
            close(fd);
            give(sink, t, strlen(t));
        }
        free(t);
        break;
    }
    case B_POPDEF:
        for (int i = 1; i <= a->n; i++)
            popdef(arg(a, i));
        break;
    case B_SHIFT:
        for (int i = 2; i <= a->n; i++) {
            if (i > 2)
                bputc(&r, ',');
            quoted(&r, arg(a, i));
        }
        push_text(r.s, r.n);
        break;
    case B_SUBSTR: {
        const char *s = arg(a, 1);
        long len = (long)strlen(s);
        if (a->n < 2) {
            push_text(s, (size_t)len);
            break;
        }
        if (!numeric(a, 2, &v))
            break;
        w = len - v;
        if (a->n >= 3 && !numeric(a, 3, &w))
            break;
        if (v < 0 || v >= len || w <= 0)
            break;
        if (w > len - v)
            w = len - v;
        push_text(s + v, (size_t)w);
        break;
    }
    case B_SYSCMD: {
        fflush(stdout);
        FILE *p = popen(arg(a, 1), "r");
        if (!p) {
            error("syscmd: %s", strerror(errno));
            sysval = 127;
            break;
        }
        char tmp[4096];
        size_t k;
        while ((k = fread(tmp, 1, sizeof tmp, p)) > 0)
            bputn(&r, tmp, k);
        int st = pclose(p);
        sysval = st < 0 ? 127 : WIFEXITED(st) ? WEXITSTATUS(st)
               : WIFSIGNALED(st) ? 128 + WTERMSIG(st) : 127;
        give(sink, r.s ? r.s : "", r.n);
        break;
    }
    case B_SYSVAL:
        push_num(sysval);
        break;
    case B_TRACEON:
    case B_TRACEOFF:
        if (a->n == 0) {
            trace_all = b == B_TRACEON;
            for (int h = 0; h < HASHSIZE; h++)
                for (struct macro *m = table[h]; m; m = m->next)
                    m->traced = trace_all;
        } else {
            for (int i = 1; i <= a->n; i++)
                lookup(arg(a, i), true)->traced = b == B_TRACEON;
        }
        break;
    case B_TRANSLIT: {
        struct buf from = { 0 }, to = { 0 };
        expand_set(arg(a, 2), &from);
        expand_set(arg(a, 3), &to);
        for (const char *s = arg(a, 1); *s; s++) {
            const char *f = from.s ? memchr(from.s, *s, from.n) : NULL;
            if (!f)
                bputc(&r, (unsigned char)*s);
            else if ((size_t)(f - from.s) < to.n)
                bputc(&r, (unsigned char)to.s[f - from.s]);
        }
        free(from.s);
        free(to.s);
        push_text(r.s, r.n);
        break;
    }
    case B_UNDEFINE:
        for (int i = 1; i <= a->n; i++)
            undefine(arg(a, i));
        break;
    case B_UNDIVERT:
        if (a->n == 0) {
            for (int i = 1; i < MAXDIV; i++)
                undivert_one(i);
            break;
        }
        for (int i = 1; i <= a->n; i++) {
            const char *s = arg(a, i), *p = s;
            while (isspace((unsigned char)*p))
                p++;
            char *end;
            long n = strtol(p, &end, 10);
            while (isspace((unsigned char)*end))
                end++;
            if (*p && !*end && (isdigit((unsigned char)*p) || *p == '-' || *p == '+')) {
                undivert_one((int)n);
                continue;
            }
            /* a file, as it is */
            FILE *f = fopen(s, "r");
            if (!f) {
                char msg[200];
                snprintf(msg, sizeof msg, "undivert: cannot open `%%s': %s", strerror(errno));
                error(msg, s);
                continue;
            }
            char tmp[4096];
            size_t k;
            while ((k = fread(tmp, 1, sizeof tmp, f)) > 0) {
                if (divnum == 0)
                    out_text(tmp, k);
                else if (divnum > 0)
                    bputn(&divs[divnum], tmp, k);
            }
            fclose(f);
        }
        break;
    }
    (void)parens;
    free(r.s);
}

/* the defining text of a macro with the arguments in */
static void substitute(const char *text, const struct args *a, struct buf *r)
{
    for (const char *p = text; *p; p++) {
        if (*p != '$' || !p[1]) {
            bputc(r, (unsigned char)*p);
            continue;
        }
        char c = p[1];
        if (isdigit((unsigned char)c)) {
            int i = c - '0';
            if (i == 0)
                bputs(r, a->v[0]);
            else
                bputs(r, arg(a, i));
            p++;
        } else if (c == '#') {
            char s[16];
            snprintf(s, sizeof s, "%d", a->n);
            bputs(r, s);
            p++;
        } else if (c == '*' || c == '@') {
            for (int i = 1; i <= a->n; i++) {
                if (i > 1)
                    bputc(r, ',');
                if (c == '@')
                    quoted(r, a->v[i]);
                else
                    bputs(r, a->v[i]);
            }
            p++;
        } else {
            bputc(r, '$');
        }
    }
}

static int call_depth;

static void expand_macro(struct macro *m, struct buf *sink)
{
    struct def *d = m->def;
    struct args a = { NULL, 0 };
    bool parens = peek() == '(';
    if (d->b != B_NONE && !parens) {
        for (size_t i = 0; i < NBUILTINS; i++)
            if (builtins[i].b == d->b && builtins[i].blind) {
                give(sink, m->name, strlen(m->name));
                return;
            }
    }
    a.v = malloc(8 * sizeof *a.v);
    a.v[0] = xstrdup(m->name);
    call_depth++;
    if (parens) {
        next();
        collect(&a);
    }
    if (m->traced || trace_all) {
        fflush(stdout);
        fprintf(stderr, "m4trace: -%d- %s", call_depth, m->name);
        if (parens) {
            fputc('(', stderr);
            for (int i = 1; i <= a.n; i++)
                fprintf(stderr, "%s%s%s%s", i > 1 ? ", " : "", lquote, a.v[i], rquote);
            fputc(')', stderr);
        }
        fputc('\n', stderr);
    }
    /* the definition may change during the call: take it now */
    d = m->def;
    if (d) {
        if (d->b != B_NONE) {
            call_builtin(d->b, &a, parens, sink);
        } else {
            struct buf r = { 0 };
            substitute(d->text, &a, &r);
            push_text(r.s ? r.s : "", r.n);
            free(r.s);
        }
    }
    call_depth--;
    for (int i = 0; i <= a.n; i++)
        free(a.v[i]);
    free(a.v);
}

/* read and expand all of the input */
static void expand(void)
{
    struct buf t = { 0 };
    for (;;) {
        int k = token(&t);
        if (k == T_EOF)
            break;
        if (k == T_WORD) {
            struct macro *m = lookup(t.s, false);
            if (m && m->def) {
                expand_macro(m, NULL);
                continue;
            }
        }
        out_text(t.s, t.n);
    }
    free(t.s);
}

static void flush_all(void)
{
    divnum = 0;
    for (int i = 1; i < MAXDIV; i++)
        undivert_one(i);
}

static void usage(void)
{
    fprintf(stderr, "usage: m4 [-s] [-D name[=val]]... [-U name]... [file...]\n");
    exit(2);
}

static bool valid_name(const char *s)
{
    if (!is_word_start((unsigned char)*s))
        return false;
    for (s++; *s; s++)
        if (!is_word((unsigned char)*s))
            return false;
    return true;
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    set_quotes("`", "'");
    set_comments("#", "\n");
    for (size_t i = 0; i < NBUILTINS; i++) {
        struct macro *m = lookup(builtins[i].name, true);
        struct def *d = calloc(1, sizeof *d);
        if (!d)
            exit(1);
        d->b = builtins[i].b;
        m->def = d;
    }

    /* the options, also after operands; -D and -U in order */
    char **files = malloc((size_t)argc * sizeof *files);
    int nfiles = 0;
    bool opts = true;
    for (int i = 1; i < argc; i++) {
        char *s = argv[i];
        if (!opts || s[0] != '-' || s[1] == '\0') {
            files[nfiles++] = s;
            continue;
        }
        if (strcmp(s, "--") == 0) {
            opts = false;
            continue;
        }
        for (char *p = s + 1; *p; p++) {
            if (*p == 's') {
                sync_lines = true;
                continue;
            }
            if (*p != 'D' && *p != 'U') {
                fprintf(stderr, "m4: unknown option -%c\n", *p);
                usage();
            }
            char o = *p;
            char *v = p[1] ? p + 1 : (i + 1 < argc ? argv[++i] : NULL);
            if (!v) {
                fprintf(stderr, "m4: option -%c needs an argument\n", o);
                usage();
            }
            if (o == 'D') {
                char *eq = strchr(v, '=');
                char *name = eq ? xstrndup(v, (size_t)(eq - v)) : xstrdup(v);
                if (!valid_name(name)) {
                    fprintf(stderr, "m4: -D %s: not a name\n", v);
                    usage();
                }
                define(name, eq ? eq + 1 : "", false);
                free(name);
            } else {
                undefine(v);
            }
            break;
        }
    }
    if (nfiles == 0)
        files[nfiles++] = "-";

    for (int i = 0; i < nfiles; i++) {
        FILE *f;
        const char *name = files[i];
        if (strcmp(name, "-") == 0) {
            f = stdin;
            name = "stdin";
        } else if (!(f = fopen(name, "r"))) {
            fprintf(stderr, "m4: %s: %s\n", name, strerror(errno));
            status = 1;
            continue;
        }
        push_file(f, name);
        /* expand only this file now: what is pushed back stays above it */
        expand();
    }
    /* the m4wrap text, and what that wraps in turn */
    while (wraps.n) {
        char *w = wraps.s;
        size_t n = wraps.n;
        wraps.s = NULL, wraps.n = wraps.cap = 0;
        push_text(w, n);
        free(w);
        expand();
    }
    flush_all();
    if (fflush(stdout) != 0) {
        fprintf(stderr, "m4: standard output: %s\n", strerror(errno));
        return 1;
    }
    return status;
}
