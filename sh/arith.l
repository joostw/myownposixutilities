%{
/*
 * arith.l - the tokens of arithmetic expansion (2.6.4) for arith.y.
 *
 * The expression is read from a memory stream.  "++" and "--" depend on
 * their context, as in C: after a variable name they are the postfix
 * operators; after another operand they are "+" or "-" (the rest is read
 * again, so "5++x" is 5 + +x); before a name they are the prefix
 * operators; otherwise "+" or "-".  Names and the characters that end a
 * constant follow isalpha()/isalnum() in the current locale.
 */
#include "sh.h"
#include "arith.h"
#include "arith.tab.h"

int arith_last_token;
static int prev, prev_prefix;   /* the previous token, and whether it followed ++/-- */
static FILE *arith_in;
static char *arith_buf;
static int scanning;

static int tok(int t);
static int incdec(int binop, int pre, int post);
static int name(int c);
static char *dup_text(const char *s, size_t n);
static int next(void);
%}
%%
[ \t\n]+                ;
[0-9][A-Za-z0-9_]*      {
                            int c;
                            /* yytext first: input() and unput() may change it */
                            yylval.num.text = dup_text(yytext, (size_t)yyleng);
                            c = next();
                            if (c)
                                unput(c);
                            yylval.num.bad = c > 127 && (isalnum(c) || c == '_');
                            return tok(NUMBER);
                        }
[A-Za-z_]               return name(yytext[0]);
[\200-\377]             {
                            if (isalpha((unsigned char)yytext[0]))
                                return name((unsigned char)yytext[0]);
                            return tok(OTHER);
                        }
"++"                    return incdec(PLUS, PREINC, POSTINC);
"--"                    return incdec(MINUS, PREDEC, POSTDEC);
"<<="                   { yylval.op = "<<"; return tok(ASSIGNOP); }
">>="                   { yylval.op = ">>"; return tok(ASSIGNOP); }
"*="                    { yylval.op = "*"; return tok(ASSIGNOP); }
"/="                    { yylval.op = "/"; return tok(ASSIGNOP); }
"%="                    { yylval.op = "%"; return tok(ASSIGNOP); }
"+="                    { yylval.op = "+"; return tok(ASSIGNOP); }
"-="                    { yylval.op = "-"; return tok(ASSIGNOP); }
"&="                    { yylval.op = "&"; return tok(ASSIGNOP); }
"^="                    { yylval.op = "^"; return tok(ASSIGNOP); }
"|="                    { yylval.op = "|"; return tok(ASSIGNOP); }
"="                     { yylval.op = "="; return tok(ASSIGNOP); }
"||"                    return tok(OR);
"&&"                    return tok(AND);
"=="                    { yylval.op = "=="; return tok(EQOP); }
"!="                    { yylval.op = "!="; return tok(EQOP); }
"<<"                    { yylval.op = "<<"; return tok(SHIFTOP); }
">>"                    { yylval.op = ">>"; return tok(SHIFTOP); }
"<="                    { yylval.op = "<="; return tok(RELOP); }
">="                    { yylval.op = ">="; return tok(RELOP); }
"<"                     { yylval.op = "<"; return tok(RELOP); }
">"                     { yylval.op = ">"; return tok(RELOP); }
"|"                     return tok(BOR);
"^"                     return tok(BXOR);
"&"                     return tok(BAND);
"+"                     return tok(PLUS);
"-"                     return tok(MINUS);
"*"                     { yylval.op = "*"; return tok(MULOP); }
"/"                     { yylval.op = "/"; return tok(MULOP); }
"%"                     { yylval.op = "%"; return tok(MULOP); }
"!"                     return tok(NOT);
"~"                     return tok(TILDE);
"?"                     return tok(QUEST);
":"                     return tok(COLON);
","                     return tok(COMMA);
"("                     return tok(LPAR);
")"                     return tok(RPAR);
.                       return tok(OTHER);
%%
int yywrap(void)
{
    scanning = 0;       /* the whole expression has been read */
    arith_last_token = 0;
    return 1;
}

static int tok(int t)
{
    prev_prefix = (t == NAME && (prev == PREINC || prev == PREDEC));
    prev = t;
    arith_last_token = t;
    return t;
}

/* the next input character, or 0 at the end (lex gives 0, flex EOF) */
static int next(void)
{
    int c = input();
    return c == EOF ? 0 : c;
}

static char *dup_text(const char *s, size_t n)
{
    char *r = stalloc(n + 1);
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}

/* a name: the letter c and the alphanumerics and underscores after it */
static int name(int c)
{
    StrBuf b;
    sb_init(&b);
    sb_addc(&b, c);
    for (;;) {
        c = next();
        if (c && (isalnum(c) || c == '_')) {
            sb_addc(&b, c);
            continue;
        }
        if (c)
            unput(c);
        break;
    }
    yylval.s = dup_text(b.s, b.len);
    sb_free(&b);
    return tok(NAME);
}

static int incdec(int binop, int pre, int post)
{
    StrBuf look;
    int c, second = (unsigned char)yytext[1];
    if (prev == NAME && !prev_prefix)
        return tok(post);
    if (prev == NAME || prev == NUMBER || prev == RPAR || prev == POSTINC || prev == POSTDEC) {
        yyless(1);
        return tok(binop);
    }
    /* a prefix operator only when a name follows (after blanks) */
    sb_init(&look);
    do {
        c = next();
        if (c)
            sb_addc(&look, c);
    } while (c == ' ' || c == '\t');
    while (look.len > 0)
        unput((unsigned char)look.s[--look.len]);
    sb_free(&look);
    if (c && (isalpha(c) || c == '_'))
        return tok(pre);
    unput(second);      /* not yyless(): yytext may have changed */
    return tok(binop);
}

void arith_scan_begin(const char *expr)
{
    size_t n = strlen(expr);
    arith_scan_end();
    arith_buf = xmalloc(n + 1);
    memcpy(arith_buf, expr, n + 1);
    arith_in = fmemopen(arith_buf, n, "r");
    if (!arith_in)
        sh_error("arithmetic expression: %s", strerror(errno));
    yyin = arith_in;
    prev = prev_prefix = 0;
    arith_last_token = -1;
    scanning = 1;
}

/* read the rest of the expression, so that the scanner starts afresh */
void arith_scan_end(void)
{
    if (scanning) {
        scanning = 0;
        while (yylex() > 0)
            ;
    }
    if (arith_in) {
        fclose(arith_in);
        arith_in = NULL;
    }
    free(arith_buf);
    arith_buf = NULL;
}
