/*
 * reader.c - reading the grammar: the declarations, the rules with
 * their actions (translating $$ and $n), and the programs section;
 * then packing the symbols and rules into the arrays used by the
 * table construction.
 */
#include "yacc.h"

Symbol **symbols;
int nsyms, ntokens, nnonterms;
Rule *rules;
int nrules;
int *ritem;
int nitems;
int start_symbol;
int max_token_value;

Chunk *chunks;
int nchunks;
char *union_body;
int union_line;
int have_tags;
char *programs;
int programs_line;
int expect_sr = -1, expect_rr = -1;

/* ---------- the input ---------- */

static char *src, *p, *end;
static int lineno = 1;
static int in_rules;

/* ---------- symbols ---------- */

#define HSIZE 1021
static Symbol *htab[HSIZE];
static Symbol *literals[256];
static Symbol **all;            /* in order of creation */
static int nall, call;
static Symbol *error_sym, *end_sym, *start_decl;
static int start_decl_line;
static int prec_level;
static int nmidrule;

static unsigned hash(const char *s)
{
    unsigned h = 0;
    while (*s)
        h = h * 31 + (unsigned char)*s++;
    return h % HSIZE;
}

static Symbol *new_symbol(const char *name)
{
    Symbol *s = xcalloc(1, sizeof *s);
    s->name = xstrdup(name);
    s->value = -1;
    s->line = lineno;
    s->index = -1;
    if (nall == call) {
        call = call ? call * 2 : 64;
        all = xrealloc(all, (size_t)call * sizeof *all);
    }
    all[nall++] = s;
    return s;
}

static Symbol *lookup(const char *name)
{
    unsigned h = hash(name);
    Symbol *s;
    for (s = htab[h]; s; s = s->next)
        if (!strcmp(s->name, name))
            return s;
    s = new_symbol(name);
    s->next = htab[h];
    htab[h] = s;
    return s;
}

static char *literal_name(int c)
{
    char buf[16];
    switch (c) {
    case '\n': return xstrdup("'\\n'");
    case '\t': return xstrdup("'\\t'");
    case '\v': return xstrdup("'\\v'");
    case '\b': return xstrdup("'\\b'");
    case '\r': return xstrdup("'\\r'");
    case '\f': return xstrdup("'\\f'");
    case '\a': return xstrdup("'\\a'");
    case '\\': return xstrdup("'\\\\'");
    case '\'': return xstrdup("'\\''");
    }
    if (c >= 0x20 && c < 0x7f)
        snprintf(buf, sizeof buf, "'%c'", c);
    else
        snprintf(buf, sizeof buf, "'\\%03o'", c);
    return xstrdup(buf);
}

static Symbol *literal(int c)
{
    if (!literals[c]) {
        char *n = literal_name(c);
        Symbol *s = new_symbol(n);
        free(n);
        s->literal = 1;
        s->class = TERM;
        s->value = c;
        literals[c] = s;
    }
    return literals[c];
}

/* ---------- lexical analysis ---------- */

enum {
    T_EOF, T_MARK, T_LCURL, T_RCURL, T_TOKEN, T_LEFT, T_RIGHT, T_NONASSOC, T_TYPE,
    T_START, T_UNION, T_PREC, T_EXPECT, T_EXPECT_RR, T_IDENT, T_CIDENT, T_LITERAL,
    T_NUMBER, T_TAG, T_LBRACE, T_BAR, T_SEMI, T_OTHER
};

typedef struct Tok {
    int type;
    char *text;          /* name or tag */
    int num;             /* number or literal value */
    int line;
} Tok;

static Tok la;
static int have_la;

static void syntax(int line, const char *fmt, ...)
{
    va_list ap;
    char msg[512];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    error_at(line, "%s", msg);
    grammar_abort();
}

static int at_end(void)
{
    return p >= end;
}

static void skip_space(void)
{
    for (;;) {
        if (at_end())
            return;
        if (*p == '\n') {
            lineno++;
            p++;
        } else if (*p == ' ' || *p == '\t' || *p == '\f' || *p == '\r' || *p == '\v') {
            p++;
        } else if (p[0] == '/' && p + 1 < end && p[1] == '*') {
            int l = lineno;
            p += 2;
            while (!at_end() && !(p[0] == '*' && p + 1 < end && p[1] == '/')) {
                if (*p == '\n')
                    lineno++;
                p++;
            }
            if (at_end())
                syntax(l, "unterminated comment");
            p += 2;
        } else if (p[0] == '/' && p + 1 < end && p[1] == '/') {
            while (!at_end() && *p != '\n')
                p++;
        } else
            return;
    }
}

static int is_name_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.';
}

static int is_name_char(int c)
{
    return is_name_start(c) || (c >= '0' && c <= '9');
}

static int octal(int c)
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

/* one character of a literal, with escape sequences; p is at it */
static int literal_char(int line)
{
    int c = 0;
    if (*p != '\\')
        return (unsigned char)*p++;
    p++;
    if (at_end() || *p == '\n')
        syntax(line, "unterminated literal");
    switch (*p) {
    case 'n': p++; return '\n';
    case 't': p++; return '\t';
    case 'v': p++; return '\v';
    case 'b': p++; return '\b';
    case 'r': p++; return '\r';
    case 'f': p++; return '\f';
    case 'a': p++; return '\a';
    case '\\': p++; return '\\';
    case '\'': p++; return '\'';
    case '"': p++; return '"';
    case '?': p++; return '?';
    case 'x': {
        int n = 0, v;
        p++;
        while (!at_end() && (v = hexval((unsigned char)*p)) >= 0) {
            c = c * 16 + v;
            if (c > 255)
                syntax(line, "character literal value out of range");
            p++;
            n++;
        }
        if (!n)
            syntax(line, "\\x used with no following hex digits");
        return c;
    }
    }
    if (octal(*p)) {
        int n = 0;
        while (n < 3 && !at_end() && octal(*p)) {
            c = c * 8 + (*p - '0');
            p++;
            n++;
        }
        if (c > 255)
            syntax(line, "character literal value out of range");
        return c;
    }
    syntax(line, "unknown escape sequence \\%c in literal", *p);
    return 0;
}

/*
 * A literal; p is after the opening quote.  A single character gives
 * its value; a multi-character literal (a historical extension) gives
 * -1 and its name in *name.
 */
static int read_literal(int line, char quote, char **name)
{
    Str bytes = { 0 };
    for (;;) {
        if (at_end() || *p == '\n')
            syntax(line, "unterminated literal");
        if (*p == quote) {
            p++;
            break;
        }
        str_addc(&bytes, literal_char(line));
    }
    if (bytes.len == 0)
        syntax(line, "empty literal");
    if (memchr(bytes.s, 0, bytes.len))
        syntax(line, "the NUL character cannot be used in a literal");
    if (bytes.len == 1) {
        int c = (unsigned char)bytes.s[0];
        free(bytes.s);
        return c;
    }
    {
        Str n = { 0 };
        str_addc(&n, '"');
        for (size_t i = 0; i < bytes.len; i++) {
            unsigned char c = (unsigned char)bytes.s[i];
            if (c == '"' || c == '\\') {
                str_addc(&n, '\\');
                str_addc(&n, c);
            } else if (c < 0x20 || c >= 0x7f)
                str_printf(&n, "\\%03o", c);
            else
                str_addc(&n, c);
        }
        str_addc(&n, '"');
        free(bytes.s);
        *name = n.s;
    }
    return -1;
}

static const struct {
    const char *name;
    int type;
} keywords[] = {
    { "token", T_TOKEN }, { "term", T_TOKEN }, { "left", T_LEFT }, { "right", T_RIGHT },
    { "nonassoc", T_NONASSOC }, { "binary", T_NONASSOC }, { "type", T_TYPE },
    { "start", T_START }, { "union", T_UNION }, { "prec", T_PREC },
    { "expect", T_EXPECT }, { "expect-rr", T_EXPECT_RR },
};

static Tok lex_raw(void)
{
    Tok t;
    memset(&t, 0, sizeof t);
    skip_space();
    t.line = lineno;
    if (at_end()) {
        t.type = T_EOF;
        return t;
    }
    if (*p == 0)
        syntax(lineno, "NUL character in grammar");
    if (*p == '%') {
        char c = p + 1 < end ? p[1] : 0;
        p += 2;
        switch (c) {
        case '%': t.type = T_MARK; return t;
        case '{': t.type = T_LCURL; return t;
        case '}': t.type = T_RCURL; return t;
        case '<': t.type = T_LEFT; return t;
        case '>': t.type = T_RIGHT; return t;
        case '2': t.type = T_NONASSOC; return t;
        case '0': t.type = T_TOKEN; return t;
        case '=': t.type = T_PREC; return t;
        }
        p--;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            char kw[32];
            size_t n = 0;
            while (!at_end() && (isalpha((unsigned char)*p) || *p == '-' || *p == '_')) {
                if (n < sizeof kw - 1)
                    kw[n++] = (char)tolower((unsigned char)*p);
                p++;
            }
            kw[n] = 0;
            for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++)
                if (!strcmp(kw, keywords[i].name)) {
                    t.type = keywords[i].type;
                    return t;
                }
            syntax(t.line, "unknown directive %%%s", kw);
        }
        syntax(t.line, "unexpected character after %%");
    }
    if (is_name_start((unsigned char)*p)) {
        char *s = p;
        while (!at_end() && is_name_char((unsigned char)*p))
            p++;
        t.text = xstrndup(s, (size_t)(p - s));
        t.type = T_IDENT;
        if (in_rules) {
            /* a name followed by a colon starts a rule */
            char *save = p;
            int saveline = lineno;
            skip_space();
            if (!at_end() && *p == ':') {
                p++;
                t.type = T_CIDENT;
            } else {
                p = save;
                lineno = saveline;
            }
        }
        return t;
    }
    if (isdigit((unsigned char)*p)) {
        long v = 0;
        while (!at_end() && isdigit((unsigned char)*p)) {
            v = v * 10 + (*p - '0');
            if (v > INT_MAX / 2)
                syntax(t.line, "number too large");
            p++;
        }
        t.type = T_NUMBER;
        t.num = (int)v;
        return t;
    }
    switch (*p) {
    case '\'':
    case '"': {
        char quote = *p++;
        t.num = read_literal(t.line, quote, &t.text);
        t.type = T_LITERAL;
        if (in_rules) {
            char *save = p;
            int saveline = lineno;
            skip_space();
            if (!at_end() && *p == ':')
                syntax(t.line, "a literal cannot be the left-hand side of a rule");
            p = save;
            lineno = saveline;
        }
        return t;
    }
    case '<': {
        char *s;
        size_t n;
        p++;
        while (!at_end() && (*p == ' ' || *p == '\t'))
            p++;
        s = p;
        while (!at_end() && *p != '>' && *p != '\n')
            p++;
        if (at_end() || *p != '>')
            syntax(t.line, "unterminated type tag");
        n = (size_t)(p - s);
        while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
            n--;
        if (!n)
            syntax(t.line, "empty type tag");
        t.text = xstrndup(s, n);
        p++;
        t.type = T_TAG;
        return t;
    }
    case '{': p++; t.type = T_LBRACE; return t;
    case '|': p++; t.type = T_BAR; return t;
    case ';': p++; t.type = T_SEMI; return t;
    }
    t.type = T_OTHER;
    t.num = (unsigned char)*p++;
    return t;
}

static Tok lex(void)
{
    if (have_la) {
        have_la = 0;
        return la;
    }
    return lex_raw();
}

static Tok peek(void)
{
    if (!have_la) {
        la = lex_raw();
        have_la = 1;
    }
    return la;
}

static const char *tok_desc(Tok t)
{
    static char buf[64];
    switch (t.type) {
    case T_EOF: return "end of file";
    case T_MARK: return "%%";
    case T_LCURL: return "%{";
    case T_RCURL: return "%}";
    case T_TOKEN: return "%token";
    case T_LEFT: return "%left";
    case T_RIGHT: return "%right";
    case T_NONASSOC: return "%nonassoc";
    case T_TYPE: return "%type";
    case T_START: return "%start";
    case T_UNION: return "%union";
    case T_PREC: return "%prec";
    case T_EXPECT: return "%expect";
    case T_EXPECT_RR: return "%expect-rr";
    case T_IDENT:
    case T_CIDENT:
        snprintf(buf, sizeof buf, "\"%.40s\"", t.text);
        return buf;
    case T_LITERAL: return "a literal";
    case T_NUMBER: return "a number";
    case T_TAG: return "a type tag";
    case T_LBRACE: return "an action";
    case T_BAR: return "|";
    case T_SEMI: return ";";
    }
    if (t.num >= 0x20 && t.num < 0x7f)
        snprintf(buf, sizeof buf, "'%c'", t.num);
    else
        snprintf(buf, sizeof buf, "character \\%03o", t.num);
    return buf;
}

static Symbol *lit_symbol(Tok *t)
{
    Symbol *s;
    if (t->num >= 0)
        return literal(t->num);
    s = lookup(t->text);
    if (!s->literal) {
        s->literal = 2;
        s->class = TERM;
    }
    return s;
}

/* ---------- copying C code ---------- */

static void copy_quoted(Str *out)
{
    char q = *p;
    int l = lineno;
    str_addc(out, *p++);
    for (;;) {
        if (at_end() || *p == '\n')
            syntax(l, "unterminated %s", q == '"' ? "string literal" : "character constant");
        if (*p == '\\' && p + 1 < end) {
            if (p[1] == '\n')
                lineno++;
            str_add(out, p, 2);
            p += 2;
            continue;
        }
        str_addc(out, *p);
        if (*p++ == q)
            return;
    }
}

/* copies a comment if there is one at p; returns 1 if it did */
static int copy_comment(Str *out)
{
    if (p[0] != '/' || p + 1 >= end)
        return 0;
    if (p[1] == '*') {
        int l = lineno;
        str_add(out, p, 2);
        p += 2;
        for (;;) {
            if (at_end())
                syntax(l, "unterminated comment");
            if (p[0] == '*' && p + 1 < end && p[1] == '/') {
                str_add(out, p, 2);
                p += 2;
                return 1;
            }
            if (*p == '\n')
                lineno++;
            str_addc(out, *p++);
        }
    }
    if (p[1] == '/') {
        while (!at_end() && *p != '\n') {
            if (*p == '\\' && p + 1 < end && p[1] == '\n') {
                str_add(out, p, 2);
                p += 2;
                lineno++;
                continue;
            }
            str_addc(out, *p++);
        }
        return 1;
    }
    return 0;
}

/* %{ ... %} */
static void copy_code(int line)
{
    Str out = { 0 };
    for (;;) {
        if (at_end())
            syntax(line, "unterminated %%{ section");
        if (*p == '%' && p + 1 < end && p[1] == '}') {
            p += 2;
            break;
        }
        if (*p == '"' || *p == '\'') {
            copy_quoted(&out);
            continue;
        }
        if (copy_comment(&out))
            continue;
        if (*p == 0)
            syntax(lineno, "NUL character in grammar");
        if (*p == '\n')
            lineno++;
        str_addc(&out, *p++);
    }
    chunks = xrealloc(chunks, (size_t)(nchunks + 1) * sizeof *chunks);
    chunks[nchunks].is_union = 0;
    chunks[nchunks].text = out.s ? out.s : xstrdup("");
    chunks[nchunks].line = line;
    nchunks++;
}

/* a braced block (action or union body); p is after the '{' */
static char *copy_braces(int line, const char *what)
{
    Str out = { 0 };
    int depth = 1;
    str_addc(&out, '{');
    for (;;) {
        if (at_end())
            syntax(line, "unterminated %s", what);
        if (*p == '"' || *p == '\'') {
            copy_quoted(&out);
            continue;
        }
        if (copy_comment(&out))
            continue;
        if (*p == 0)
            syntax(lineno, "NUL character in grammar");
        if (*p == '\n')
            lineno++;
        if (*p == '{')
            depth++;
        else if (*p == '}' && --depth == 0) {
            str_addc(&out, *p++);
            return out.s;
        }
        str_addc(&out, *p++);
    }
}

/* ---------- declarations ---------- */

static void set_tag(Symbol *s, const char *tag, int line)
{
    if (!tag)
        return;
    if (s->tag && strcmp(s->tag, tag)) {
        error_at(line, "type of %s redeclared (<%s> and <%s>)", s->name, s->tag, tag);
        return;
    }
    if (!s->tag)
        s->tag = xstrdup(tag);
}

static void declare_list(int kind, int line)
{
    char *tag = NULL;
    int count = 0;
    int assoc = kind == T_LEFT ? LEFT : kind == T_RIGHT ? RIGHT : kind == T_NONASSOC ? NONASSOC : NOASSOC;
    Tok t = peek();
    if (t.type == T_TAG) {
        lex();
        tag = t.text;
        have_tags = 1;
    } else if (kind == T_TYPE)
        error_at(line, "%%type requires a <tag>");
    if (assoc)
        prec_level++;
    for (;;) {
        Symbol *s;
        int first;
        t = peek();
        if (t.type == T_IDENT) {
            lex();
            s = lookup(t.text);
        } else if (t.type == T_LITERAL) {
            lex();
            s = lit_symbol(&t);
            if (kind == T_TYPE)
                error_at(t.line, "a literal cannot be declared with %%type");
        } else
            break;
        count++;
        first = !s->declared;
        if (kind != T_TYPE) {
            s->class = TERM;
            s->declared = 1;
        }
        set_tag(s, tag, t.line);
        if (assoc) {
            if (s->prec)
                error_at(t.line, "precedence of %s redeclared", s->name);
            else {
                s->prec = prec_level;
                s->assoc = assoc;
            }
        }
        if (peek().type == T_NUMBER) {
            Tok n = lex();
            if (kind == T_TYPE)
                error_at(n.line, "a token number cannot be assigned with %%type");
            else if (!first || (s->value >= 0 && !s->literal))
                error_at(n.line, "token number of %s cannot be changed", s->name);
            else if (n.num == 0) {
                if (end_sym && end_sym != s)
                    error_at(n.line, "token number 0 is already assigned to %s", end_sym->name);
                else if (s == error_sym)
                    error_at(n.line, "token number 0 cannot be assigned to error");
                else {
                    end_sym = s;
                    s->value = 0;
                }
            } else
                s->value = n.num;
            if (s->literal && s->value != 0)
                s->declared = 2;    /* explicitly renumbered */
        }
    }
    if (!count)
        error_at(line, "no symbols in declaration");
    free(tag);
}

static void parse_declarations(void)
{
    for (;;) {
        Tok t = lex();
        switch (t.type) {
        case T_MARK:
            return;
        case T_EOF:
            syntax(t.line, "unexpected end of file: the declarations section must end with %%%%");
            return;
        case T_LCURL:
            copy_code(t.line);
            break;
        case T_TOKEN:
        case T_LEFT:
        case T_RIGHT:
        case T_NONASSOC:
        case T_TYPE:
            declare_list(t.type, t.line);
            break;
        case T_START: {
            Tok n = lex();
            if (n.type != T_IDENT)
                syntax(n.line, "%%start must be followed by a name");
            if (start_decl)
                error_at(n.line, "%%start declared more than once");
            else {
                start_decl = lookup(n.text);
                start_decl_line = n.line;
            }
            break;
        }
        case T_UNION: {
            Tok b = lex();
            char *body;
            if (b.type != T_LBRACE)
                syntax(b.line, "%%union must be followed by {");
            body = copy_braces(b.line, "%union");
            if (union_body) {
                error_at(t.line, "%%union declared more than once");
                break;
            }
            union_body = body;
            union_line = b.line;
            chunks = xrealloc(chunks, (size_t)(nchunks + 1) * sizeof *chunks);
            chunks[nchunks].is_union = 1;
            chunks[nchunks].text = body;
            chunks[nchunks].line = b.line;
            nchunks++;
            break;
        }
        case T_EXPECT:
        case T_EXPECT_RR: {
            Tok n = lex();
            if (n.type != T_NUMBER)
                syntax(n.line, "%s must be followed by a number", tok_desc(t));
            if (t.type == T_EXPECT)
                expect_sr = n.num;
            else
                expect_rr = n.num;
            break;
        }
        default:
            syntax(t.line, "unexpected %s in the declarations section", tok_desc(t));
        }
    }
}

/* ---------- rules ---------- */

typedef struct Raw {
    Symbol *lhs;
    Symbol **rhs;
    int n;
    Symbol *prec;
    char *action;
    int action_line;
    int line;
} Raw;

static Raw *raws;
static int nraws, craws;

typedef struct Elem {
    Symbol *sym;         /* or NULL for an action */
    char *act;
    int line;
} Elem;

static Raw *new_raw(void)
{
    if (nraws == craws) {
        craws = craws ? craws * 2 : 64;
        raws = xrealloc(raws, (size_t)craws * sizeof *raws);
    }
    memset(&raws[nraws], 0, sizeof raws[0]);
    return &raws[nraws++];
}

/* the type of component n (1-based) of a rule body, NULL if none */
static const char *component_tag(Elem *e, int n)
{
    return e[n - 1].sym->tag;
}

/*
 * Translates $$, $n, $<tag>$, $<tag>n in an action.  'e' holds the L
 * components before the action; lhs is NULL for a mid-rule action.
 */
static char *translate(const char *code, int line, Elem *e, int L, Symbol *lhs, Symbol *mid)
{
    Str out = { 0 };
    const char *q = code;
    int l = line;
    while (*q) {
        char c = *q;
        if (c == '"' || c == '\'') {
            char quote = c;
            str_addc(&out, *q++);
            while (*q && *q != quote) {
                if (*q == '\\' && q[1]) {
                    if (q[1] == '\n')
                        l++;
                    str_add(&out, q, 2);
                    q += 2;
                    continue;
                }
                str_addc(&out, *q++);
            }
            if (*q)
                str_addc(&out, *q++);
            continue;
        }
        if (c == '/' && q[1] == '*') {
            const char *e2 = strstr(q + 2, "*/");
            size_t n = e2 ? (size_t)(e2 + 2 - q) : strlen(q);
            for (size_t i = 0; i < n; i++)
                if (q[i] == '\n')
                    l++;
            str_add(&out, q, n);
            q += n;
            continue;
        }
        if (c == '/' && q[1] == '/') {
            while (*q && *q != '\n')
                str_addc(&out, *q++);
            continue;
        }
        if (c == '\n')
            l++;
        if (c == '$') {
            const char *r = q + 1;
            char *tag = NULL;
            if (*r == '<') {
                const char *s = r + 1, *t2 = s;
                while (*t2 && *t2 != '>' && *t2 != '\n')
                    t2++;
                if (*t2 != '>') {
                    error_at(l, "unterminated type tag in $<...>");
                    str_addc(&out, *q++);
                    continue;
                }
                {
                    size_t n = (size_t)(t2 - s);
                    while (n && (*s == ' ' || *s == '\t'))
                        s++, n--;
                    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t'))
                        n--;
                    tag = xstrndup(s, n);
                }
                r = t2 + 1;
            }
            if (*r == '$') {
                const char *t = tag;
                if (!t) {
                    if (lhs)
                        t = lhs->tag;
                    if (!t && have_tags) {
                        if (lhs)
                            error_at(l, "$$ of %s has no declared type", lhs->name);
                        else
                            error_at(l, "$$ of mid-rule action %s has no declared type; use $<tag>$", mid->name);
                    }
                }
                str_adds(&out, "(yyval");
                if (t) {
                    str_addc(&out, '.');
                    str_adds(&out, t);
                }
                str_addc(&out, ')');
                q = r + 1;
                free(tag);
                continue;
            }
            if (isdigit((unsigned char)*r) || (*r == '-' && isdigit((unsigned char)r[1]))) {
                int neg = 0;
                long n = 0;
                const char *t = tag;
                if (*r == '-') {
                    neg = 1;
                    r++;
                }
                while (isdigit((unsigned char)*r)) {
                    if (n < 100000000)
                        n = n * 10 + (*r - '0');
                    r++;
                }
                if (neg)
                    n = -n;
                if (n > L) {
                    error_at(l, "$%ld refers beyond the %d component%s before the action", n, L,
                             L == 1 ? "" : "s");
                    n = L;
                }
                if (!t) {
                    if (n >= 1)
                        t = component_tag(e, (int)n);
                    if (!t && have_tags) {
                        if (n >= 1)
                            error_at(l, "$%ld (%s) has no declared type", n, e[n - 1].sym->name);
                        else
                            error_at(l, "$%ld has no declared type; use $<tag>%ld", n, n);
                    }
                }
                str_printf(&out, "(yyvsp[%ld]", n - L);
                if (t) {
                    str_addc(&out, '.');
                    str_adds(&out, t);
                }
                str_addc(&out, ')');
                q = r;
                free(tag);
                continue;
            }
            if (tag) {
                error_at(l, "invalid $<%s> reference", tag);
                free(tag);
            }
        }
        str_addc(&out, *q++);
    }
    return out.s ? out.s : xstrdup("");
}

static void make_midrule(Elem *e, int i)
{
    char name[32];
    Symbol *mid;
    Raw *r;
    snprintf(name, sizeof name, "$$%d", ++nmidrule);
    mid = lookup(name);
    mid->class = NONTERM;
    mid->used = 1;
    mid->line = e[i].line;
    r = new_raw();
    r->lhs = mid;
    r->line = e[i].line;
    r->action = translate(e[i].act, e[i].line, e, i, NULL, mid);
    r->action_line = e[i].line;
    free(e[i].act);
    e[i].act = NULL;
    e[i].sym = mid;
}

static void finish_rule(Symbol *lhs, Elem *e, int n, Symbol *prec, int line)
{
    Raw *r;
    char *action = NULL;
    int action_line = 0;
    int ncomp = n;
    if (n && !e[n - 1].sym) {
        ncomp = n - 1;
        action = translate(e[n - 1].act, e[n - 1].line, e, ncomp, lhs, NULL);
        action_line = e[n - 1].line;
        free(e[n - 1].act);
    }
    r = new_raw();
    r->lhs = lhs;
    r->line = line;
    r->n = ncomp;
    r->rhs = xmalloc((size_t)(ncomp ? ncomp : 1) * sizeof(Symbol *));
    for (int i = 0; i < ncomp; i++)
        r->rhs[i] = e[i].sym;
    r->prec = prec;
    r->action = action;
    r->action_line = action_line;
    if (!action && have_tags && lhs->tag) {
        if (!ncomp)
            warning_at(line, "empty rule for typed nonterminal %s, and no action", lhs->name);
        else if (!r->rhs[0]->tag || strcmp(r->rhs[0]->tag, lhs->tag))
            error_at(line, "type clash on default action: <%s> != <%s>", lhs->tag,
                     r->rhs[0]->tag ? r->rhs[0]->tag : "");
    }
}

/* parses rules; returns at %% or end of file */
static void parse_rules(void)
{
    Tok t;
    Symbol *lhs;
    int rule_line;
    Elem *e = NULL;
    int ne = 0, ce = 0;

    in_rules = 1;
    t = lex();
    if (t.type != T_CIDENT) {
        if (t.type == T_MARK || t.type == T_EOF)
            syntax(t.line, "no grammar rules");
        syntax(t.line, "expected a rule (a name followed by ':'), found %s", tok_desc(t));
    }
    lhs = lookup(t.text);
    rule_line = t.line;
    for (;;) {
        Symbol *prec = NULL;
        ne = 0;
        if (lhs->class == TERM)
            error_at(rule_line, "token %s on the left-hand side of a rule", lhs->name);
        else
            lhs->class = NONTERM;
        lhs->used = 1;
        for (;;) {
            t = lex();
            if (t.type == T_IDENT || t.type == T_LITERAL) {
                Symbol *s = t.type == T_IDENT ? lookup(t.text) : lit_symbol(&t);
                s->used = 1;
                if (ne && !e[ne - 1].sym)
                    make_midrule(e, ne - 1);
                if (ne == ce) {
                    ce = ce ? ce * 2 : 16;
                    e = xrealloc(e, (size_t)ce * sizeof *e);
                }
                e[ne].sym = s;
                e[ne].act = NULL;
                e[ne].line = t.line;
                ne++;
            } else if (t.type == T_LBRACE) {
                char *code = copy_braces(t.line, "action");
                if (ne && !e[ne - 1].sym)
                    make_midrule(e, ne - 1);
                if (ne == ce) {
                    ce = ce ? ce * 2 : 16;
                    e = xrealloc(e, (size_t)ce * sizeof *e);
                }
                e[ne].sym = NULL;
                e[ne].act = code;
                e[ne].line = t.line;
                ne++;
            } else if (t.type == T_PREC) {
                Tok n = lex();
                Symbol *s = NULL;
                if (n.type == T_IDENT)
                    s = lookup(n.text);
                else if (n.type == T_LITERAL)
                    s = lit_symbol(&n);
                else
                    syntax(n.line, "%%prec must be followed by a token name or literal");
                if (s->class != TERM)
                    error_at(n.line, "%%prec %s: not a token", s->name);
                if (prec)
                    error_at(n.line, "more than one %%prec in a rule");
                prec = s;
            } else
                break;
        }
        finish_rule(lhs, e, ne, prec, rule_line);
        /* what ended the rule */
        while (t.type == T_SEMI) {
            t = lex();
        }
        if (t.type == T_BAR) {
            rule_line = t.line;
            continue;
        }
        if (t.type == T_CIDENT) {
            lhs = lookup(t.text);
            rule_line = t.line;
            continue;
        }
        if (t.type == T_MARK) {
            /* the programs section: the rest of the file */
            programs_line = lineno;
            programs = xstrndup(p, (size_t)(end - p));
            break;
        }
        if (t.type == T_EOF)
            break;
        if (t.type == T_IDENT && peek().type == T_EOF)
            syntax(t.line, "unexpected %s at end of file", tok_desc(t));
        syntax(t.line, "unexpected %s in the rules section", tok_desc(t));
    }
    free(e);
}

/* ---------- packing ---------- */

static void pack(void)
{
    int next = 257, i, k;
    Symbol *accept, *undef;
    int nt = 0, nn = 0;
    Symbol **terms, **nonterms;
    Symbol *start;

    /* classify */
    for (i = 0; i < nall; i++) {
        Symbol *s = all[i];
        if (s->class == UNKNOWN && s->used)
            error_at(s->line, "symbol %s is used, but is not defined as a token and has no rules", s->name);
    }
    if (start_decl) {
        start = start_decl;
        if (start->class == TERM) {
            error_at(start_decl_line, "the start symbol %s is a token", start->name);
            start = raws[nraws - 1].lhs;
        } else if (start->class != NONTERM) {
            error_at(start_decl_line, "the start symbol %s has no rules", start->name);
            start = raws[nraws - 1].lhs;
        }
    } else {
        /* the left-hand side of the first rule given by the user (not a mid-rule symbol) */
        start = NULL;
        for (i = 0; i < nraws; i++)
            if (raws[i].lhs->name[0] != '$') {
                start = raws[i].lhs;
                break;
            }
    }
    if (nerrors)
        grammar_abort();

    /* the end marker, error and $undefined */
    if (!end_sym) {
        end_sym = new_symbol("$end");
        end_sym->class = TERM;
        end_sym->value = 0;
    }
    if (error_sym->value < 0)
        error_sym->value = 256;
    undef = new_symbol("$undefined");
    undef->class = TERM;
    undef->value = -1;
    accept = new_symbol("$accept");
    accept->class = NONTERM;

    terms = xmalloc((size_t)nall * sizeof *terms);
    nonterms = xmalloc((size_t)nall * sizeof *nonterms);
    terms[nt++] = end_sym;
    terms[nt++] = error_sym;
    terms[nt++] = undef;
    nonterms[nn++] = accept;
    for (i = 0; i < nall; i++) {
        Symbol *s = all[i];
        if (s == end_sym || s == error_sym || s == undef || s == accept)
            continue;
        if (s->class == TERM)
            terms[nt++] = s;
        else if (s->class == NONTERM)
            nonterms[nn++] = s;
        else if (!s->used)
            warning_at(s->line, "%s is declared with %%type but never used", s->name);
    }
    /* token numbers */
    for (i = 0; i < nt; i++) {
        Symbol *s = terms[i];
        if (s->value < 0 && s != undef) {
            for (;;) {
                int clash = 0;
                for (k = 0; k < nt; k++)
                    if (terms[k]->value == next) {
                        clash = 1;
                        break;
                    }
                if (!clash)
                    break;
                next++;
            }
            s->value = next++;
        }
    }
    max_token_value = 0;
    for (i = 0; i < nt; i++) {
        if (terms[i]->value > max_token_value)
            max_token_value = terms[i]->value;
        for (k = 0; k < i; k++)
            if (terms[i] != undef && terms[k] != undef && terms[i]->value == terms[k]->value)
                error_at(terms[i]->line, "tokens %s and %s both have token number %d", terms[k]->name,
                         terms[i]->name, terms[i]->value);
    }
    if (nerrors)
        grammar_abort();

    ntokens = nt;
    nnonterms = nn;
    nsyms = nt + nn;
    symbols = xmalloc((size_t)nsyms * sizeof *symbols);
    for (i = 0; i < nt; i++) {
        symbols[i] = terms[i];
        terms[i]->index = i;
    }
    for (i = 0; i < nn; i++) {
        symbols[nt + i] = nonterms[i];
        nonterms[i]->index = nt + i;
    }
    free(terms);
    free(nonterms);
    start_symbol = start->index;

    /* rules */
    nrules = nraws + 1;
    rules = xcalloc((size_t)nrules, sizeof *rules);
    nitems = 3;
    for (i = 0; i < nraws; i++)
        nitems += raws[i].n + 1;
    ritem = xmalloc((size_t)nitems * sizeof *ritem);
    k = 0;
    rules[0].lhs = accept->index;
    rules[0].rhs = 0;
    rules[0].len = 2;
    ritem[k++] = start->index;
    ritem[k++] = end_sym->index;
    ritem[k++] = -1;
    for (i = 0; i < nraws; i++) {
        Raw *r = &raws[i];
        Rule *u = &rules[i + 1];
        u->lhs = r->lhs->index;
        u->rhs = k;
        u->len = r->n;
        u->line = r->line;
        u->action = r->action;
        u->action_line = r->action_line;
        for (int j = 0; j < r->n; j++) {
            ritem[k++] = r->rhs[j]->index;
            if (r->rhs[j]->class == TERM) {
                u->prec = r->rhs[j]->prec;
                u->assoc = r->rhs[j]->assoc;
            }
        }
        if (r->prec) {
            u->prec = r->prec->prec;
            u->assoc = r->prec->assoc;
        }
        ritem[k++] = -(i + 2);
        free(r->rhs);
    }
    nitems = k;
    free(raws);
}

void read_grammar(FILE *fp)
{
    Str in = { 0 };
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, fp)) > 0)
        str_add(&in, buf, n);
    if (ferror(fp))
        fatal("cannot read %s: %s", input_name, strerror(errno));
    if (!in.s)
        str_adds(&in, "");
    src = p = in.s;
    end = in.s + in.len;

    error_sym = lookup("error");
    error_sym->class = TERM;
    parse_declarations();
    parse_rules();
    pack();
    (void)src;
}
