/*
 * re.c - lex regular expressions: the pattern of a rule (start
 * conditions, ^, trailing context, $) and the extended regular
 * expressions with lex's additions (quoting, {name}, escapes) and
 * lex's precedence, in which an interval applies to the concatenation
 * before it.
 */
#include "lex.h"

CSet *sets;
int nsets;
static int csets;

Rule *rules;
int nrules;

SCond *sconds;
int nsconds;

/* ---------- character sets ---------- */

int add_set(const CSet *s)
{
    for (int i = nsets - 1; i >= 0 && i >= nsets - 64; i--)
        if (!memcmp(&sets[i], s, sizeof *s))
            return i;
    for (int i = 0; i < nsets - 64; i++)
        if (!memcmp(&sets[i], s, sizeof *s))
            return i;
    if (nsets == csets) {
        csets = csets ? csets * 2 : 64;
        sets = xrealloc(sets, (size_t)csets * sizeof *sets);
    }
    sets[nsets] = *s;
    return nsets++;
}

/* ---------- nodes ---------- */

static Node *mk(int type, Node *a, Node *b)
{
    Node *n = xcalloc(1, sizeof *n);
    n->type = type;
    n->a = a;
    n->b = b;
    return n;
}

static Node *mk_char(int c)
{
    CSet s;
    Node *n;
    memset(&s, 0, sizeof s);
    CS_ADD(&s, c);
    n = mk(N_SET, NULL, NULL);
    n->set = add_set(&s);
    return n;
}

static Node *mk_set(const CSet *s)
{
    Node *n = mk(N_SET, NULL, NULL);
    n->set = add_set(s);
    return n;
}

static Node *cat(Node *a, Node *b)
{
    if (!a)
        return b;
    if (!b)
        return a;
    return mk(N_CAT, a, b);
}

int fixed_length(Node *n)
{
    int a, b;
    switch (n->type) {
    case N_EMPTY: return 0;
    case N_SET: return 1;
    case N_CAT:
        a = fixed_length(n->a);
        b = fixed_length(n->b);
        return a < 0 || b < 0 ? -1 : a + b;
    case N_ALT:
        a = fixed_length(n->a);
        b = fixed_length(n->b);
        return a == b ? a : -1;
    case N_STAR:
    case N_PLUS:
    case N_QUEST:
        return fixed_length(n->a) == 0 ? 0 : -1;
    case N_REP:
        a = fixed_length(n->a);
        if (a == 0)
            return 0;
        return a < 0 || n->min != n->max ? -1 : a * n->min;
    }
    return -1;
}

/* ---------- definitions ---------- */

typedef struct Def {
    char *name, *subst;
    int line;
    struct Def *next;
} Def;
static Def *defs;

void define(const char *name, const char *subst, int line)
{
    for (Def *d = defs; d; d = d->next)
        if (!strcmp(d->name, name)) {
            error_at(line, "definition %s redefined", name);
            return;
        }
    Def *d = xcalloc(1, sizeof *d);
    d->name = xstrdup(name);
    d->subst = xstrdup(subst);
    d->line = line;
    d->next = defs;
    defs = d;
}

static Def *find_def(const char *name, size_t len)
{
    for (Def *d = defs; d; d = d->next)
        if (strlen(d->name) == len && !strncmp(d->name, name, len))
            return d;
    return NULL;
}

int find_scond(const char *name, size_t len)
{
    for (int i = 0; i < nsconds; i++)
        if (strlen(sconds[i].name) == len && !strncmp(sconds[i].name, name, len))
            return i;
    return -1;
}

/* ---------- the parser ---------- */

typedef struct P {
    const char *p;
    int line;
    int in_rule;             /* a rule's pattern (not a definition) */
    int depth;               /* parentheses */
    int defdepth;
    int failed;
} P;

static void perr(P *ps, const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    if (ps->failed)
        return;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    error_at(ps->line, "%s", msg);
    ps->failed = 1;
}

/* the end of the pattern: a blank or the end of the line */
static int at_end(P *ps)
{
    char c = *ps->p;
    if (c == 0 || c == '\n')
        return 1;
    return ps->in_rule && !ps->defdepth && (c == ' ' || c == '\t');
}

static int is_octal(int c)
{
    return c >= '0' && c <= '7';
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* an escape sequence; p is after the backslash */
static int escape(P *ps)
{
    int c = (unsigned char)*ps->p;
    if (c == 0 || c == '\n') {
        perr(ps, "backslash at the end of the pattern");
        return '\\';
    }
    ps->p++;
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'a': return '\a';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'r': return '\r';
    case 'v': return '\v';
    case 'x': {
        int v = 0, n = 0, h;
        while ((h = hexval((unsigned char)*ps->p)) >= 0) {
            v = v * 16 + h;
            if (v > 255) {
                perr(ps, "hexadecimal escape out of range");
                v = 255;
            }
            ps->p++;
            n++;
        }
        if (!n)
            return 'x';      /* \x without digits: the character x */
        return v;
    }
    }
    if (is_octal(c)) {
        int v = c - '0', n = 1;
        while (n < 3 && is_octal((unsigned char)*ps->p)) {
            v = v * 8 + (*ps->p - '0');
            ps->p++;
            n++;
        }
        if (v > 255)
            perr(ps, "octal escape out of range");
        return v & 255;
    }
    return c;
}

/* the POSIX locale's character classes */
static int in_class(const char *cls, size_t len, int c)
{
#define IS(name) (len == strlen(name) && !strncmp(cls, name, len))
    int upper = c >= 'A' && c <= 'Z', lower = c >= 'a' && c <= 'z', digit = c >= '0' && c <= '9';
    int alpha = upper || lower, print = c >= 0x20 && c < 0x7f;
    if (IS("alpha")) return alpha;
    if (IS("upper")) return upper;
    if (IS("lower")) return lower;
    if (IS("digit")) return digit;
    if (IS("xdigit")) return digit || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (IS("alnum")) return alpha || digit;
    if (IS("space")) return c == ' ' || (c >= '\t' && c <= '\r');
    if (IS("blank")) return c == ' ' || c == '\t';
    if (IS("cntrl")) return c < 0x20 || c == 0x7f;
    if (IS("print")) return print;
    if (IS("graph")) return print && c != ' ';
    if (IS("punct")) return print && c != ' ' && !alpha && !digit;
    return -1;
#undef IS
}

/* a bracket expression; p is after the '[' */
static Node *bracket(P *ps)
{
    CSet s;
    int neg = 0, first = 1;
    memset(&s, 0, sizeof s);
    if (*ps->p == '^') {
        neg = 1;
        ps->p++;
    }
    for (;;) {
        int lo, hi, is_range_end_ok = 1;
        char c = *ps->p;
        if (c == 0 || c == '\n') {
            perr(ps, "unterminated bracket expression");
            return NULL;
        }
        if (c == ']' && !first) {
            ps->p++;
            break;
        }
        first = 0;
        if (c == '[' && (ps->p[1] == ':' || ps->p[1] == '=' || ps->p[1] == '.')) {
            char kind = ps->p[1];
            const char *start = ps->p + 2, *e = start;
            while (*e && *e != '\n' && !(e[0] == kind && e[1] == ']'))
                e++;
            if (!*e || *e == '\n') {
                perr(ps, "unterminated [%c in a bracket expression", kind);
                return NULL;
            }
            ps->p = e + 2;
            if (kind == ':') {
                int any = 0;
                for (int ch = 0; ch < 256; ch++) {
                    int r = in_class(start, (size_t)(e - start), ch);
                    if (r < 0) {
                        perr(ps, "unknown character class [:%.*s:]", (int)(e - start), start);
                        return NULL;
                    }
                    if (r) {
                        CS_ADD(&s, ch);
                        any = 1;
                    }
                }
                (void)any;
                continue;
            }
            if (e - start != 1) {
                perr(ps, "[%c%.*s%c] is not a single character", kind, (int)(e - start), start, kind);
                return NULL;
            }
            lo = (unsigned char)*start;
            is_range_end_ok = kind == '.';
        } else if (c == '\\') {
            ps->p++;
            lo = escape(ps);
        } else {
            lo = (unsigned char)c;
            ps->p++;
        }
        hi = lo;
        /* a range, unless the '-' is last */
        if (*ps->p == '-' && ps->p[1] != ']' && ps->p[1] != 0 && ps->p[1] != '\n' && is_range_end_ok) {
            const char *save = ps->p;
            ps->p++;
            if (*ps->p == '[' && ps->p[1] == '.') {
                const char *start = ps->p + 2, *e = start;
                while (*e && *e != '\n' && !(e[0] == '.' && e[1] == ']'))
                    e++;
                if (!*e || *e == '\n' || e - start != 1) {
                    perr(ps, "bad collating symbol at the end of a range");
                    return NULL;
                }
                hi = (unsigned char)*start;
                ps->p = e + 2;
            } else if (*ps->p == '[' && (ps->p[1] == ':' || ps->p[1] == '=')) {
                ps->p = save;    /* '-' then a class: the '-' is literal */
                CS_ADD(&s, lo);
                continue;
            } else if (*ps->p == '\\') {
                ps->p++;
                hi = escape(ps);
            } else {
                hi = (unsigned char)*ps->p++;
            }
            if (hi < lo) {
                perr(ps, "range out of order in a bracket expression");
                return NULL;
            }
        }
        for (int ch = lo; ch <= hi; ch++)
            CS_ADD(&s, ch);
    }
    if (neg)
        for (int i = 0; i < 32; i++)
            s.b[i] = (unsigned char)~s.b[i];
    return mk_set(&s);
}

static Node *alt(P *ps);

static int is_name_start(int c)
{
    return isalpha(c) || c == '_';
}

static int is_name_char(int c)
{
    return isalnum(c) || c == '_' || c == '-';
}

/* an atom, or NULL with *empty set when there is none here */
static Node *atom(P *ps, int *none)
{
    char c = *ps->p;
    *none = 0;
    switch (c) {
    case '(': {
        Node *n;
        ps->p++;
        ps->depth++;
        n = alt(ps);
        ps->depth--;
        if (ps->failed)
            return NULL;
        if (*ps->p != ')') {
            perr(ps, "missing )");
            return NULL;
        }
        ps->p++;
        return n ? n : mk(N_EMPTY, NULL, NULL);
    }
    case '[':
        ps->p++;
        return bracket(ps);
    case '"': {
        Node *n = NULL;
        ps->p++;
        while (*ps->p != '"') {
            int ch;
            if (*ps->p == 0 || *ps->p == '\n') {
                perr(ps, "unterminated string");
                return NULL;
            }
            if (*ps->p == '\\') {
                ps->p++;
                ch = escape(ps);
            } else
                ch = (unsigned char)*ps->p++;
            n = cat(n, mk_char(ch));
        }
        ps->p++;
        return n ? n : mk(N_EMPTY, NULL, NULL);
    }
    case '.': {
        CSet s;
        ps->p++;
        memset(&s, 0xff, sizeof s);
        s.b['\n' >> 3] &= (unsigned char)~(1 << ('\n' & 7));
        return mk_set(&s);
    }
    case '\\':
        ps->p++;
        return mk_char(escape(ps));
    case '{':
        if (is_name_start((unsigned char)ps->p[1])) {
            const char *s = ps->p + 1, *e = s;
            Def *d;
            P sub;
            Node *n;
            while (is_name_char((unsigned char)*e))
                e++;
            if (*e != '}') {
                perr(ps, "bad definition reference {%.*s", (int)(e - s), s);
                return NULL;
            }
            d = find_def(s, (size_t)(e - s));
            if (!d) {
                perr(ps, "undefined definition {%.*s}", (int)(e - s), s);
                return NULL;
            }
            if (ps->defdepth > 50) {
                perr(ps, "definitions nested too deeply (a definition refers to itself?)");
                return NULL;
            }
            ps->p = e + 1;
            sub = *ps;
            sub.p = d->subst;
            sub.depth = 0;
            sub.defdepth = ps->defdepth + 1;
            n = alt(&sub);
            if (!sub.failed && *sub.p) {
                if (*sub.p == ')')
                    perr(&sub, "unmatched ) in definition %s", d->name);
                else if (*sub.p == '/')
                    perr(&sub, "trailing context in definition %s", d->name);
                else
                    perr(&sub, "syntax error in definition %s", d->name);
            }
            if (sub.failed) {
                ps->failed = 1;
                return NULL;
            }
            return n ? n : mk(N_EMPTY, NULL, NULL);
        }
        break;
    case ')':
    case '|':
        *none = 1;
        return NULL;
    case '/':
        if (ps->in_rule && !ps->defdepth) {
            *none = 1;
            return NULL;
        }
        break;
    case '$':
        /* the anchor at the end of the whole pattern */
        if (ps->in_rule && !ps->defdepth && !ps->depth) {
            const char *q = ps->p + 1;
            if (*q == 0 || *q == '\n' || *q == ' ' || *q == '\t') {
                *none = 1;
                return NULL;
            }
        }
        break;
    }
    if (at_end(ps)) {
        *none = 1;
        return NULL;
    }
    ps->p++;
    return mk_char((unsigned char)c);
}

/* a concatenation; an interval applies to everything before it */
static Node *seq(P *ps)
{
    Node *n = NULL;
    for (;;) {
        Node *a;
        int none;
        if (ps->failed)
            return NULL;
        if (at_end(ps))
            return n;
        if (*ps->p == '{' && isdigit((unsigned char)ps->p[1])) {
            int m = 0, x;
            const char *q = ps->p + 1;
            while (isdigit((unsigned char)*q)) {
                if (m < 100000)
                    m = m * 10 + (*q - '0');
                q++;
            }
            x = m;
            if (*q == ',') {
                q++;
                if (isdigit((unsigned char)*q)) {
                    x = 0;
                    while (isdigit((unsigned char)*q)) {
                        if (x < 100000)
                            x = x * 10 + (*q - '0');
                        q++;
                    }
                } else
                    x = -1;
            }
            if (*q != '}') {
                perr(ps, "bad interval expression");
                return NULL;
            }
            if (!n) {
                perr(ps, "interval expression with nothing to repeat");
                return NULL;
            }
            if (x >= 0 && x < m) {
                perr(ps, "bad interval: {%d,%d}", m, x);
                return NULL;
            }
            if (m > 1000 || x > 1000) {
                perr(ps, "interval count too large (at most 1000)");
                return NULL;
            }
            ps->p = q + 1;
            a = mk(N_REP, n, NULL);
            a->min = m;
            a->max = x;
            n = a;
            continue;
        }
        if (!n && (*ps->p == '*' || *ps->p == '+' || *ps->p == '?')) {
            /* nothing to repeat: an ordinary character */
            a = mk_char((unsigned char)*ps->p++);
        } else {
            a = atom(ps, &none);
            if (ps->failed)
                return NULL;
            if (none)
                return n;
        }
        while (*ps->p == '*' || *ps->p == '+' || *ps->p == '?') {
            int t = *ps->p == '*' ? N_STAR : *ps->p == '+' ? N_PLUS : N_QUEST;
            ps->p++;
            a = mk(t, a, NULL);
        }
        n = cat(n, a);
    }
}

static Node *alt(P *ps)
{
    Node *n = seq(ps);
    if (!n)
        n = mk(N_EMPTY, NULL, NULL);
    while (!ps->failed && *ps->p == '|') {
        Node *b;
        ps->p++;
        b = seq(ps);
        if (!b)
            b = mk(N_EMPTY, NULL, NULL);
        n = mk(N_ALT, n, b);
    }
    return ps->failed ? NULL : n;
}

int parse_rule_pattern(const char **pp, Rule *r, int line)
{
    P ps;
    memset(&ps, 0, sizeof ps);
    ps.p = *pp;
    ps.line = line;
    ps.in_rule = 1;
    r->line = line;
    r->sc = xcalloc((size_t)(nsconds ? nsconds : 1), 1);

    /* start conditions */
    if (*ps.p == '<') {
        const char *q = ps.p + 1;
        while (*q && (isalnum((unsigned char)*q) || *q == '_' || *q == ',' || *q == '*' || *q == '-'))
            q++;
        if (*q == '>' && q > ps.p + 1) {
            const char *s = ps.p + 1;
            while (s < q) {
                const char *e = s;
                while (e < q && *e != ',')
                    e++;
                if (e == s) {
                    perr(&ps, "empty start condition name");
                    break;
                }
                if (e - s == 1 && *s == '*') {
                    for (int i = 0; i < nsconds; i++)
                        r->sc[i] = 1;
                } else {
                    int k = find_scond(s, (size_t)(e - s));
                    if (k < 0)
                        perr(&ps, "undeclared start condition %.*s", (int)(e - s), s);
                    else
                        r->sc[k] = 1;
                }
                s = e < q ? e + 1 : e;
            }
            ps.p = q + 1;
        } else {
            /* not a start condition list: inclusive conditions and INITIAL */
            goto no_sc;
        }
    } else {
    no_sc:
        for (int i = 0; i < nsconds; i++)
            r->sc[i] = !sconds[i].exclusive;
    }
    if (ps.failed)
        return 0;
    if (*ps.p == '^') {
        r->bol = 1;
        ps.p++;
    }
    r->r = alt(&ps);
    if (ps.failed)
        return 0;
    if (*ps.p == ')') {
        perr(&ps, "unmatched )");
        return 0;
    }
    if (*ps.p == '/') {
        ps.p++;
        r->trailing = 1;
        if (*ps.p == '^') {
            perr(&ps, "^ in trailing context");
            return 0;
        }
        r->x = alt(&ps);
        if (ps.failed)
            return 0;
        if (*ps.p == '/') {
            perr(&ps, "trailing context used twice");
            return 0;
        }
        if (*ps.p == ')') {
            perr(&ps, "unmatched )");
            return 0;
        }
        if (*ps.p == '$') {
            perr(&ps, "$ cannot be used with trailing context");
            return 0;
        }
    } else if (*ps.p == '$') {
        ps.p++;
        r->trailing = 1;
        r->x = mk_char('\n');
    }
    if (!at_end(&ps)) {
        perr(&ps, "syntax error in pattern near \"%.20s\"", ps.p);
        return 0;
    }
    if (r->trailing) {
        r->rlen = fixed_length(r->r);
        r->xlen = fixed_length(r->x);
        r->re = mk(N_CAT, r->r, r->x);
    } else
        r->re = r->r;
    *pp = ps.p;
    return 1;
}
