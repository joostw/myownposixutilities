%{
/*
 * awk.l - the tokens of awk (XCU awk, "Lexical Conventions"), for the
 * grammar in awk.y.
 *
 * The program text is read from a memory stream, and the scanner keeps
 * the offset of each token in it for the context shown with syntax
 * errors.  Where the text after a token decides what the token is, the
 * scanner looks at the text itself rather than at further tokens:
 *   - a name immediately followed by "(" is a FUNC_NAME;
 *   - "|" is PIPE_GETLINE when the next token is getline: the grammar
 *     asks for that check (lex_pipe_getline) where the earlier parser
 *     looked at the token after "|", at the end of a concatenation;
 *   - a token at the outer level preceded by "}" (and blanks) gets a
 *     separator (ITEMSEP) before it, since a pattern-action item ending
 *     in "}" may be followed directly by another item.
 * "/" starts a regular expression unless the previous token ends an
 * operand (a number, string, name, ")", "]", ++/--, getline or a bare
 * built-in); the ")" that closes "if (", "while (" or "for (" does not,
 * nor do ++ and -- among the prefix operators of a "$" operand.
 * Names and the letters in them follow isalpha()/isalnum() in the
 * current locale.
 */
#include "awk.h"
#include "parse.h"
#include "awk.tab.h"

#define yylex awk_scan          /* the scanner proper; yylex() is below */

static const char *src;         /* the program text */
static size_t srclen;
static size_t off;              /* the offset of the next character */
static size_t tstart;           /* the offset of the current match */
static int cur_line = 1;
static int prev = TK_NEWLINE;   /* the previous token, for "/" */
static int prev_special_paren;  /* it was the ")" of if/while/for */
static int dollar_prefix;       /* in the prefix operators after "$" */
static int prev_dollar_incr;    /* the previous token is ++/-- there */
static FILE *lex_in;

static int BEG(void);
static int mk(int type);
static int name(void);
static int number(void);
static int string(void);
static int slash(int assign);
static void skip(size_t n);
static int is_name_char(int c);
%}
%%
[ \t\r\f\v]+            BEG();
\\\n                    { BEG(); cur_line++; }
\\\r\n                  { BEG(); cur_line++; }
#[^\n]*                 BEG();
\n                      { int t; BEG(); t = mk(TK_NEWLINE); cur_line++; return t; }
[0-9][0-9.eE+-]*        |
\.[0-9][0-9.eE+-]*      { BEG(); return number(); }
[A-Za-z_]               { BEG(); return name(); }
[\200-\377]             {
                            BEG();
                            if (isalpha((unsigned char)src[tstart]))
                                return name();
                            syntax_error_at(lex_la = &(Tok){ 0, tstart, cur_line, 0, 0, 0 },
                                            "invalid character");
                        }
\"([^"\\\n]|\\(.|\n))*\"  { BEG(); return string(); }
\"([^"\\\n]|\\(.|\n))*\\? {
                            /* the end of the line or of the program */
                            int line = cur_line;
                            BEG();
                            syntax_error_at(lex_la = &(Tok){ 0, tstart + (size_t)yyleng, line, 0, 0, 0 },
                                            "non-terminated string");
                        }
"/="                    { BEG(); return slash(1); }
"/"                     { BEG(); return slash(0); }
"{"                     { BEG(); return mk(TK_LBRACE); }
"}"                     { BEG(); return mk(TK_RBRACE); }
"("                     { BEG(); return mk(TK_LPAREN); }
")"                     { BEG(); return mk(TK_RPAREN); }
"["                     { BEG(); return mk(TK_LBRACKET); }
"]"                     { BEG(); return mk(TK_RBRACKET); }
","                     { BEG(); return mk(TK_COMMA); }
";"                     { BEG(); return mk(TK_SEMI); }
"?"                     { BEG(); return mk(TK_QUESTION); }
":"                     { BEG(); return mk(TK_COLON); }
"~"                     { BEG(); return mk(TK_TILDE); }
"$"                     { BEG(); return mk(TK_DOLLAR); }
"++"                    { BEG(); return mk(TK_INCR); }
"+="                    { BEG(); return mk(TK_ADD_ASSIGN); }
"+"                     { BEG(); return mk(TK_PLUS); }
"--"                    { BEG(); return mk(TK_DECR); }
"-="                    { BEG(); return mk(TK_SUB_ASSIGN); }
"-"                     { BEG(); return mk(TK_MINUS); }
"**="                   { BEG(); return mk(TK_POW_ASSIGN); }
"**"                    { BEG(); return mk(TK_CARET); }
"*="                    { BEG(); return mk(TK_MUL_ASSIGN); }
"*"                     { BEG(); return mk(TK_STAR); }
"%="                    { BEG(); return mk(TK_MOD_ASSIGN); }
"%"                     { BEG(); return mk(TK_PERCENT); }
"^="                    { BEG(); return mk(TK_POW_ASSIGN); }
"^"                     { BEG(); return mk(TK_CARET); }
"!="                    { BEG(); return mk(TK_NE); }
"!~"                    { BEG(); return mk(TK_NOMATCH); }
"!"                     { BEG(); return mk(TK_NOT); }
">="                    { BEG(); return mk(TK_GE); }
">>"                    { BEG(); return mk(TK_APPEND); }
">"                     { BEG(); return mk(TK_GT); }
"<="                    { BEG(); return mk(TK_LE); }
"<"                     { BEG(); return mk(TK_LT); }
"||"                    { BEG(); return mk(TK_OR); }
"|"                     { BEG(); return mk(TK_PIPE); }
"&&"                    { BEG(); return mk(TK_AND); }
"=="                    { BEG(); return mk(TK_EQ); }
"="                     { BEG(); return mk(TK_ASSIGN); }
.                       {
                            BEG();
                            syntax_error_at(lex_la = &(Tok){ 0, tstart, cur_line, 0, 0, 0 },
                                            "invalid character");
                        }
%%
#undef yylex

Tok *lex_la;

int yywrap(void)
{
    return 1;
}

/* the start of every match: where it is in the text */
static int BEG(void)
{
    tstart = off;
    off += (size_t)yyleng;
    return 0;
}

/* the token codes of the grammar for the TK_ codes */
static int ytoken(int t)
{
    switch (t) {
    case TK_NEWLINE: return NEWLINE;
    case TK_LBRACE: return '{';
    case TK_RBRACE: return '}';
    case TK_LPAREN: return '(';
    case TK_RPAREN: return ')';
    case TK_LBRACKET: return '[';
    case TK_RBRACKET: return ']';
    case TK_COMMA: return ',';
    case TK_SEMI: return ';';
    case TK_PLUS: return '+';
    case TK_MINUS: return '-';
    case TK_STAR: return '*';
    case TK_SLASH: return '/';
    case TK_PERCENT: return '%';
    case TK_CARET: return '^';
    case TK_NOT: return '!';
    case TK_GT: return '>';
    case TK_LT: return '<';
    case TK_PIPE: return '|';
    case TK_QUESTION: return '?';
    case TK_COLON: return ':';
    case TK_TILDE: return '~';
    case TK_DOLLAR: return '$';
    case TK_ASSIGN: case TK_ADD_ASSIGN: case TK_SUB_ASSIGN: case TK_MUL_ASSIGN:
    case TK_DIV_ASSIGN: case TK_MOD_ASSIGN: case TK_POW_ASSIGN:
        return ASSIGNOP;
    case TK_OR: return OR;
    case TK_AND: return AND;
    case TK_NOMATCH: return NOMATCH;
    case TK_EQ: case TK_LE: case TK_GE: case TK_NE:
        return RELOP;
    case TK_INCR: return INCR;
    case TK_DECR: return DECR;
    case TK_APPEND: return APPEND;
    case TK_NUMBER: return NUMBER;
    case TK_STRING: return STRING;
    case TK_ERE: return ERE;
    case TK_NAME: return NAME;
    case TK_FUNC_NAME: return FUNC_NAME;
    case TK_BUILTIN: return BUILTIN;
    case TK_BEGIN: return Begin;
    case TK_END: return End;
    case TK_BREAK: return Break;
    case TK_CONTINUE: return Continue;
    case TK_DELETE: return Delete;
    case TK_DO: return Do;
    case TK_ELSE: return Else;
    case TK_EXIT: return Exit;
    case TK_FOR: return For;
    case TK_FUNCTION: return Function;
    case TK_GETLINE: return Getline;
    case TK_IF: return If;
    case TK_IN: return In;
    case TK_NEXT: return Next;
    case TK_NEXTFILE: return Nextfile;
    case TK_PRINT: return Print;
    case TK_PRINTF: return Printf;
    case TK_RETURN: return Return;
    case TK_WHILE: return While;
    }
    return 0;
}

/* tokens kept between the scanner and the parser */
static Tok *cur;                /* the token being made */
static int cur_ytok;

static int mk(int type)
{
    cur = xcalloc(1, sizeof *cur);
    cur->type = type;
    cur->start = tstart;
    cur->line = cur_line;
    cur_ytok = ytoken(type);
    return 1;
}

/* the characters after the match that belong to the token; how many is
   known from the text, which may hold NULs (input() gives 0 for them) */
static void skip(size_t n)
{
    while (n-- > 0) {
        (void)input();
        off++;
    }
}

static int is_name_char(int c)
{
    return isalnum(c) || c == '_';
}

static int name(void)
{
    size_t e = tstart + 1, n;
    char *w;
    int t, bi = 0;
    while (e < srclen && is_name_char((unsigned char)src[e]))
        e++;
    skip(e - off);
    n = e - tstart;
    w = xmalloc(n + 1);
    memcpy(w, src + tstart, n);
    w[n] = 0;
    t = word_token(w, &bi);
    if (t && t != TK_BUILTIN) {
        free(w);
        return mk(t);
    }
    if (t == TK_BUILTIN) {
        mk(TK_BUILTIN);
        cur->bi = bi;
        cur->str = str_cstr(w);
        free(w);
        return 1;
    }
    mk(e < srclen && src[e] == '(' ? TK_FUNC_NAME : TK_NAME);
    cur->str = str_cstr(w);
    free(w);
    return 1;
}

static int number(void)
{
    size_t n = scan_number(src + tstart, srclen - tstart, '.', 0);
    yyless((int)n);
    off = tstart + n;
    mk(TK_NUMBER);
    cur->num = conv_number(src + tstart, n, '.');
    return 1;
}

/* a string constant: the escapes, and backslash-newline continuations */
static int string(void)
{
    size_t pos = tstart + 1, end = off - 1;     /* the closing quote */
    int line = cur_line;
    Buf b;
    buf_init(&b);
    while (pos < end) {
        int c = (unsigned char)src[pos];
        if (c == '\\' && pos + 1 < srclen) {
            int ch;
            size_t ni;
            if (src[pos + 1] == '\n') {         /* continuation */
                pos += 2;
                cur_line++;
                continue;
            }
            ni = (size_t)parse_escape(src, pos + 1, srclen, &ch);
            if (ch < 0) {
                /* undefined escape: keep backslash and character */
                buf_addc(&b, '\\');
                buf_addc(&b, src[pos + 1]);
                pos += 2;
            } else {
                buf_addc(&b, ch);
                pos = ni;
            }
            continue;
        }
        buf_addc(&b, c);
        pos++;
    }
    mk(TK_STRING);
    cur->line = line;
    cur->str = buf_str(&b);
    buf_free(&b);
    return 1;
}

/* does the previous token end an operand, so that "/" divides? */
static int after_operand(void)
{
    switch (prev) {
    case TK_NUMBER: case TK_STRING: case TK_ERE: case TK_NAME: case TK_FUNC_NAME:
    case TK_BUILTIN: case TK_RBRACKET: case TK_GETLINE:
        return 1;
    case TK_INCR: case TK_DECR:
        return !prev_dollar_incr;
    case TK_RPAREN:
        return !prev_special_paren;
    }
    return 0;
}

/*
 * "/" or "/=": division, or the start of a regular expression, which
 * ends at the next "/" that is not escaped and not in a bracket
 * expression.  One that does not end on its line is reported where the
 * grammar takes it as an operand (ERE_BAD).
 */
static int slash(int assign)
{
    size_t pos, st;
    int inbr = 0;
    if (after_operand())
        return mk(assign ? TK_DIV_ASSIGN : TK_SLASH);
    if (assign) {
        yyless(1);
        off = tstart + 1;
    }
    pos = st = tstart + 1;
    for (;;) {
        int c;
        if (pos >= srclen || src[pos] == '\n') {
            mk(TK_ERE);
            cur_ytok = ERE_BAD;
            return 1;
        }
        c = (unsigned char)src[pos];
        if (c == '\\' && pos + 1 < srclen && src[pos + 1] != '\n') {
            pos += 2;
            continue;
        }
        if (inbr) {
            if (c == '[' && pos + 1 < srclen &&
                (src[pos+1] == ':' || src[pos+1] == '.' || src[pos+1] == '=')) {
                char d = src[pos + 1];
                size_t j = pos + 2;
                while (j + 1 < srclen && src[j] != '\n' && !(src[j] == d && src[j+1] == ']'))
                    j++;
                if (j + 1 < srclen && src[j] == d) {
                    pos = j + 2;
                    continue;
                }
            }
            if (c == ']')
                inbr = 0;
            pos++;
            continue;
        }
        if (c == '[') {
            inbr = 1;
            pos++;
            if (pos < srclen && src[pos] == '^')
                pos++;
            if (pos < srclen && src[pos] == ']')
                pos++;
            continue;
        }
        if (c == '/')
            break;
        pos++;
    }
    skip(pos + 1 - off);
    mk(TK_ERE);
    cur->str = str_new(src + st, pos - st);
    return 1;
}

void lex_begin(const char *text, size_t len)
{
    src = text;
    srclen = len;
    off = 0;
    cur_line = 1;
    prev = TK_NEWLINE;
    prev_special_paren = 0;
    dollar_prefix = prev_dollar_incr = 0;
    lex_la = NULL;
    lex_in = fmemopen((void *)text, len ? len : 1, "r");
    if (!lex_in)
        fatal("cannot read the program: %s", strerror(errno));
    if (!len) {
        fclose(lex_in);
        lex_in = fopen("/dev/null", "r");
    }
    yyin = lex_in;
}

/* ---------- the tokens for the grammar ---------- */

static int depth;               /* braces open */
static int nparen;
static unsigned char *paren_kind;
static size_t paren_cap;
static Tok *pending;            /* the token after an ITEMSEP or VTERM */
static int pending_ytok;
static Tok *peeked;             /* the token after "|", read by lex_pipe_getline */
static int peeked_ytok;
static int at_eof;

/* the offset of the end of the text before t, without blanks, is a "}" */
static int after_rbrace(const Tok *t)
{
    size_t k = t->start;
    if (k == 0)
        return 0;
    while (k > 0 && (src[k - 1] == ' ' || src[k - 1] == '\t' || src[k - 1] == '\r'))
        k--;
    return k > 0 && src[k - 1] == '}';
}

static void note_token(int type)
{
    if (type == TK_LPAREN) {
        if ((size_t)nparen == paren_cap) {
            paren_cap = paren_cap ? paren_cap * 2 : 16;
            paren_kind = xrealloc(paren_kind, paren_cap);
        }
        paren_kind[nparen++] = prev == TK_IF || prev == TK_WHILE || prev == TK_FOR;
        prev_special_paren = 0;
    } else if (type == TK_RPAREN) {
        prev_special_paren = nparen > 0 ? paren_kind[--nparen] : 0;
    } else
        prev_special_paren = 0;
    /* "$" and the prefix operators of its operand */
    prev_dollar_incr = dollar_prefix && (type == TK_INCR || type == TK_DECR);
    dollar_prefix = type == TK_DOLLAR ||
        (dollar_prefix && (type == TK_INCR || type == TK_DECR || type == TK_MINUS ||
                           type == TK_PLUS || type == TK_NOT));
    prev = type;
}

/* the next token from the scanner */
static Tok *raw_token(int *y)
{
    Tok *t;
    if (peeked) {
        t = peeked;
        *y = peeked_ytok;
        peeked = NULL;
        return t;
    }
    if (at_eof || !awk_scan()) {
        at_eof = 1;
        t = xcalloc(1, sizeof *t);
        t->type = TK_EOF;
        t->start = srclen;
        t->line = cur_line;
        *y = 0;
        return t;
    }
    t = cur;
    *y = cur_ytok;
    note_token(t->type);
    return t;
}

int yylex(void)
{
    Tok *t;
    int y;
    if (pending) {
        t = pending;
        y = pending_ytok;
        pending = NULL;
    } else {
        t = raw_token(&y);
        if (depth > 0 && (y == 0 || y == '}')) {
            /* a statement in a block may end at "}" or at the end of the
               program: the grammar gets a terminator there */
            Tok *v = xmalloc(sizeof *v);
            *v = *t;
            pending = t;
            pending_ytok = y;
            lex_la = v;
            yylval.t = v;
            return VTERM;
        }
        if (depth <= 0 && y != 0 && y != NEWLINE && y != ';' && after_rbrace(t)) {
            /* another item may follow one that ends in "}" */
            pending = t;
            pending_ytok = y;
            lex_la = t;
            yylval.t = t;
            return ITEMSEP;
        }
    }
    if (t->type == TK_LBRACE)
        depth++;
    else if (t->type == TK_RBRACE)
        depth--;
    lex_la = t;
    yylval.t = t;
    return y;
}

/*
 * The lookahead is "|" at the end of a concatenation: read the token
 * after it (as the earlier parser did there), and give PIPE_GETLINE if
 * it is getline.
 */
int lex_pipe_getline(void)
{
    int y;
    if (!peeked && !pending) {
        peeked = raw_token(&peeked_ytok);
    }
    y = pending ? pending_ytok : peeked_ytok;
    return y == Getline;
}

void lex_reset(void)
{
    depth = nparen = 0;
    pending = peeked = NULL;
    at_eof = 0;
    dollar_prefix = prev_dollar_incr = 0;
}

_Noreturn void syntax_error_at(const Tok *t, const char *msg)
{
    size_t ls = t->start, le = t->start;
    while (ls > 0 && src[ls - 1] != '\n')
        ls--;
    while (le < srclen && src[le] != '\n')
        le++;
    fflush(stdout);
    fprintf(stderr, "%s: syntax error at source line %d: %s\n", progname, t->line, msg);
    fprintf(stderr, "%s:  context is: %.*s >>> %.*s <<<\n", progname,
            (int)(t->start - ls), src + ls,
            (int)(le - t->start > 40 ? 40 : le - t->start), src + t->start);
    exit(2);
}
