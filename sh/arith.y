%{
/*
 * arith.y - arithmetic expansion (2.6.4): signed long integer arithmetic
 * with the C operators, assignments, and short-circuit evaluation.
 * Tokens come from arith.l.
 *
 * The expression is evaluated while it is parsed, left to right, as C
 * would: the left operand of a binary operator is read when the operator
 * has been seen (a mid-rule action), the operand that &&, || or ?:
 * skips is parsed with evaluation off (noeval), and an assignment checks
 * its left side when the operator has been seen.  The value of a
 * variable is read when it is used, not where its name appears.
 */
#include "sh.h"
#include "arith.h"

#define YYMAXDEPTH 10000000     /* deep nesting, as the old recursive evaluator allowed */

int yylex(void);
void yyerror(const char *s);

static int noeval;
static const char *aexpr;       /* the whole expression, for messages */
static long result;

_Noreturn static void aerror(const char *msg);
static long rv(AVal x);
static void assign(const char *name, long v);
static long apply(const char *op, long x, long y);
static AVal mk(long v);
static AVal num(ANum n);
static AVal nm(char *s);
static long incr(char *name, long by, int post);
%}

%union {
    AVal a;
    long v;
    const char *op;
    char *s;
    ANum num;
    ATern t;
    AHead h;
}

%token <num> NUMBER
%token <s> NAME
%token <op> ASSIGNOP EQOP RELOP SHIFTOP MULOP
%token OR AND BOR BXOR BAND PLUS MINUS NOT TILDE QUEST COLON COMMA LPAR RPAR
%token PREINC PREDEC POSTINC POSTDEC OTHER

%left OR
%left AND
%left BOR
%left BXOR
%left BAND
%left EQOP
%left RELOP
%left SHIFTOP
%left PLUS MINUS
%left MULOP

%type <a> assign cond e unary postfix primary plist
%type <v> list
%type <t> tern_head
%type <h> assign_head

%start top
%%

top         : list                      { result = $1; }
            ;
list        : assign                    { $$ = rv($1); }
            | list COMMA assign         { $$ = rv($3); }
            ;

assign      : cond                      { $$ = $1; }
            | assign_head assign
                { long v = rv($2);
                  if ($1.op[0] != '=')
                      v = apply($1.op, rv(nm($1.name)), v);
                  assign($1.name, v);
                  $$ = mk(v); }
            ;
assign_head : cond ASSIGNOP
                { if (!$1.name)
                      aerror("assignment to non-variable");
                  $$.name = $1.name; $$.op = $2; }
            ;

cond        : e                         { $$ = $1; }
            | tern_head COLON { if ($1.cv) noeval++; } cond
                { long fv = rv($4);
                  if ($1.cv) noeval--;
                  $$ = mk($1.cv ? $1.tv : fv); }
            | tern_head error           { aerror("expected ':'"); }
            ;
tern_head   : e QUEST { $<v>$ = rv($1); if (!$<v>$) noeval++; } assign
                { $$.tv = rv($4);
                  if (!$<v>3) noeval--;
                  $$.cv = $<v>3; }
            ;

e           : e OR { $<v>$ = rv($1); if ($<v>$) noeval++; } e
                { long r = rv($4);
                  if ($<v>3) noeval--;
                  $$ = mk($<v>3 || r); }
            | e AND { $<v>$ = rv($1); if (!$<v>$) noeval++; } e
                { long r = rv($4);
                  if (!$<v>3) noeval--;
                  $$ = mk($<v>3 && r); }
            | e BOR { $<v>$ = rv($1); } e       { $$ = mk(apply("|", $<v>3, rv($4))); }
            | e BXOR { $<v>$ = rv($1); } e      { $$ = mk(apply("^", $<v>3, rv($4))); }
            | e BAND { $<v>$ = rv($1); } e      { $$ = mk(apply("&", $<v>3, rv($4))); }
            | e EQOP { $<v>$ = rv($1); } e      { $$ = mk(apply($2, $<v>3, rv($4))); }
            | e RELOP { $<v>$ = rv($1); } e     { $$ = mk(apply($2, $<v>3, rv($4))); }
            | e SHIFTOP { $<v>$ = rv($1); } e   { $$ = mk(apply($2, $<v>3, rv($4))); }
            | e PLUS { $<v>$ = rv($1); } e      { $$ = mk(apply("+", $<v>3, rv($4))); }
            | e MINUS { $<v>$ = rv($1); } e     { $$ = mk(apply("-", $<v>3, rv($4))); }
            | e MULOP { $<v>$ = rv($1); } e     { $$ = mk(apply($2, $<v>3, rv($4))); }
            | unary                     { $$ = $1; }
            ;

unary       : postfix                   { $$ = $1; }
            | PREINC NAME               { $$ = mk(incr($2, 1, 0)); }
            | PREDEC NAME               { $$ = mk(incr($2, -1, 0)); }
            | PLUS unary                { $$ = mk(rv($2)); }
            | MINUS unary               { $$ = mk((long)(0UL - (unsigned long)rv($2))); }
            | NOT unary                 { $$ = mk(!rv($2)); }
            | TILDE unary               { $$ = mk(~rv($2)); }
            ;
postfix     : primary                   { $$ = $1; }
            | NAME POSTINC              { $$ = mk(incr($1, 1, 1)); }
            | NAME POSTDEC              { $$ = mk(incr($1, -1, 1)); }
            ;
primary     : NUMBER                    { $$ = num($1); }
            | NAME                      { $$ = nm($1); }
            | LPAR plist RPAR           { $$ = mk(rv($2)); }
            | LPAR plist error          { aerror("expected ')'"); }
            | error
                { aerror(arith_last_token == 0 ? "unexpected end of expression" : "syntax error"); }
            ;
plist       : assign                    { $$ = $1; }
            | plist COMMA assign        { $$ = $3; }
            ;

%%

void yyerror(const char *s)
{
    (void)s;            /* reported by the error rules */
}

_Noreturn static void aerror(const char *msg)
{
    arith_scan_end();
    sh_error("arithmetic expression: %s: \"%s\"", msg, aexpr);
}

static AVal mk(long v)
{
    AVal x;
    x.v = v;
    x.name = NULL;
    return x;
}

static AVal nm(char *s)
{
    AVal x;
    x.v = 0;
    x.name = s;
    return x;
}

/* parse an integer constant (C decimal, octal or hexadecimal) */
static int parse_const(const char *s, const char **end, long *out)
{
    unsigned long v = 0;
    const char *p = s;
    int base = 10, any = 0;
    if (*p == '0') {
        if (p[1] == 'x' || p[1] == 'X') {
            base = 16;
            p += 2;
        } else
            base = 8;
    }
    for (;; p++) {
        int d;
        if (isdigit((unsigned char)*p))
            d = *p - '0';
        else if (base == 16 && isxdigit((unsigned char)*p))
            d = tolower((unsigned char)*p) - 'a' + 10;
        else
            break;
        if (d >= base)
            return 0;
        v = v * (unsigned long)base + (unsigned long)d;
        any = 1;
    }
    if (!any && base != 8)
        return 0;
    if (isalnum((unsigned char)*p) || *p == '_')
        return 0;
    *end = p;
    *out = (long)v;
    return 1;
}

static AVal num(ANum n)
{
    const char *e;
    long v;
    if (n.bad || !parse_const(n.text, &e, &v))
        aerror("invalid number");
    return mk(v);
}

static long var_value(const char *name)
{
    const char *s = var_get(name);
    const char *e;
    long v;
    int neg = 0;
    if (!s) {
        if (opts[O_NOUNSET]) {
            arith_scan_end();
            sh_error("%s: parameter not set", name);
        }
        return 0;
    }
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (!*s)
        return 0;
    if (*s == '-' || *s == '+') {
        neg = *s == '-';
        s++;
    }
    if (!parse_const(s, &e, &v))
        aerror("invalid number in variable");
    while (*e == ' ' || *e == '\t' || *e == '\n') e++;
    if (*e)
        aerror("invalid number in variable");
    return neg ? (long)(0UL - (unsigned long)v) : v;
}

static long rv(AVal x)
{
    if (x.name)
        return noeval ? 0 : var_value(x.name);
    return x.v;
}

static void assign(const char *name, long v)
{
    char buf[32];
    if (noeval)
        return;
    snprintf(buf, sizeof buf, "%ld", v);
    if (var_set(name, buf, 0) < 0) {
        arith_scan_end();
        sh_error("%s: is read only", name);
    }
}

/* ++ and -- : the new value, or the old one for the postfix forms */
static long incr(char *name, long by, int post)
{
    long old = rv(nm(name));
    long v = (long)((unsigned long)old + (unsigned long)by);
    assign(name, v);
    return post ? old : v;
}

static long apply(const char *op, long x, long y)
{
    unsigned long ux = (unsigned long)x, uy = (unsigned long)y;
    switch (op[0]) {
    case '+': return (long)(ux + uy);
    case '-': return (long)(ux - uy);
    case '*': return (long)(ux * uy);
    case '/': case '%':
        if (noeval) return 0;
        if (y == 0) aerror("division by zero");
        if (x == LONG_MIN && y == -1) return op[0] == '/' ? LONG_MIN : 0;
        return op[0] == '/' ? x / y : x % y;
    case '<':
        if (op[1] == '<') return (long)(ux << (uy & 63));
        if (op[1] == '=') return x <= y;
        return x < y;
    case '>':
        if (op[1] == '>') return x >> (uy & 63);
        if (op[1] == '=') return x >= y;
        return x > y;
    case '=': return x == y;
    case '!': return x != y;
    case '&': return x & y;
    case '|': return x | y;
    case '^': return x ^ y;
    }
    return 0;
}

long arith_eval(const char *expr)
{
    const char *p = expr;
    while (*p == ' ' || *p == '\t' || *p == '\n')
        p++;
    if (!*p)
        return 0;
    aexpr = expr;
    noeval = 0;
    arith_scan_begin(expr);
    if (yyparse() != 0)
        aerror("syntax error");
    arith_scan_end();
    return result;
}
