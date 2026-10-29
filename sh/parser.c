/*
 * parser.c - token recognition (2.3), word encoding, here-documents,
 * alias substitution (2.3.1), and the token classifier that feeds the
 * yacc grammar of 2.10 (sh.y).
 *
 * Words are encoded as byte strings with embedded control bytes (see
 * CTL* in sh.h) so that quoting information survives until expansion.
 * Command substitutions are parsed while the word is read, as the
 * standard requires for finding the end of "$(" and "`": the lexer
 * calls the parser recursively.
 *
 * Which tokens are reserved words, assignment words and function names,
 * and where alias substitution applies, depends on where the token
 * stands in the grammar (2.10.2).  The classifier (shylex) tracks that
 * position from the tokens it has handed to the parser.
 */
#include "sh.h"
#include "parse.h"
#include "sh.tab.h"

int parse_lineno = 1;
int checkkwd_disabled;

/* ------------------------------------------------------------------ */
/* tokens                                                             */
/* ------------------------------------------------------------------ */

enum {
    T_EOF, T_NEWLINE, T_SEMI, T_AMP, T_PIPE, T_LPAREN, T_RPAREN, T_AND_IF,
    T_OR_IF, T_DSEMI, T_SEMI_AND, T_LESS, T_GREAT, T_DLESS, T_DGREAT,
    T_LESSAND, T_GREATAND, T_LESSGREAT, T_DLESSDASH, T_CLOBBER, T_WORD,
    T_IO_NUMBER
};

static const char *tokname[] = {
    "end of file", "newline", ";", "&", "|", "(", ")", "&&", "||", ";;",
    ";&", "<", ">", "<<", ">>", "<&", ">&", "<>", "<<-", ">|", "word",
    "io number"
};

typedef struct Token {
    int type;
    Word *word;
    int ionum;
    int lineno;
    int aliaschecked;
    int after_blank_alias;
} Token;

static Token tokq[4];
static int ntokq;

/* where the next token stands in the grammar (see shylex) */
enum {
    P_TOP,          /* the start of a complete command */
    P_LIST,         /* the start of a command in a compound list */
    P_SUBLIST,      /* the same, directly in "$(...)" or a string */
    P_PIPE,         /* after && or || */
    P_CMD_BANG,     /* after ! */
    P_CMD_PIPE,     /* after | */
    P_PREFIX,       /* in a simple command before the command name */
    P_ARG,          /* after the command name */
    P_TARGET,       /* the word after a redirection operator */
    P_CLOSE,        /* after the end of a compound command */
    P_FN_LPAREN, P_FN_RPAREN, P_FN_BODY,                /* NAME ( ) body */
    P_FOR_NAME, P_FOR_AFTER, P_FOR_INDO, P_FOR_WORDS, P_DO,
    P_CASE_WORD, P_CASE_IN, P_PAT_START, P_PAT, P_PAT_AFTER
};
enum { MODE_TOP, MODE_CMDSUB, MODE_STRING };
static int lpos, lret, ldepth, lmode, lstart, lop;
static Tok lastok;          /* the last token handed to the parser */
Node *parse_result;
int parse_at_eof;

/* pending here-documents waiting for the next newline */
typedef struct HereDoc { Redir *r; int strip; struct HereDoc *next; } HereDoc;
static HereDoc *heredocs, **heredoc_tail = &heredocs;
static int alias_check_next;     /* previous alias value ended in a blank */

/* word scanning modes */
enum { M_NORMAL, M_DQ, M_HEREDOC, M_VAR, M_VARDQ, M_ARITH };

typedef struct WCtx {
    StrBuf b;
    Node **subs;
    int nsubs, subcap;
    int quoted;
} WCtx;

static void read_heredocs_at_end(void);
void mark_continuation_source(void);

/* XBD 3.10 alias name: alphanumerics, '_', '!', '%', ',', '-', '@' */
static int is_name_alias(const char *s)
{
    if (!*s)
        return 0;
    for (; *s; s++)
        if (!(isalnum((unsigned char)*s) || strchr("_!%,-@", *s)) ||
            (unsigned char)*s > 127)
            return 0;
    return 1;
}
static Node *parse_cmdsub_paren(void);
static Node *parse_string_program(const char *s);
static void scan_word(WCtx *c, int mode);
static void read_heredocs(void);

static int nextc(void)
{
    int c = pgetc();
    if (c == '\n')
        parse_lineno++;
    return c;
}

static void ungetc_(int c)
{
    if (c == '\n')
        parse_lineno--;
    pungetc(c);
}

/* next char with <backslash><newline> line continuation removed */
static int nextc_cont(void)
{
    for (;;) {
        int c = nextc();
        if (c == '\\') {
            int d = nextc();
            if (d == '\n') {
                set_prompt(2);
                continue;
            }
            ungetc_(d);
        }
        return c;
    }
}

static void ctx_init(WCtx *c)
{
    sb_init(&c->b);
    c->subs = NULL;
    c->nsubs = c->subcap = 0;
    c->quoted = 0;
}

static Word *ctx_word(WCtx *c)
{
    Word *w = stalloc(sizeof *w);
    w->s = sb_stack(&c->b);
    w->nsubs = c->nsubs;
    w->subs = NULL;
    if (c->nsubs) {
        w->subs = stalloc((size_t)c->nsubs * sizeof(Node *));
        memcpy(w->subs, c->subs, (size_t)c->nsubs * sizeof(Node *));
    }
    free(c->subs);
    w->quoted = c->quoted;
    w->next = NULL;
    return w;
}

static void add_lit(WCtx *c, int ch)
{
    if (ISCTL(ch))
        sb_addc(&c->b, CTLESC);
    sb_addc(&c->b, ch);
}

static void add_quoted(WCtx *c, int ch)
{
    sb_addc(&c->b, CTLESC);
    sb_addc(&c->b, ch);
}

static int add_sub(WCtx *c, Node *n)
{
    if (c->nsubs == c->subcap) {
        c->subcap = c->subcap ? c->subcap * 2 : 4;
        c->subs = xrealloc(c->subs, (size_t)c->subcap * sizeof(Node *));
    }
    c->subs[c->nsubs] = n;
    return c->nsubs++;
}

static void add_backq(WCtx *c, Node *n)
{
    char num[16];
    int idx = add_sub(c, n);
    snprintf(num, sizeof num, "%d;", idx);
    sb_addc(&c->b, CTLBACKQ);
    sb_adds(&c->b, num);
}

/* ------------------------------------------------------------------ */
/* lexer save/restore around nested parses                            */
/* ------------------------------------------------------------------ */

typedef struct LexState {
    Token tokq[4];
    int ntokq;
    HereDoc *heredocs, **heredoc_tail;
    int alias_check_next;
    int pos, ret, depth, mode, start, op;
    Tok last;
} LexState;

static void lex_save(LexState *s)
{
    memcpy(s->tokq, tokq, sizeof tokq);
    s->ntokq = ntokq;
    s->heredocs = heredocs;
    s->heredoc_tail = heredoc_tail;
    s->alias_check_next = alias_check_next;
    s->pos = lpos;
    s->ret = lret;
    s->depth = ldepth;
    s->mode = lmode;
    s->start = lstart;
    s->op = lop;
    s->last = lastok;
    ntokq = 0;
    heredocs = NULL;
    heredoc_tail = &heredocs;
    alias_check_next = 0;
}

static void lex_restore(LexState *s)
{
    memcpy(tokq, s->tokq, sizeof tokq);
    ntokq = s->ntokq;
    heredocs = s->heredocs;
    heredoc_tail = s->heredoc_tail;
    alias_check_next = s->alias_check_next;
    lpos = s->pos;
    lret = s->ret;
    ldepth = s->depth;
    lmode = s->mode;
    lstart = s->start;
    lop = s->op;
    lastok = s->last;
}

/* ------------------------------------------------------------------ */
/* escapes in $'...'                                                  */
/* ------------------------------------------------------------------ */

static void scan_dollar_single(WCtx *c)
{
    c->quoted = 1;
    sb_addc(&c->b, CTLQNULL);
    for (;;) {
        int ch = nextc();
        if (ch == EOF)
            syntax_error_at(parse_lineno, "unterminated quoted string");
        if (ch == '\'')
            return;
        if (ch != '\\') {
            add_quoted(c, ch);
            continue;
        }
        ch = nextc();
        switch (ch) {
        case '"': add_quoted(c, '"'); break;
        case '\'': add_quoted(c, '\''); break;
        case '\\': add_quoted(c, '\\'); break;
        case 'a': add_quoted(c, '\a'); break;
        case 'b': add_quoted(c, '\b'); break;
        case 'e': add_quoted(c, 033); break;
        case 'f': add_quoted(c, '\f'); break;
        case 'n': add_quoted(c, '\n'); break;
        case 'r': add_quoted(c, '\r'); break;
        case 't': add_quoted(c, '\t'); break;
        case 'v': add_quoted(c, '\v'); break;
        case 'c': {
            int x = nextc();
            if (x == '\\') {
                int y = nextc();
                if (y != '\\') ungetc_(y);
                add_quoted(c, 034);
            } else if (x == '?')
                add_quoted(c, 0177);
            else if (x != EOF)
                add_quoted(c, toupper(x) & 037);
            break;
        }
        case 'x': {
            int v = 0, k = 0, x;
            while (k < 2 && (x = nextc()) != EOF && isxdigit(x)) {
                v = v * 16 + (isdigit(x) ? x - '0' : (tolower(x) - 'a' + 10));
                k++;
            }
            if (k < 2 && x != EOF && !isxdigit(x))
                ungetc_(x);
            if (k == 0)
                add_quoted(c, '\\'), add_quoted(c, 'x');
            else if (v)
                add_quoted(c, v);
            break;
        }
        case EOF:
            syntax_error_at(parse_lineno, "unterminated quoted string");
        default:
            if (ch >= '0' && ch <= '7') {
                int v = ch - '0', k = 1, x;
                while (k < 3 && (x = nextc()) >= '0' && x <= '7') {
                    v = v * 8 + x - '0';
                    k++;
                }
                if (k < 3) ungetc_(x);
                if (v & 0xff)
                    add_quoted(c, v & 0xff);
            } else {
                add_quoted(c, '\\');
                add_quoted(c, ch);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* ${...}                                                             */
/* ------------------------------------------------------------------ */

static int is_special_param(int c)
{
    return c && strchr("@*#?-$!0", c) != NULL;
}

static void scan_braced(WCtx *c, int indq)
{
    StrBuf name;
    int ch, sub = VS_NORMAL, length = 0;
    sb_init(&name);
    ch = nextc_cont();
    if (ch == '#') {
        int n1 = nextc_cont();
        if (n1 == '}') {
            sb_addc(&name, '#');
            goto done;
        }
        if (n1 == ':' || strchr("-=?+#%", n1)) {
            int n2 = nextc_cont();
            if (n2 == '}' && n1 != ':') {
                sb_addc(&name, n1);
                length = 1;
                goto done;
            }
            ungetc_(n2);
            sb_addc(&name, '#');
            ch = n1;
            goto op;
        }
        length = 1;
        ch = n1;
    }
    if (isalpha(ch) || ch == '_') {
        while (isalnum(ch) || ch == '_') {
            sb_addc(&name, ch);
            ch = nextc_cont();
        }
    } else if (isdigit(ch)) {
        while (isdigit(ch)) {
            sb_addc(&name, ch);
            ch = nextc_cont();
        }
    } else if (is_special_param(ch)) {
        sb_addc(&name, ch);
        ch = nextc_cont();
    } else
        syntax_error_at(parse_lineno, "bad substitution");
    if (length) {
        if (ch != '}')
            syntax_error_at(parse_lineno, "bad substitution");
        goto done;
    }
op:
    if (ch == '}')
        goto done;
    {
        int colon = 0;
        if (ch == ':') {
            colon = VS_COLON_OFF;
            ch = nextc_cont();
            if (!strchr("-=?+", ch) || !ch)
                syntax_error_at(parse_lineno, "bad substitution");
        }
        switch (ch) {
        case '-': sub = VS_MINUS; break;
        case '+': sub = VS_PLUS; break;
        case '?': sub = VS_QUESTION; break;
        case '=': sub = VS_ASSIGN; break;
        case '#': {
            int d = nextc_cont();
            if (d == '#') sub = VS_TRIMLEFTMAX;
            else { ungetc_(d); sub = VS_TRIMLEFT; }
            break;
        }
        case '%': {
            int d = nextc_cont();
            if (d == '%') sub = VS_TRIMRIGHTMAX;
            else { ungetc_(d); sub = VS_TRIMRIGHT; }
            break;
        }
        default:
            syntax_error_at(parse_lineno, "bad substitution");
        }
        sb_addc(&c->b, CTLVAR);
        sb_addc(&c->b, sub + colon);
        sb_add(&c->b, name.s, name.len);
        sb_addc(&c->b, '=');
        sb_free(&name);
        {
            int pat = sub >= VS_TRIMLEFT && sub <= VS_TRIMRIGHTMAX;
            scan_word(c, (indq && !pat) ? M_VARDQ : M_VAR);
        }
        sb_addc(&c->b, CTLENDVAR);
        return;
    }
done:
    sb_addc(&c->b, CTLVAR);
    sb_addc(&c->b, length ? VS_LENGTH : VS_NORMAL);
    sb_add(&c->b, name.s, name.len);
    sb_addc(&c->b, '=');
    sb_addc(&c->b, CTLENDVAR);
    sb_free(&name);
}

/* ------------------------------------------------------------------ */
/* $(( vs $(                                                          */
/* ------------------------------------------------------------------ */

/*
 * Called after "$((" has been read.  Decide whether this is an arithmetic
 * expansion: scan ahead (recording the text) to the ')' that balances
 * the inner '(' nesting; it is arithmetic if that ')' is immediately
 * followed by another ')'.  The recorded text is pushed back.
 */
static int looks_arith(void)
{
    StrBuf rec;
    int depth = 0, result = 1, ch;
    sb_init(&rec);
    for (;;) {
        ch = nextc();
        if (ch == EOF)
            break;
        sb_addc(&rec, ch);
        if (ch == '\\') {
            ch = nextc();
            if (ch == EOF) break;
            sb_addc(&rec, ch);
            continue;
        }
        if (ch == '\'') {
            while ((ch = nextc()) != EOF) {
                sb_addc(&rec, ch);
                if (ch == '\'') break;
            }
            continue;
        }
        if (ch == '(') {
            depth++;
            continue;
        }
        if (ch == ')') {
            if (depth > 0) {
                depth--;
                continue;
            }
            ch = nextc();
            if (ch != EOF)
                sb_addc(&rec, ch);
            result = (ch == ')');
            break;
        }
    }
    /* push the recorded text back in front of the input */
    if (rec.len) {
        size_t i;
        for (i = rec.len; i > 0; i--)
            if (rec.s[i - 1] == '\n')
                parse_lineno--;
        push_input_string(xstrndup(rec.s, rec.len), 1);
        mark_continuation_source();
    }
    sb_free(&rec);
    return result;
}

static void scan_arith(WCtx *c)
{
    sb_addc(&c->b, CTLARI);
    scan_word(c, M_ARITH);
    sb_addc(&c->b, CTLENDARI);
}

static void scan_dollar(WCtx *c, int mode)
{
    int indq = (mode == M_DQ || mode == M_HEREDOC || mode == M_VARDQ);
    int ch = nextc_cont();
    if (ch == '{') {
        scan_braced(c, indq);
        return;
    }
    if (ch == '(') {
        int d = nextc_cont();
        if (d == '(') {
            if (looks_arith()) {
                scan_arith(c);
                return;
            }
            /* a command substitution beginning with a subshell */
            ungetc_('(');
        } else
            ungetc_(d);
        add_backq(c, parse_cmdsub_paren());
        return;
    }
    if (ch == '\'' && !indq && mode != M_ARITH) {
        scan_dollar_single(c);
        return;
    }
    if (isalpha(ch) || ch == '_') {
        sb_addc(&c->b, CTLVAR);
        sb_addc(&c->b, VS_NORMAL);
        while (isalnum(ch) || ch == '_') {
            sb_addc(&c->b, ch);
            ch = nextc_cont();
        }
        ungetc_(ch);
        sb_addc(&c->b, '=');
        sb_addc(&c->b, CTLENDVAR);
        return;
    }
    if (isdigit(ch) || is_special_param(ch)) {
        sb_addc(&c->b, CTLVAR);
        sb_addc(&c->b, VS_NORMAL);
        sb_addc(&c->b, ch);
        sb_addc(&c->b, '=');
        sb_addc(&c->b, CTLENDVAR);
        return;
    }
    /* a lone '$' is literal */
    ungetc_(ch);
    if (indq)
        sb_addc(&c->b, '$');
    else
        add_lit(c, '$');
}

/* `...` : collect with backslash processing, then parse */
static void scan_backquote(WCtx *c, int indq)
{
    StrBuf t;
    int ch;
    Node *n;
    sb_init(&t);
    for (;;) {
        ch = nextc();
        if (ch == EOF)
            syntax_error_at(parse_lineno, "unterminated `...`");
        if (ch == '`')
            break;
        if (ch == '\\') {
            int d = nextc();
            if (d == '\n')
                continue;
            if (d == '$' || d == '`' || d == '\\' || (indq && d == '"'))
                sb_addc(&t, d);
            else {
                sb_addc(&t, '\\');
                if (d != EOF) ungetc_(d);
            }
            continue;
        }
        sb_addc(&t, ch);
    }
    n = parse_string_program(t.s ? t.s : "");
    sb_free(&t);
    add_backq(c, n);
}

/* ------------------------------------------------------------------ */
/* word scanner                                                       */
/* ------------------------------------------------------------------ */

static int is_meta(int ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == ';' || ch == '&' ||
           ch == '|' || ch == '<' || ch == '>' || ch == '(' || ch == ')' || ch == EOF;
}

static void scan_dq(WCtx *c, int mode)
{
    c->quoted = 1;
    sb_addc(&c->b, CTLQUOTE);
    scan_word(c, mode);
    sb_addc(&c->b, CTLENDQ);
}

static void scan_word(WCtx *c, int mode)
{
    int adepth = 0;     /* parenthesis depth in arithmetic */
    for (;;) {
        int ch = nextc();
        switch (mode) {
        case M_NORMAL:
            if (is_meta(ch)) {
                ungetc_(ch);
                return;
            }
            break;
        case M_DQ:
            if (ch == '"')
                return;
            if (ch == EOF)
                syntax_error_at(parse_lineno, "unterminated quoted string");
            break;
        case M_HEREDOC:
            if (ch == EOF)
                return;
            break;
        case M_VAR: case M_VARDQ:
            if (ch == '}')
                return;
            if (ch == EOF)
                syntax_error_at(parse_lineno, "missing '}'");
            break;
        case M_ARITH:
            if (ch == EOF)
                syntax_error_at(parse_lineno, "missing '))'");
            if (ch == '(') {
                adepth++;
                sb_addc(&c->b, '(');
                continue;
            }
            if (ch == ')') {
                if (adepth > 0) {
                    adepth--;
                    sb_addc(&c->b, ')');
                    continue;
                }
                ch = nextc_cont();
                if (ch != ')')
                    syntax_error_at(parse_lineno, "missing '))'");
                return;
            }
            break;
        }

        switch (ch) {
        case '\\': {
            int d = nextc();
            if (d == '\n') {
                set_prompt(2);
                continue;
            }
            if (d == EOF) {
                if (mode == M_NORMAL || mode == M_VAR || mode == M_ARITH)
                    add_lit(c, '\\');
                else
                    sb_addc(&c->b, '\\');
                return;
            }
            if (mode == M_NORMAL || mode == M_VAR || mode == M_ARITH) {
                c->quoted = 1;
                add_quoted(c, d);
            } else {
                int special = d == '$' || d == '`' || d == '\\' ||
                              (mode == M_DQ && d == '"') ||
                              (mode == M_VARDQ && (d == '"' || d == '}'));
                if (special)
                    add_quoted(c, d);
                else {
                    sb_addc(&c->b, '\\');
                    ungetc_(d);
                }
            }
            continue;
        }
        case '\'':
            if (mode == M_NORMAL || mode == M_VAR || mode == M_ARITH) {
                c->quoted = 1;
                sb_addc(&c->b, CTLQNULL);
                for (;;) {
                    int d = nextc();
                    if (d == EOF)
                        syntax_error_at(parse_lineno, "unterminated quoted string");
                    if (d == '\'')
                        break;
                    add_quoted(c, d);
                }
                continue;
            }
            break;
        case '"':
            if (mode == M_NORMAL || mode == M_VAR || mode == M_ARITH) {
                scan_dq(c, M_DQ);
                continue;
            }
            if (mode == M_VARDQ) {
                /* nested double quotes inside "${...}" */
                scan_dq(c, M_DQ);
                continue;
            }
            break;
        case '$':
            scan_dollar(c, mode);
            continue;
        case '`':
            scan_backquote(c, mode == M_DQ || mode == M_HEREDOC || mode == M_VARDQ);
            continue;
        }
        if (mode == M_DQ || mode == M_HEREDOC || mode == M_VARDQ) {
            if (ISCTL(ch))
                sb_addc(&c->b, CTLESC);
            sb_addc(&c->b, ch);
        } else
            add_lit(c, ch);
    }
}

/* ------------------------------------------------------------------ */
/* lexer                                                              */
/* ------------------------------------------------------------------ */

extern int alias_blank_popped;

static void lex(Token *t)
{
    int ch;
    t->word = NULL;
    t->aliaschecked = 0;
    t->after_blank_alias = 0;
    alias_blank_popped = 0;
again:
    ch = nextc();
    while (ch == ' ' || ch == '\t')
        ch = nextc();
    if (ch == '\\') {
        int d = nextc();
        if (d == '\n') {
            set_prompt(2);
            goto again;
        }
        ungetc_(d);
    }
    if (ch == '#') {
        while ((ch = nextc()) != '\n' && ch != EOF)
            ;
    }
    t->lineno = parse_lineno;
    t->after_blank_alias = alias_blank_popped;
    alias_blank_popped = 0;
    switch (ch) {
    case EOF:
        t->type = T_EOF;
        return;
    case '\n':
        t->lineno = parse_lineno - 1;
        t->type = T_NEWLINE;
        if (heredocs)
            read_heredocs();
        return;
    case ';': {
        int d = nextc();
        if (d == ';') t->type = T_DSEMI;
        else if (d == '&') t->type = T_SEMI_AND;
        else { ungetc_(d); t->type = T_SEMI; }
        return;
    }
    case '&': {
        int d = nextc();
        if (d == '&') t->type = T_AND_IF;
        else { ungetc_(d); t->type = T_AMP; }
        return;
    }
    case '|': {
        int d = nextc();
        if (d == '|') t->type = T_OR_IF;
        else { ungetc_(d); t->type = T_PIPE; }
        return;
    }
    case '(': t->type = T_LPAREN; return;
    case ')': t->type = T_RPAREN; return;
    case '<': {
        int d = nextc();
        if (d == '<') {
            int e = nextc();
            if (e == '-') t->type = T_DLESSDASH;
            else { ungetc_(e); t->type = T_DLESS; }
        } else if (d == '&') t->type = T_LESSAND;
        else if (d == '>') t->type = T_LESSGREAT;
        else { ungetc_(d); t->type = T_LESS; }
        return;
    }
    case '>': {
        int d = nextc();
        if (d == '>') t->type = T_DGREAT;
        else if (d == '&') t->type = T_GREATAND;
        else if (d == '|') t->type = T_CLOBBER;
        else { ungetc_(d); t->type = T_GREAT; }
        return;
    }
    }
    ungetc_(ch);
    {
        WCtx c;
        int d;
        ctx_init(&c);
        scan_word(&c, M_NORMAL);
        t->word = ctx_word(&c);
        t->type = T_WORD;
        d = nextc();
        ungetc_(d);
        if ((d == '<' || d == '>') && is_number(t->word->s) && !t->word->quoted &&
            strlen(t->word->s) < 8) {
            t->type = T_IO_NUMBER;
            t->ionum = atoi(t->word->s);
        }
    }
}

static const char *reserved[] = {
    "!", "{", "}", "case", "do", "done", "elif", "else", "esac", "fi", "for",
    "if", "in", "then", "until", "while", NULL
};

char *word_literal(Word *w)
{
    const char *p;
    if (!w || w->nsubs)
        return NULL;
    for (p = w->s; *p; p++)
        if (ISCTL(*p))
            return NULL;
    return w->s;
}

static int is_reserved_str(const char *s)
{
    int i;
    for (i = 0; reserved[i]; i++)
        if (strcmp(reserved[i], s) == 0)
            return 1;
    return 0;
}

/* is the token the (unquoted) word kw? */
static int tok_is(Token *t, const char *kw)
{
    char *l;
    if (t->type != T_WORD)
        return 0;
    l = word_literal(t->word);
    return l && strcmp(l, kw) == 0;
}

/* the token at the head of the queue, read if necessary */
static Token *head(void)
{
    if (!ntokq) {
        lex(&tokq[0]);
        ntokq = 1;
    }
    return &tokq[0];
}

/*
 * Alias substitution on the head token (2.3.1): in a command name
 * position (cmdpos), or after an alias whose value ends in a blank.  A
 * token is examined at most once, by the first such check.  Returns 1
 * if an alias was substituted.
 */
static int alias_check(int cmdpos)
{
    int done = 0;
    head();
    for (;;) {
        Token *t = &tokq[0];
        char *lit;
        Alias *a;
        if (!(cmdpos || t->after_blank_alias) || t->aliaschecked || t->type != T_WORD)
            return done;
        t->aliaschecked = 1;
        lit = word_literal(t->word);
        if (!lit || !is_name_alias(lit))
            return done;
        if (cmdpos && is_reserved_str(lit))
            return done;
        a = alias_lookup(lit);
        if (!a || a->inuse)
            return done;
        push_alias(a->val, a);
        ntokq = 1;
        lex(&tokq[0]);
        done = 1;
    }
}

static void consume(void)
{
    if (ntokq) {
        memmove(&tokq[0], &tokq[1], (size_t)(ntokq - 1) * sizeof(Token));
        ntokq--;
    }
}

_Noreturn static void unexpected_at(int line, int type, int word)
{
    if (word)
        syntax_error_at(line, "unexpected word");
    syntax_error_at(line, "unexpected \"%s\"", tokname[type]);
}

_Noreturn static void unexpected(Token *t)
{
    unexpected_at(t->lineno, t->type, t->type == T_WORD);
}

_Noreturn static void expect_failed(int line, int type, const char *kw)
{
    if (type == T_EOF)
        syntax_error_at(line, "unexpected end of file (expecting \"%s\")", kw);
    syntax_error_at(line, "expecting \"%s\"", kw);
}

/* ------------------------------------------------------------------ */
/* here-documents                                                     */
/* ------------------------------------------------------------------ */

/* delimiter text: quote removal on the encoded word */
static char *heredoc_delim(Word *w)
{
    StrBuf b;
    const char *p;
    sb_init(&b);
    for (p = w->s; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == (unsigned char)CTLESC) {
            if (p[1]) sb_addc(&b, *++p);
        } else if (ch == (unsigned char)CTLQUOTE || ch == (unsigned char)CTLENDQ ||
                   ch == (unsigned char)CTLQNULL) {
            /* removed */
        } else if (ch == (unsigned char)CTLVAR) {
            int sub = (unsigned char)*++p;
            sb_addc(&b, '$');
            if (sub != VS_NORMAL) sb_addc(&b, '{');
            p++;
            while (*p && *p != '=') sb_addc(&b, *p++);
            while (*p && *p != CTLENDVAR) p++;
            if (sub != VS_NORMAL) sb_addc(&b, '}');
        } else if (ch == (unsigned char)CTLBACKQ) {
            while (*p && *p != ';') p++;
        } else if (ISCTL(ch)) {
            /* arithmetic markers etc.: drop */
        } else
            sb_addc(&b, ch);
    }
    return sb_stack(&b);
}

static void add_heredoc(Redir *r, int strip)
{
    HereDoc *h = stalloc(sizeof *h);
    h->r = r;
    h->strip = strip;
    h->next = NULL;
    *heredoc_tail = h;
    heredoc_tail = &h->next;
}

static Word *encode_heredoc_body(const char *text)
{
    WCtx c;
    LexState ls;
    int saveline = parse_lineno;
    ctx_init(&c);
    lex_save(&ls);
    push_input_string(text, 0);
    scan_word(&c, M_HEREDOC);
    pop_input();
    lex_restore(&ls);
    parse_lineno = saveline;
    return ctx_word(&c);
}

static void read_heredocs_at_end(void)
{
    read_heredocs();
}

static void read_heredocs(void)
{
    HereDoc *h = heredocs;
    heredocs = NULL;
    heredoc_tail = &heredocs;
    for (; h; h = h->next) {
        char *delim = heredoc_delim(h->r->word);
        int quoted = h->r->word->quoted;
        StrBuf body, line;
        int done = 0;
        sb_init(&body);
        while (!done) {
            int ch;
            sb_init(&line);
            set_prompt(2);
            for (;;) {
                ch = nextc();
                if (ch == EOF)
                    break;
                if (ch == '\\' && !quoted) {
                    int d = nextc();
                    if (d == '\n')
                        continue;   /* line continuation */
                    sb_addc(&line, '\\');
                    if (d == EOF) break;
                    ch = d;
                }
                if (ch == '\n')
                    break;
                if (h->strip && line.len == 0 && ch == '\t')
                    continue;
                sb_addc(&line, ch);
            }
            if (line.s && strcmp(line.s, delim) == 0) {
                done = 1;
            } else if (!line.s && *delim == 0 && ch != EOF) {
                done = 1;
            } else {
                if (line.s)
                    sb_add(&body, line.s, line.len);
                if (ch == EOF) {
                    if (line.len)
                        sb_addc(&body, '\n');
                    done = 1;
                } else
                    sb_addc(&body, '\n');
            }
            sb_free(&line);
        }
        h->r->hquoted = quoted;
        if (quoted) {
            Word *w = stalloc(sizeof *w);
            w->s = sb_stack(&body);
            w->subs = NULL;
            w->nsubs = 0;
            w->quoted = 1;
            w->next = NULL;
            h->r->here = w;
        } else {
            char *text = sb_stack(&body);
            h->r->here = encode_heredoc_body(text);
        }
    }
}

/* ------------------------------------------------------------------ */
/* the token classifier: tokens for the yacc grammar (sh.y)           */
/* ------------------------------------------------------------------ */

Node *new_node(int type, int line)
{
    Node *n = stalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->type = type;
    n->lineno = line;
    return n;
}

Node *make_list(Node *a, Node *b, int line)
{
    Node *n;
    if (!a) return b;
    if (!b) return a;
    n = new_node(N_LIST, line);
    n->u.bin.a = a;
    n->u.bin.b = b;
    return n;
}

/* the line of the first token of an and-or list */
int node_first_line(Node *n)
{
    while (n->type == N_AND || n->type == N_OR)
        n = n->u.bin.a;
    return n->lineno;
}

static const struct { const char *s; int tok; } kwtab[] = {
    { "!", Bang }, { "{", Lbrace }, { "}", Rbrace }, { "case", Case }, { "do", Do },
    { "done", Done }, { "elif", Elif }, { "else", Else }, { "esac", Esac }, { "fi", Fi },
    { "for", For }, { "if", If }, { "in", In }, { "then", Then }, { "until", Until },
    { "while", While }
};

static int kwtok(const char *lit)
{
    size_t i;
    for (i = 0; i < sizeof kwtab / sizeof kwtab[0]; i++)
        if (strcmp(kwtab[i].s, lit) == 0)
            return kwtab[i].tok;
    return 0;
}

/* reserved words and tokens that end a compound list */
static int ends_list_kw(int k)
{
    return k == Then || k == Else || k == Elif || k == Fi || k == Do || k == Done ||
           k == Esac || k == Rbrace;
}

static int ends_list_type(int type)
{
    return type == T_EOF || type == T_RPAREN || type == T_DSEMI || type == T_SEMI_AND;
}

static int is_redir_op(int type)
{
    return type == T_LESS || type == T_GREAT || type == T_DLESS || type == T_DGREAT ||
           type == T_LESSAND || type == T_GREATAND || type == T_LESSGREAT ||
           type == T_DLESSDASH || type == T_CLOBBER;
}

static int optok(int type)
{
    switch (type) {
    case T_EOF: return EOFTOK;
    case T_NEWLINE: return NEWLINE;
    case T_SEMI: return SEMI;
    case T_AMP: return AMP;
    case T_PIPE: return PIPE;
    case T_LPAREN: return LPAREN;
    case T_RPAREN: return RPAREN;
    case T_AND_IF: return AND_IF;
    case T_OR_IF: return OR_IF;
    case T_DSEMI: return DSEMI;
    case T_SEMI_AND: return SEMI_AND;
    case T_LESS: return LESS;
    case T_GREAT: return GREAT;
    case T_DLESS: return DLESS;
    case T_DGREAT: return DGREAT;
    case T_LESSAND: return LESSAND;
    case T_GREATAND: return GREATAND;
    case T_LESSGREAT: return LESSGREAT;
    case T_DLESSDASH: return DLESSDASH;
    case T_CLOBBER: return CLOBBER;
    case T_IO_NUMBER: return IO_NUMBER;
    }
    return WORD;
}

/* the position after a separator in a list */
static int list_pos(void)
{
    return ldepth > 0 ? P_LIST : lmode == MODE_TOP ? P_TOP : P_SUBLIST;
}

static int at_command_start(int pos)
{
    return pos == P_TOP || pos == P_LIST || pos == P_SUBLIST || pos == P_PIPE ||
           pos == P_CMD_BANG || pos == P_CMD_PIPE;
}

/* hand the head token to the parser as ytok */
static int give(int ytok, int word_derived, int cmd_pos)
{
    Token *t = &tokq[0];
    Tok *v = stalloc(sizeof *v);
    v->type = t->type;
    v->ytok = ytok;
    v->line = t->lineno;
    v->word = t->word;
    v->ionum = t->ionum;
    v->word_derived = word_derived;
    v->cmd_pos = cmd_pos;
    v->here = NULL;
    shylval.t = v;
    lastok = *v;
    consume();
    return ytok;
}

/* an operator token (or the end of input): the position it leads to */
static int give_op(int cmd_pos)
{
    Token *t = &tokq[0];
    int pos = lpos;
    switch (t->type) {
    case T_SEMI: case T_AMP: case T_NEWLINE:
        lpos = list_pos();
        break;
    case T_AND_IF: case T_OR_IF:
        lpos = P_PIPE;
        break;
    case T_PIPE:
        lpos = P_CMD_PIPE;
        break;
    case T_DSEMI: case T_SEMI_AND:
        lpos = P_PAT_START;
        break;
    case T_LPAREN:              /* a subshell */
        ldepth++;
        lpos = P_LIST;
        break;
    case T_RPAREN:              /* the end of a subshell or of "$(" */
        ldepth--;
        lpos = P_CLOSE;
        break;
    case T_IO_NUMBER: case T_EOF:
        break;
    default:
        if (is_redir_op(t->type)) {
            lop = t->type;
            lret = at_command_start(pos) || pos == P_PREFIX ? P_PREFIX : pos;
            lpos = P_TARGET;
        }
    }
    return give(optok(t->type), 0, cmd_pos);
}

/* a reserved word: the position it leads to */
static int give_kw(int k, int cmd_pos)
{
    switch (k) {
    case If: case While: case Until: case Lbrace:
        ldepth++;
        lpos = P_LIST;
        break;
    case Then: case Else: case Elif: case Do:
        lpos = P_LIST;
        break;
    case Fi: case Done: case Esac: case Rbrace:
        ldepth--;
        lpos = P_CLOSE;
        break;
    case For:
        ldepth++;
        lpos = P_FOR_NAME;
        break;
    case Case:
        ldepth++;
        lpos = P_CASE_WORD;
        break;
    case Bang:
        lpos = P_CMD_BANG;
        break;
    default:
        lpos = P_ARG;
    }
    return give(k, 1, cmd_pos);
}

/*
 * Where a command can begin.  The old recursive-descent parser looked
 * at such a token first with a plain peek (from a linebreak) and then
 * in command position; reserved words end a compound list (P_LIST) or
 * start a command; any other token here that is not a word or a
 * redirection is the "command expected" error (cmd_pos).
 */
static int cmd_start(void)
{
    int pos = lpos, cmd_pos, late;
    Token *t;
    char *lit;
    if (pos != P_TOP && pos != P_CMD_BANG)
        alias_check(0);
    late = alias_check(1);
    t = &tokq[0];
    if (t->type == T_NEWLINE && pos != P_TOP && pos != P_CMD_BANG) {
        /* the linebreak is skipped before the command-position alias
           check; a newline that an alias left here is a bad command */
        if (late)
            unexpected(t);
        return give(NEWLINE, 0, 0);     /* a linebreak: the position stays */
    }
    if (t->type == T_WORD) {
        lit = word_literal(t->word);
        if (lit && is_reserved_str(lit)) {
            int k = kwtok(lit);
            if (k == Bang && (pos == P_CMD_BANG || pos == P_CMD_PIPE)) {
                lpos = P_ARG;           /* a command named "!" */
                return give(WORD, 1, 1);
            }
            cmd_pos = (pos == P_LIST || pos == P_SUBLIST) ? !ends_list_kw(k) : 1;
            return give_kw(k, cmd_pos);
        }
        if (is_assignment_word(t->word)) {
            lpos = P_PREFIX;
            return give(ASSIGNMENT_WORD, 1, 1);
        }
        if (lit && is_name(lit)) {
            /* NAME followed by ( is a function definition */
            if (ntokq < 2) {
                lex(&tokq[1]);
                ntokq = 2;
            }
            if (tokq[1].type == T_LPAREN) {
                lpos = P_FN_LPAREN;
                return give(FNAME, 1, 1);
            }
        }
        lpos = P_ARG;
        return give(WORD, 1, 1);
    }
    cmd_pos = 1;
    if ((pos == P_LIST || pos == P_SUBLIST) && ends_list_type(t->type))
        cmd_pos = 0;
    return give_op(cmd_pos);
}

/* in a simple command: before the command name (prefix) or after it */
static int in_command(int prefix)
{
    Token *t = &tokq[0];
    if (t->type == T_WORD) {
        if (prefix && is_assignment_word(t->word))
            return give(ASSIGNMENT_WORD, 1, 0);
        lpos = P_ARG;
        return give(WORD, 1, 0);
    }
    return give_op(0);
}

/*
 * After a compound command.  Inside a compound list the old parser
 * looked at a word here once more in command position, to find the
 * reserved word that closes the enclosing construct; an alias found
 * then can only supply that reserved word, so anything else it yields
 * is an error token (LATE).
 */
static int after_close(void)
{
    Token *t;
    char *lit;
    int late = 0;
    alias_check(0);
    if (ldepth > 0 && tokq[0].type == T_WORD)
        late = alias_check(1);
    t = &tokq[0];
    if (t->type == T_WORD) {
        lit = word_literal(t->word);
        if (lit && is_reserved_str(lit))
            return give_kw(kwtok(lit), 0);
        lpos = P_ARG;
        return give(WORD, 1, 0);
    }
    if (late)
        return give(LATE, 0, 0);
    return give_op(0);
}

static int compound_start(Token *t)
{
    char *lit;
    if (t->type == T_LPAREN)
        return 1;
    lit = t->type == T_WORD ? word_literal(t->word) : NULL;
    return lit && (!strcmp(lit, "{") || !strcmp(lit, "if") || !strcmp(lit, "while") ||
                   !strcmp(lit, "until") || !strcmp(lit, "for") || !strcmp(lit, "case"));
}

static int tok_is_kw(Token *t, const char *kw)
{
    return tok_is(t, kw);
}

int shylex(void)
{
    Token *t;
    if (lstart) {
        int s = lstart;
        lstart = 0;
        return s;
    }
    switch (lpos) {
    case P_TOP: case P_LIST: case P_SUBLIST: case P_PIPE: case P_CMD_BANG: case P_CMD_PIPE:
        return cmd_start();
    case P_PREFIX:
        alias_check(1);
        return in_command(1);
    case P_ARG:
        alias_check(0);
        return in_command(0);
    case P_CLOSE:
        return after_close();
    case P_TARGET:
        alias_check(0);
        t = &tokq[0];
        if (t->type != T_WORD)
            unexpected(t);
        lpos = lret;
        if (lop == T_DLESS || lop == T_DLESSDASH) {
            Redir *r = stalloc(sizeof *r);
            int k;
            memset(r, 0, sizeof *r);
            r->word = t->word;
            add_heredoc(r, lop == T_DLESSDASH);
            k = give(WORD, 1, 0);
            shylval.t->here = r;
            return k;
        }
        return give(WORD, 1, 0);
    case P_FN_LPAREN:
        head();
        lpos = P_FN_RPAREN;
        return give(LPAREN, 0, 0);
    case P_FN_RPAREN:
        alias_check(0);
        t = &tokq[0];
        if (t->type != T_RPAREN)
            unexpected(t);
        lpos = P_FN_BODY;
        return give(RPAREN, 0, 0);
    case P_FN_BODY: {
        int late;
        alias_check(0);
        late = alias_check(1);
        t = &tokq[0];
        if (t->type == T_NEWLINE && !late)
            return give(NEWLINE, 0, 0);
        if (!compound_start(t))
            syntax_error_at(t->lineno, "function body must be a compound command");
        if (t->type == T_LPAREN)
            return give_op(0);
        return give_kw(kwtok(word_literal(t->word)), 0);
    }
    case P_FOR_NAME: {
        char *name;
        alias_check(0);
        t = &tokq[0];
        name = t->type == T_WORD ? word_literal(t->word) : NULL;
        if (!name || !is_name(name))
            syntax_error_at(t->lineno, "bad for loop variable");
        lpos = P_FOR_AFTER;
        return give(WORD, 1, 0);
    }
    case P_FOR_AFTER:
        alias_check(0);
        t = &tokq[0];
        if (t->type == T_SEMI) {
            lpos = P_DO;
            return give(SEMI, 0, 0);
        }
        lpos = P_FOR_INDO;
        /* FALLTHROUGH */
    case P_FOR_INDO:
        alias_check(0);
        t = &tokq[0];
        if (t->type == T_NEWLINE)
            return give(NEWLINE, 0, 0);
        if (tok_is_kw(t, "in")) {
            lpos = P_FOR_WORDS;
            return give(In, 1, 0);
        }
        goto do_word;
    case P_FOR_WORDS:
        alias_check(0);
        t = &tokq[0];
        if (t->type == T_WORD)
            return give(WORD, 1, 0);
        if (t->type == T_SEMI) {
            lpos = P_DO;
            return give(SEMI, 0, 0);
        }
        if (t->type == T_NEWLINE) {
            lpos = P_DO;
            return give(NEWLINE, 0, 0);
        }
        unexpected(t);
    case P_DO:
        alias_check(0);
        if (tokq[0].type == T_NEWLINE)
            return give(NEWLINE, 0, 0);
    do_word:
        alias_check(1);
        t = &tokq[0];
        if (!tok_is_kw(t, "do"))
            expect_failed(t->lineno, t->type, "do");
        lpos = P_LIST;
        return give(Do, 1, 0);
    case P_CASE_WORD:
        alias_check(0);
        t = &tokq[0];
        if (t->type != T_WORD)
            unexpected(t);
        lpos = P_CASE_IN;
        return give(WORD, 1, 0);
    case P_CASE_IN:
        alias_check(0);
        t = &tokq[0];
        if (t->type == T_NEWLINE)
            return give(NEWLINE, 0, 0);
        if (!tok_is_kw(t, "in"))
            expect_failed(t->lineno, t->type, "in");
        lpos = P_PAT_START;
        return give(In, 1, 0);
    case P_PAT_START:
        alias_check(0);
        t = &tokq[0];
        if (t->type == T_NEWLINE)
            return give(NEWLINE, 0, 0);
        if (tok_is_kw(t, "esac"))
            return give_kw(Esac, 0);
        if (t->type == T_LPAREN) {
            lpos = P_PAT;
            return give(LPAREN, 0, 0);
        }
        /* FALLTHROUGH */
    case P_PAT:
        alias_check(0);
        t = &tokq[0];
        if (t->type != T_WORD)
            unexpected(t);
        lpos = P_PAT_AFTER;
        return give(WORD, 1, 0);
    case P_PAT_AFTER:
        alias_check(0);
        t = &tokq[0];
        if (t->type == T_PIPE) {
            lpos = P_PAT;
            return give(PIPE, 0, 0);
        }
        if (t->type != T_RPAREN)
            syntax_error_at(t->lineno, "expecting \")\" in case pattern");
        lpos = P_LIST;
        return give(RPAREN, 0, 0);
    }
    return give_op(0);
}

/*
 * A syntax error found by the grammar, at the last token handed to it.
 * A token where a command had to begin gets the message the old parser
 * gave for a bad command; otherwise the message names what the
 * construct (ctx) expected.
 */
void shy_syntax(int ctx, const char *expected)
{
    Tok *t = &lastok;
    if (t->cmd_pos) {
        char *lit = t->word_derived ? word_literal(t->word) : NULL;
        if (lit && is_reserved_str(lit) && strcmp(lit, "!") != 0)
            syntax_error_at(t->line, "unexpected \"%s\"", lit);
        unexpected_at(t->line, t->type, t->word_derived);
    }
    switch (ctx) {
    case CTX_EXPECT:
        expect_failed(t->line, t->type, expected);
    case CTX_RPAREN:
        if (t->type == T_EOF)
            syntax_error_at(t->line, "unexpected end of file (expecting \")\")");
        syntax_error_at(t->line, "expecting \")\"");
    case CTX_CMDSUB:
        if (t->type == T_EOF)
            syntax_error_at(t->line, "unexpected end of file (expecting \")\")");
        /* FALLTHROUGH */
    default:
        unexpected_at(t->line, t->type, t->word_derived);
    }
}

void shyerror(const char *s)
{
    (void)s;            /* reported by the error rules or below */
}

static Node *run_parser(int mode)
{
    lmode = mode;
    ldepth = 0;
    lpos = mode == MODE_TOP ? P_TOP : P_SUBLIST;
    lstart = mode == MODE_TOP ? START_LINE : mode == MODE_CMDSUB ? START_CMDSUB : START_STRING;
    parse_result = NULL;
    parse_at_eof = 0;
    if (shyparse() != 0)
        shy_syntax(CTX_GENERIC, NULL);
    return parse_result;
}

/*
 * Parse one complete_command (a list terminated by a newline or EOF).
 * Returns NULL for an empty line; *eof is set at end of input.
 */
Node *parse_command(int *eof)
{
    Node *n;
    *eof = 0;
    set_prompt(1);
    n = run_parser(MODE_TOP);
    if (parse_at_eof) {
        *eof = 1;
        return NULL;
    }
    if (heredocs)
        syntax_error_at(parse_lineno, "here-document delimiter not found");
    return n;
}

/* after "$(" : parse commands up to the matching ')' */
static Node *parse_cmdsub_paren(void)
{
    LexState ls;
    Node *n;
    lex_save(&ls);
    n = run_parser(MODE_CMDSUB);
    if (heredocs)
        read_heredocs_at_end();
    ntokq = 0;
    lex_restore(&ls);
    return n ? n : new_node(N_LIST, parse_lineno);
}

static Node *parse_string_program(const char *s)
{
    LexState ls;
    Node *n;
    int saveline = parse_lineno;
    lex_save(&ls);
    push_input_string(s, 0);
    n = run_parser(MODE_STRING);
    if (heredocs)
        read_heredocs_at_end();
    pop_input();
    lex_restore(&ls);
    parse_lineno = saveline;
    return n ? n : new_node(N_LIST, parse_lineno);
}

Node *parse_string_list(const char *s, int lineno)
{
    int save = parse_lineno;
    Node *n;
    parse_lineno = lineno;
    n = parse_string_program(s);
    parse_lineno = save;
    return n;
}

void parser_reset(void)
{
    ntokq = 0;
    heredocs = NULL;
    heredoc_tail = &heredocs;
    alias_check_next = 0;
    lstart = 0;
}

/* ------------------------------------------------------------------ */
/* helpers used by expansion                                          */
/* ------------------------------------------------------------------ */

int is_assignment_word(Word *w)
{
    const char *p = w->s;
    if (!(isalpha((unsigned char)*p) || *p == '_'))
        return 0;
    while (isalnum((unsigned char)*p) || *p == '_')
        p++;
    return *p == '=';
}

/* ------------------------------------------------------------------ */
/* deep copy for function definitions                                 */
/* ------------------------------------------------------------------ */

static Word *copy_words(Word *w)
{
    Word *head = NULL, **tail = &head;
    for (; w; w = w->next) {
        Word *c = xmalloc(sizeof *c);
        int i;
        c->s = xstrdup(w->s);
        c->nsubs = w->nsubs;
        c->subs = NULL;
        if (w->nsubs) {
            c->subs = xmalloc((size_t)w->nsubs * sizeof(Node *));
            for (i = 0; i < w->nsubs; i++)
                c->subs[i] = copy_tree(w->subs[i]);
        }
        c->quoted = w->quoted;
        c->next = NULL;
        *tail = c;
        tail = &c->next;
    }
    return head;
}

static Redir *copy_redirs(Redir *r)
{
    Redir *head = NULL, **tail = &head;
    for (; r; r = r->next) {
        Redir *c = xmalloc(sizeof *c);
        *c = *r;
        c->word = copy_words(r->word);
        c->here = r->here ? copy_words(r->here) : NULL;
        c->next = NULL;
        *tail = c;
        tail = &c->next;
    }
    return head;
}

Node *copy_tree(Node *n)
{
    Node *c;
    if (!n)
        return NULL;
    c = xmalloc(sizeof *c);
    *c = *n;
    c->redir = copy_redirs(n->redir);
    switch (n->type) {
    case N_SIMPLE:
        c->u.simple.assigns = copy_words(n->u.simple.assigns);
        c->u.simple.words = copy_words(n->u.simple.words);
        break;
    case N_PIPE: {
        int i;
        c->u.pipe.cmds = xmalloc((size_t)n->u.pipe.n * sizeof(Node *));
        for (i = 0; i < n->u.pipe.n; i++)
            c->u.pipe.cmds[i] = copy_tree(n->u.pipe.cmds[i]);
        break;
    }
    case N_AND: case N_OR: case N_LIST:
        c->u.bin.a = copy_tree(n->u.bin.a);
        c->u.bin.b = copy_tree(n->u.bin.b);
        break;
    case N_ASYNC: case N_NOT: case N_SUBSHELL: case N_BRACE:
        c->u.un.a = copy_tree(n->u.un.a);
        c->u.un.text = n->u.un.text ? xstrdup(n->u.un.text) : NULL;
        break;
    case N_IF:
        c->u.nif.cond = copy_tree(n->u.nif.cond);
        c->u.nif.then = copy_tree(n->u.nif.then);
        c->u.nif.els = copy_tree(n->u.nif.els);
        break;
    case N_WHILE: case N_UNTIL:
        c->u.loop.cond = copy_tree(n->u.loop.cond);
        c->u.loop.body = copy_tree(n->u.loop.body);
        break;
    case N_FOR:
        c->u.nfor.var = xstrdup(n->u.nfor.var);
        c->u.nfor.words = copy_words(n->u.nfor.words);
        c->u.nfor.body = copy_tree(n->u.nfor.body);
        break;
    case N_CASE: {
        CaseItem *ci, *head = NULL, **tail = &head;
        c->u.ncase.word = copy_words(n->u.ncase.word);
        for (ci = n->u.ncase.items; ci; ci = ci->next) {
            CaseItem *x = xmalloc(sizeof *x);
            x->patterns = copy_words(ci->patterns);
            x->body = copy_tree(ci->body);
            x->fallthrough = ci->fallthrough;
            x->next = NULL;
            *tail = x;
            tail = &x->next;
        }
        c->u.ncase.items = head;
        break;
    }
    case N_FUNCDEF:
        c->u.func.name = xstrdup(n->u.func.name);
        c->u.func.body = copy_tree(n->u.func.body);
        break;
    }
    return c;
}

Word *parse_prompt_word(const char *s)
{
    return encode_heredoc_body(s);
}

/* save/restore the parser state around nested parses (eval, ., traps) */
void *parser_push_state(void)
{
    LexState *ls = xmalloc(sizeof *ls);
    lex_save(ls);
    return ls;
}

void parser_pop_state(void *p)
{
    LexState *ls = p;
    lex_restore(ls);
    free(ls);
}
