%{
/*
 * awk.y - the grammar of awk (XCU awk, "Grammar"), building the syntax
 * tree that run.c interprets.  Tokens come from awk.l.
 *
 * The structure follows the precedence of the standard with the
 * choices of the earlier hand-written parser kept exactly:
 *   - "!" applies to a unary operand (it binds tighter than the
 *     comparisons), and "^" takes a unary right operand (2^-1);
 *   - concatenation takes an additive right operand that does not start
 *     with "+" or "-" (nor with a regular expression or getline);
 *   - "|" getline is at the level of concatenation;
 *   - comparisons do not associate;
 *   - in the expression list of print and printf, ">" is not a
 *     comparison outside parentheses, brackets and argument lists (the
 *     p* rules);
 *   - an assignment needs a variable, element or field on the left;
 *   - a parenthesized list "(a, b)" may appear only in print and printf
 *     and before "in".
 * Newlines are allowed where the earlier parser skipped them.  The
 * error rules give the same messages: each is placed after the token
 * that expects something, so that the parser reports the error where
 * the expected token is missing.
 */
#include "awk.h"
#include "parse.h"

#define YYMAXDEPTH 10000000

static Node *node(int type, const Tok *t);
static Node *var_node(Str *name, const Tok *t);
static int is_lvalue(Node *n);
static List list1(Node *n);
static List list_add(List l, Node *n);
static Node *block_node(List l, const Tok *t);
static Node *check_expr(Node *n);
static Node *print_node(const Tok *kw, List args, const Tok *redir, Node *target);
static void empty_print(const Tok *kw);
static Func *def_func(const Tok *t);
static void def_param(const Tok *t);
static Node *call_node(const Tok *t);
static int at_stmt_end(int type);
static int is_redir(int type);
_Noreturn static void err(const char *msg);
static void pipe_check(void);

static Func *cur_func;          /* the function being defined */
static Func *new_func;          /* the function whose parameters are read */
static int new_func_cap;
static int loop_depth;
void yyerror(const char *s);
%}

%union {
    Tok *t;
    Node *n;
    List l;
    Func *f;
}

%token <t> NEWLINE NUMBER STRING ERE ERE_BAD NAME FUNC_NAME BUILTIN
%token <t> ASSIGNOP RELOP INCR DECR AND OR NOMATCH APPEND PIPE_GETLINE ITEMSEP VTERM
%token <t> Begin End Break Continue Delete Do Else Exit For Function Getline If In
%token <t> Next Nextfile Print Printf Return While
%token <t> '{' '}' '(' ')' '[' ']' ',' ';' '+' '-' '*' '/' '%' '^' '!' '>' '<' '|'
%token <t> '?' ':' '~' '$'

%type <n> item_block stmt body simple simple_print delete_s exit_s return_s for_init
%type <n> expr ternary or_e and_e in_e match_e cmp_e
%type <n> pexpr pternary por pand pin pmatch pcmp
%type <n> concat additive mul unary power postfix primary primary_np prim_core prim_inc
%type <n> additive_nc mul_nc unary_nc power_nc postfix_nc primary_nc
%type <n> lvalue lval2 name_ref name_tok dollar_op group call call_head builtin getline_p opt_glv do_body
%type <n> array_name target opt_expr opt_simple
%type <l> bitems exprlist exprlist_nl plist plist_chk args opt_args
%type <t> redir break_kw continue_kw return_kw print_kw
%type <f> fname

%expect 36
%start program
%%

/* ---------- the program: items separated by newlines and semicolons ---------- */

program     : seps
            | items
            ;
seps        : /* empty */
            | seps sep
            | items sep
            | seps error                { err("unexpected token"); }
            | items error               { err("unexpected token after pattern or action"); }
            ;
items       : seps item
            ;
sep         : NEWLINE | ';' | ITEMSEP
            ;

item        : expr                      { add_rule($1, NULL, NULL); }
            | expr item_block           { add_rule($1, NULL, $2); }
            | expr ',' nls expr         { add_rule($1, $4, NULL); }
            | expr ',' nls expr item_block  { add_rule($1, $4, $5); }
            | expr ',' nls error        { err("unexpected token"); }
            | item_block                { add_rule(NULL, NULL, $1); }
            | Begin nls item_block      { add_begin($3); }
            | Begin nls error           { err("BEGIN requires an action"); }
            | End nls item_block        { add_end($3); }
            | End nls error             { err("END requires an action"); }
            | Function fname '(' nls params ')' nls { cur_func = $2; } item_block
                { $2->body = $9; cur_func = NULL; }
            | Function error            { err("expected function name"); }
            | Function fname error      { err("expected ("); }
            | Function fname '(' nls params error   { err("expected )"); }
            | Function fname '(' nls params ')' nls error   { err("expected {"); }
            ;

fname       : NAME                      { $$ = def_func($1); }
            | FUNC_NAME                 { $$ = def_func($1); }
            ;
params      : /* empty */
            | pseq
            | pseq_c
            ;
pseq        : param nls
            | pseq_c param nls
            ;
pseq_c      : pseq ',' nls
            ;
param       : NAME                      { def_param($1); }
            ;

item_block  : '{' bitems '}'            { $$ = block_node($2, $1); }
            ;

nls         : /* empty */
            | nls NEWLINE
            ;

/* ---------- statements ---------- */

bitems      : /* empty */               { $$.head = $$.last = NULL; $$.n = 0; }
            | bitems NEWLINE            { $$ = $1; }
            | bitems ';'                { $$ = $1; }
            | bitems VTERM              { $$ = $1; }
            | bitems stmt               { $$ = $2 ? list_add($1, $2) : $1; }
            | bitems error
                { err(lex_la->type == TK_EOF ? "unexpected end of program, missing }"
                                             : "unexpected token"); }
            ;

/* a statement in a block; one that ends a block needs no terminator */
stmt        : item_block nls            { $$ = $1; }
            | simple end                { $$ = $1; }
            | simple error              { err("statement not terminated"); }
            | break_kw end              { $$ = node(N_BREAK, $1); }
            | break_kw error            { err("statement not terminated"); }
            | continue_kw end           { $$ = node(N_CONTINUE, $1); }
            | continue_kw error         { err("statement not terminated"); }
            | Next end                  { $$ = node(N_NEXT, $1); }
            | Next error                { err("statement not terminated"); }
            | Nextfile end              { $$ = node(N_NEXTFILE, $1); }
            | Nextfile error            { err("statement not terminated"); }
            | exit_s end                { $$ = $1; }
            | exit_s error              { err("statement not terminated"); }
            | return_s end              { $$ = $1; }
            | return_s error            { err("statement not terminated"); }
            | If '(' expr ')' nls body
                { $$ = node(N_IF, $1); $$->a = $3; $$->b = $6; }
            | If '(' expr ')' nls body Else nls body
                { $$ = node(N_IF, $1); $$->a = $3; $$->b = $6; $$->c = $9; }
            | If error                  { err("expected ("); }
            | If '(' error              { err("unexpected token"); }
            | If '(' expr error         { err("expected )"); }
            | While '(' expr ')' { loop_depth++; } nls body
                { loop_depth--; $$ = node(N_WHILE, $1); $$->a = $3; $$->b = $7; }
            | While error               { err("expected ("); }
            | While '(' error           { err("unexpected token"); }
            | While '(' expr error      { err("expected )"); }
            | do_body While '(' expr ')' end    { $$ = $1; $$->b = $4; }
            | do_body While '(' expr ')' error  { err("statement not terminated"); }
            | do_body error                     { err("expected while"); }
            | do_body While error               { err("expected ("); }
            | do_body While '(' error           { err("unexpected token"); }
            | do_body While '(' expr error      { err("expected )"); }
            | For '(' opt_simple ';' nls opt_expr ';' nls opt_simple ')' { loop_depth++; } nls body
                { loop_depth--; $$ = node(N_FOR, $1); $$->a = $3; $$->b = $6; $$->c = $9; $$->d = $13; }
            | For '(' for_init ')'
                {
                    Node *in = $3->type == N_EXPRSTMT ? $3->a : NULL;
                    if (!in || in->type != N_IN || in->a->next ||
                        (in->a->type != N_GVAR && in->a->type != N_LVAR))
                        syntax_error_at($4, "expected ;");
                    loop_depth++;
                } nls body
                {
                    loop_depth--;
                    $$ = node(N_FORIN, $1);
                    $$->a = $3->a->a;
                    $$->b = $3->a->b;
                    $$->c = $7;
                }
            | For error                 { err("expected ("); }
            | For '(' error             { err("unexpected token"); }
            | For '(' opt_simple error  { err("expected ;"); }
            | For '(' opt_simple ';' nls error  { err("unexpected token"); }
            | For '(' opt_simple ';' nls opt_expr error  { err("expected ;"); }
            | For '(' opt_simple ';' nls opt_expr ';' nls error  { err("unexpected token"); }
            | For '(' opt_simple ';' nls opt_expr ';' nls opt_simple error  { err("expected )"); }
            ;

do_body     : Do nls { loop_depth++; } body
                { loop_depth--; $$ = node(N_DO, $1); $$->a = $4; }
            ;

/* the statement controlled by if, else, while, do or for */
body        : stmt                      { $$ = $1; }
            | ';' nls                   { $$ = NULL; }
            | error                     { err("unexpected token"); }
            ;

/* the end of a simple statement: a semicolon or newline (and the newlines
   after it), or the end of the block (VTERM, before "}" or the end of
   the program, which the enclosing construct then reports) */
end         : ';' nls
            | NEWLINE nls
            | VTERM
            ;

break_kw    : Break
                { if (!loop_depth) syntax_error_at($1, "break outside a loop"); $$ = $1; }
            ;
continue_kw : Continue
                { if (!loop_depth) syntax_error_at($1, "continue outside a loop"); $$ = $1; }
            ;
return_kw   : Return
                { if (!cur_func) syntax_error_at($1, "return outside function body"); $$ = $1; }
            ;
exit_s      : Exit
                { if (!at_stmt_end(lex_la->type)) err("unexpected token");
                  $$ = node(N_EXIT, $1); }
            | Exit expr                 { $$ = node(N_EXIT, $1); $$->a = $2; }
            ;
return_s    : return_kw
                { if (!at_stmt_end(lex_la->type)) err("unexpected token");
                  $$ = node(N_RETURN, $1); }
            | return_kw expr            { $$ = node(N_RETURN, $1); $$->a = $2; }
            ;

opt_simple  : /* empty */               { $$ = NULL; }
            | simple                    { $$ = $1; }
            ;
for_init    : simple                    { $$ = $1; }
            ;
opt_expr    : /* empty */               { $$ = NULL; }
            | expr                      { $$ = $1; }
            ;

simple      : simple_print              { $$ = $1; }
            | delete_s                  { $$ = $1; }
            | expr                      { $$ = node(N_EXPRSTMT, NULL); $$->a = $1; }
            ;

print_kw    : Print                     { $$ = $1; }
            | Printf
                {
                    /* the format is missing when the list is empty */
                    lex_need_lookahead();
                    if (at_stmt_end(lex_la->type) || is_redir(lex_la->type))
                        err("printf: no format");
                    $$ = $1;
                }
            ;
simple_print: print_kw
                { List none = { NULL, NULL, 0 };
                  empty_print($1);
                  $$ = print_node($1, none, NULL, NULL); }
            | print_kw redir target
                { List none = { NULL, NULL, 0 };
                  $$ = print_node($1, none, $2, $3); }
            | print_kw plist_chk        { $$ = print_node($1, $2, NULL, NULL); }
            | print_kw plist_chk redir target  { $$ = print_node($1, $2, $3, $4); }
            ;
redir       : '>'                       { $$ = $1; }
            | APPEND                    { $$ = $1; }
            | '|'                       { $$ = $1; }
            ;
target      : concat                    { $$ = $1; }
            | error                     { err("unexpected token"); }
            ;
plist       : pexpr                     { $$ = list1($1); }
            | plist ',' nls pexpr       { $$ = list_add($1, $4); }
            | plist ',' nls error       { err("unexpected token"); }
            ;
plist_chk   : plist
                {
                    $$ = $1;
                    if ($1.n == 1 && $1.head->type == N_GROUPLIST) {
                        $$.n = $1.head->nargs;
                        $$.head = $1.head->a;
                    } else {
                        Node *p;
                        for (p = $1.head; p; p = p->next)
                            if (p->type == N_GROUPLIST)
                                err("invalid use of parenthesized list");
                    }
                }
            ;

delete_s    : Delete array_name         { $$ = node(N_DELETE, $1); $$->a = $2; }
            | Delete array_name '[' exprlist ']'
                { $$ = node(N_DELETE, $1); $$->a = $2; $$->b = $4.head; }
            | Delete error              { err("expected array name"); }
            | Delete array_name '[' error           { err("unexpected token"); }
            | Delete array_name '[' exprlist error  { err("expected ]"); }
            ;

array_name  : NAME                      { $$ = var_node($1->str, $1); }
            ;

/* ---------- expressions ---------- */

expr        : ternary                   { $$ = check_expr($1); }
            | lvalue ASSIGNOP nls expr
                { $$ = node(N_ASSIGN, $2); $$->op = $2->type; $$->a = $1; $$->b = $4; }
            | lvalue ASSIGNOP nls error { err("unexpected token"); }
            ;
ternary     : or_e                      { $$ = $1; }
            | or_e '?' nls expr nls ':' nls expr
                { $$ = node(N_COND, $2); $$->a = $1; $$->b = $4; $$->c = $8; }
            | or_e '?' nls error        { err("unexpected token"); }
            | or_e '?' nls expr nls error   { err("expected :"); }
            | or_e '?' nls expr nls ':' nls error   { err("unexpected token"); }
            ;
or_e        : and_e                     { $$ = $1; }
            | or_e OR nls and_e         { $$ = node(N_OR, $2); $$->a = $1; $$->b = $4; }
            | or_e OR nls error         { err("unexpected token"); }
            ;
and_e       : in_e                      { $$ = $1; }
            | and_e AND nls in_e        { $$ = node(N_AND, $2); $$->a = $1; $$->b = $4; }
            | and_e AND nls error       { err("unexpected token"); }
            ;
in_e        : match_e                   { $$ = $1; }
            | in_e In array_name        { $$ = node(N_IN, $2); $$->a = $1; $$->b = $3; }
            | in_e In error             { err("expected array name"); }
            ;
match_e     : cmp_e                     { $$ = $1; }
            | match_e '~' cmp_e         { $$ = node(N_MATCH, $2); $$->a = $1; $$->b = $3; }
            | match_e NOMATCH cmp_e     { $$ = node(N_MATCH, $2); $$->op = 1; $$->a = $1; $$->b = $3; }
            | match_e '~' error         { err("unexpected token"); }
            | match_e NOMATCH error     { err("unexpected token"); }
            ;
cmp_e       : concat                    { $$ = $1; }
            | concat '<' concat         { $$ = node(N_CMP, $2); $$->op = TK_LT; $$->a = $1; $$->b = $3; }
            | concat '>' concat         { $$ = node(N_CMP, $2); $$->op = TK_GT; $$->a = $1; $$->b = $3; }
            | concat RELOP concat       { $$ = node(N_CMP, $2); $$->op = $2->type; $$->a = $1; $$->b = $3; }
            | concat '<' error          { err("unexpected token"); }
            | concat '>' error          { err("unexpected token"); }
            | concat RELOP error        { err("unexpected token"); }
            ;

/* the same, in the list of print and printf: no ">" */
pexpr       : pternary                  { $$ = $1; }
            | lvalue ASSIGNOP nls pexpr
                { $$ = node(N_ASSIGN, $2); $$->op = $2->type; $$->a = $1; $$->b = $4; }
            | lvalue ASSIGNOP nls error { err("unexpected token"); }
            ;
pternary    : por                       { $$ = $1; }
            | por '?' nls pexpr nls ':' nls pexpr
                { $$ = node(N_COND, $2); $$->a = $1; $$->b = $4; $$->c = $8; }
            | por '?' nls error         { err("unexpected token"); }
            | por '?' nls pexpr nls error   { err("expected :"); }
            | por '?' nls pexpr nls ':' nls error   { err("unexpected token"); }
            ;
por         : pand                      { $$ = $1; }
            | por OR nls pand           { $$ = node(N_OR, $2); $$->a = $1; $$->b = $4; }
            | por OR nls error          { err("unexpected token"); }
            ;
pand        : pin                       { $$ = $1; }
            | pand AND nls pin          { $$ = node(N_AND, $2); $$->a = $1; $$->b = $4; }
            | pand AND nls error        { err("unexpected token"); }
            ;
pin         : pmatch                    { $$ = $1; }
            | pin In array_name         { $$ = node(N_IN, $2); $$->a = $1; $$->b = $3; }
            | pin In error              { err("expected array name"); }
            ;
pmatch      : pcmp                      { $$ = $1; }
            | pmatch '~' pcmp           { $$ = node(N_MATCH, $2); $$->a = $1; $$->b = $3; }
            | pmatch NOMATCH pcmp       { $$ = node(N_MATCH, $2); $$->op = 1; $$->a = $1; $$->b = $3; }
            | pmatch '~' error          { err("unexpected token"); }
            | pmatch NOMATCH error      { err("unexpected token"); }
            ;
pcmp        : concat                    { $$ = $1; }
            | concat '<' concat         { $$ = node(N_CMP, $2); $$->op = TK_LT; $$->a = $1; $$->b = $3; }
            | concat RELOP concat       { $$ = node(N_CMP, $2); $$->op = $2->type; $$->a = $1; $$->b = $3; }
            | concat '<' error          { err("unexpected token"); }
            | concat RELOP error        { err("unexpected token"); }
            ;

concat      : additive                  { $$ = $1; pipe_check(); }
            | concat additive_nc        { $$ = node(N_CONCAT, NULL); $$->a = $1; $$->b = $2; pipe_check(); }
            | concat PIPE_GETLINE Getline opt_glv
                { $$ = node(N_GETLINE, $2); $$->op = 2; $$->b = $1; $$->a = $4; pipe_check(); }
            ;

additive    : mul                       { $$ = $1; }
            | additive '+' mul          { $$ = node(N_ARITH, $2); $$->op = '+'; $$->a = $1; $$->b = $3; }
            | additive '-' mul          { $$ = node(N_ARITH, $2); $$->op = '-'; $$->a = $1; $$->b = $3; }
            | additive '+' error        { err("unexpected token"); }
            | additive '-' error        { err("unexpected token"); }
            ;
mul         : unary                     { $$ = $1; }
            | mul '*' unary             { $$ = node(N_ARITH, $2); $$->op = '*'; $$->a = $1; $$->b = $3; }
            | mul '/' unary             { $$ = node(N_ARITH, $2); $$->op = '/'; $$->a = $1; $$->b = $3; }
            | mul '%' unary             { $$ = node(N_ARITH, $2); $$->op = '%'; $$->a = $1; $$->b = $3; }
            | mul '*' error             { err("unexpected token"); }
            | mul '/' error             { err("unexpected token"); }
            | mul '%' error             { err("unexpected token"); }
            ;
unary       : power                     { $$ = $1; }
            | '!' unary                 { $$ = node(N_NOT, $1); $$->a = $2; }
            | '-' unary                 { $$ = node(N_NEG, $1); $$->a = $2; }
            | '+' unary                 { $$ = node(N_PLUS, $1); $$->a = $2; }
            | '!' error                 { err("unexpected token"); }
            | '-' error                 { err("unexpected token"); }
            | '+' error                 { err("unexpected token"); }
            ;
power       : postfix                   { $$ = $1; }
            | postfix '^' unary         { $$ = node(N_ARITH, $2); $$->op = '^'; $$->a = $1; $$->b = $3; }
            | postfix '^' error         { err("unexpected token"); }
            ;
postfix     : primary                   { $$ = $1; }
            | lvalue INCR               { $$ = node(N_POSTINC, $2); $$->a = $1; }
            | lvalue DECR               { $$ = node(N_POSTDEC, $2); $$->a = $1; }
            ;

/* the right operand of a concatenation */
additive_nc : mul_nc                    { $$ = $1; }
            | additive_nc '+' mul       { $$ = node(N_ARITH, $2); $$->op = '+'; $$->a = $1; $$->b = $3; }
            | additive_nc '-' mul       { $$ = node(N_ARITH, $2); $$->op = '-'; $$->a = $1; $$->b = $3; }
            | additive_nc '+' error     { err("unexpected token"); }
            | additive_nc '-' error     { err("unexpected token"); }
            ;
mul_nc      : unary_nc                  { $$ = $1; }
            | mul_nc '*' unary          { $$ = node(N_ARITH, $2); $$->op = '*'; $$->a = $1; $$->b = $3; }
            | mul_nc '/' unary          { $$ = node(N_ARITH, $2); $$->op = '/'; $$->a = $1; $$->b = $3; }
            | mul_nc '%' unary          { $$ = node(N_ARITH, $2); $$->op = '%'; $$->a = $1; $$->b = $3; }
            | mul_nc '*' error          { err("unexpected token"); }
            | mul_nc '/' error          { err("unexpected token"); }
            | mul_nc '%' error          { err("unexpected token"); }
            ;
unary_nc    : power_nc                  { $$ = $1; }
            | '!' unary                 { $$ = node(N_NOT, $1); $$->a = $2; }
            | '!' error                 { err("unexpected token"); }
            ;
power_nc    : postfix_nc                { $$ = $1; }
            | postfix_nc '^' unary      { $$ = node(N_ARITH, $2); $$->op = '^'; $$->a = $1; $$->b = $3; }
            | postfix_nc '^' error      { err("unexpected token"); }
            ;
postfix_nc  : primary_nc                { $$ = $1; }
            | lvalue INCR               { $$ = node(N_POSTINC, $2); $$->a = $1; }
            | lvalue DECR               { $$ = node(N_POSTDEC, $2); $$->a = $1; }
            ;

primary     : lvalue                    { $$ = $1; }
            | prim_core                 { $$ = $1; }
            | prim_inc                  { $$ = $1; }
            | ERE
                { $$ = node(N_ERE, $1); $$->str = $1->str;
                  $$->re = re_compile($1->str->s, $1->str->len, 0); }
            | ERE_BAD                   { syntax_error_at($1, "non-terminated regular expression"); }
            | getline_p                 { $$ = $1; }
            ;
/* the operand of "$" */
primary_np  : lvalue                    { $$ = $1; }
            | prim_core                 { $$ = $1; }
            | ERE
                { $$ = node(N_ERE, $1); $$->str = $1->str;
                  $$->re = re_compile($1->str->s, $1->str->len, 0); }
            | ERE_BAD                   { syntax_error_at($1, "non-terminated regular expression"); }
            | getline_p                 { $$ = $1; }
            ;
primary_nc  : lvalue                    { $$ = $1; }
            | prim_core                 { $$ = $1; }
            | prim_inc                  { $$ = $1; }
            ;
prim_core   : NUMBER                    { $$ = node(N_NUM, $1); $$->num = $1->num; }
            | STRING                    { $$ = node(N_STR, $1); $$->str = $1->str; }
            | group                     { $$ = $1; }
            | call                      { $$ = $1; }
            | builtin                   { $$ = $1; }
            ;
prim_inc    : INCR lval2                { $$ = node(N_PREINC, $1); $$->a = $2; }
            | DECR lval2                { $$ = node(N_PREDEC, $1); $$->a = $2; }
            | INCR error                { err("++ or -- requires an lvalue"); }
            | DECR error                { err("++ or -- requires an lvalue"); }
            ;
lval2       : name_ref                  { $$ = $1; }
            | '$' dollar_op             { $$ = node(N_FIELD, $1); $$->a = $2; }
            | '$' error                 { err("unexpected token"); }
            ;

lvalue      : name_ref                  { $$ = $1; }
            | '$' dollar_op             { $$ = node(N_FIELD, $1); $$->a = $2; }
            | '$' error                 { err("unexpected token"); }
            ;
name_ref    : name_tok                  { $$ = $1; }
            | name_tok '[' exprlist ']'
                { $$ = node(N_INDEX, $2); $$->a = $1; $$->b = $3.head; }
            | name_tok '[' error        { err("unexpected token"); }
            | name_tok '[' exprlist error   { err("expected ]"); }
            ;
/* the variable is made when its name is read */
name_tok    : NAME                      { $$ = var_node($1->str, $1); }
            ;
exprlist    : expr                      { $$ = list1($1); }
            | exprlist ',' nls expr     { $$ = list_add($1, $4); }
            | exprlist ',' nls error    { err("unexpected token"); }
            ;

/* the operand of "$": the highest precedence, but with prefix operators */
dollar_op   : INCR dollar_op
                {
                    lex_need_lookahead();
                    if (!is_lvalue($2))
                        err("++ or -- applied to non-lvalue");
                    $$ = node(N_PREINC, $1); $$->a = $2;
                }
            | DECR dollar_op
                {
                    lex_need_lookahead();
                    if (!is_lvalue($2))
                        err("++ or -- applied to non-lvalue");
                    $$ = node(N_PREDEC, $1); $$->a = $2;
                }
            | '-' dollar_op             { $$ = node(N_NEG, $1); $$->a = $2; }
            | '+' dollar_op             { $$ = node(N_PLUS, $1); $$->a = $2; }
            | '!' dollar_op             { $$ = node(N_NOT, $1); $$->a = $2; }
            | primary_np                { $$ = $1; }
            | INCR error                { err("unexpected token"); }
            | DECR error                { err("unexpected token"); }
            | '-' error                 { err("unexpected token"); }
            | '+' error                 { err("unexpected token"); }
            | '!' error                 { err("unexpected token"); }
            ;

group       : '(' nls expr nls ')'      { $$ = node(N_GROUP, $1); $$->a = $3; }
            | '(' nls expr nls ',' nls exprlist_nl ')'
                { $$ = node(N_GROUPLIST, $1); $$->a = $3; $3->next = $7.head; $$->nargs = 1 + $7.n; }
            | '(' nls expr nls ',' nls exprlist_nl ')' In array_name
                { $$ = node(N_IN, $9); $$->a = $3; $3->next = $7.head; $$->b = $10; }
            | '(' nls expr nls ',' nls exprlist_nl ')' In error  { err("expected array name"); }
            | '(' nls expr nls ',' nls exprlist_nl error  { err("expected )"); }
            | '(' nls error             { err("unexpected token"); }
            | '(' nls expr nls error    { err("expected )"); }
            ;
/* after the first comma in parentheses: expressions, newlines around commas */
exprlist_nl : expr nls                  { $$ = list1($1); }
            | exprlist_nl ',' nls expr nls  { $$ = list_add($1, $4); }
            | exprlist_nl ',' nls error { err("unexpected token"); }
            | error                     { err("unexpected token"); }
            ;

call        : call_head '(' opt_args ')'
                { $$ = $1; $$->a = $3.head; $$->nargs = $3.n; }
            | call_head '(' opt_args error  { err("expected )"); }
            ;
call_head   : FUNC_NAME                 { $$ = call_node($1); }
            ;
builtin     : BUILTIN '(' opt_args ')'
                { $$ = node(N_BUILTIN, $1); $$->idx = $1->bi; $$->str = $1->str;
                  $$->a = $3.head; $$->nargs = $3.n; }
            | BUILTIN '(' opt_args error  { err("expected )"); }
            | BUILTIN
                {
                    if ($1->bi != B_LENGTH)
                        err("built-in function requires an argument list");
                    $$ = node(N_BUILTIN, $1); $$->idx = $1->bi; $$->str = $1->str;
                }
            ;
opt_args    : nls                       { $$.head = $$.last = NULL; $$.n = 0; }
            | nls args                  { $$ = $2; }
            | nls error                 { err("unexpected token"); }
            ;
args        : expr nls                  { $$ = list1($1); }
            | args ',' nls expr nls     { $$ = list_add($1, $4); }
            | args ',' nls error        { err("unexpected token"); }
            ;

getline_p   : Getline opt_glv           { $$ = node(N_GETLINE, $1); $$->a = $2; }
            | Getline opt_glv '<' additive
                { $$ = node(N_GETLINE, $1); $$->a = $2; $$->op = 1; $$->b = $4; }
            | Getline opt_glv '<' error { err("unexpected token"); }
            ;
opt_glv     : /* empty */               { $$ = NULL; }
            | name_ref                  { $$ = $1; }
            | '$' dollar_op             { $$ = node(N_FIELD, $1); $$->a = $2; }
            | '$' error                 { err("unexpected token"); }
            ;

%%

void yyerror(const char *s)
{
    (void)s;            /* reported by the error rules */
}

/* the lookahead is the token the earlier parser would have read by now */
void lex_need_lookahead(void)
{
    if (yychar == YYEMPTY)
        yychar = yylex();
}

/*
 * At the end of each part of a concatenation, a "|" may be followed by
 * getline: the token after it decides, read now as the earlier parser
 * did.
 */
static void pipe_check(void)
{
    lex_need_lookahead();
    if (yychar == '|' && lex_pipe_getline())
        yychar = PIPE_GETLINE;
}

_Noreturn static void err(const char *msg)
{
    syntax_error_at(lex_la, msg);
}

static Node *node(int type, const Tok *t)
{
    Node *n = xcalloc(1, sizeof *n);
    n->type = type;
    n->line = t ? t->line : lex_la ? lex_la->line : 0;
    return n;
}

static int is_lvalue(Node *n)
{
    return n && (n->type == N_GVAR || n->type == N_LVAR || n->type == N_INDEX ||
                 n->type == N_FIELD);
}

static Node *var_node(Str *name, const Tok *t)
{
    Node *n;
    if (cur_func) {
        int i;
        for (i = 0; i < cur_func->nparams; i++)
            if (strcmp(cur_func->params[i], name->s) == 0) {
                n = node(N_LVAR, t);
                n->idx = i;
                n->str = name;
                return n;
            }
    }
    n = node(N_GVAR, t);
    n->idx = global_lookup(name->s, 1);
    n->str = name;
    return n;
}

static List list1(Node *n)
{
    List l;
    l.head = l.last = n;
    l.n = 1;
    return l;
}

static List list_add(List l, Node *n)
{
    if (!l.head)
        l.head = n;
    else
        l.last->next = n;
    l.last = n;
    l.n++;
    return l;
}

static Node *block_node(List l, const Tok *t)
{
    Node *n = node(N_BLOCK, t);
    n->a = l.head;
    return n;
}

/* an expression where a parenthesized list is not allowed */
static Node *check_expr(Node *n)
{
    if (n->type == N_GROUPLIST)
        err("parenthesized expression list not allowed here");
    return n;
}

static int at_stmt_end(int type)
{
    return type == TK_SEMI || type == TK_NEWLINE || type == TK_RBRACE || type == TK_EOF;
}

static int is_redir(int type)
{
    return type == TK_GT || type == TK_APPEND || type == TK_PIPE;
}

/* print or printf with nothing to print: only before the end of the
   statement or a redirection */
static void empty_print(const Tok *kw)
{
    (void)kw;
    if (!at_stmt_end(lex_la->type) && !is_redir(lex_la->type))
        err("unexpected token");
}

static Node *print_node(const Tok *kw, List args, const Tok *redir, Node *target)
{
    Node *n = node(kw->type == TK_PRINT ? N_PRINT : N_PRINTF, kw);
    n->a = args.head;
    n->nargs = args.n;
    if (redir) {
        n->op = redir->type == TK_GT ? '>' : redir->type == TK_APPEND ? 'a' : '|';
        n->b = target;
    }
    return n;
}

/* "function NAME": the function, which must not be defined already */
static Func *def_func(const Tok *t)
{
    int fi = func_lookup(t->str->s);
    Func *f = funcs[fi];
    if (f->defined)
        syntax_error_at(t, "function redefined");
    f->defined = 1;
    f->line = t->line;
    new_func = f;
    new_func_cap = 0;
    return f;
}

static void def_param(const Tok *t)
{
    Func *f = new_func;
    int i;
    if (strcmp(t->str->s, f->name) == 0)
        syntax_error_at(t, "function name used as parameter");
    for (i = 0; i < f->nparams; i++)
        if (strcmp(f->params[i], t->str->s) == 0)
            syntax_error_at(t, "duplicate parameter name");
    if (f->nparams == new_func_cap) {
        new_func_cap = new_func_cap ? new_func_cap * 2 : 8;
        f->params = xrealloc(f->params, (size_t)new_func_cap * sizeof *f->params);
    }
    f->params[f->nparams++] = xstrdup(t->str->s);
}

/* a call of a user function: not one of the parameters */
static Node *call_node(const Tok *t)
{
    Node *n;
    if (cur_func) {
        int i;
        for (i = 0; i < cur_func->nparams; i++)
            if (strcmp(cur_func->params[i], t->str->s) == 0)
                syntax_error_at(t, "function parameter used as function");
    }
    n = node(N_CALL, t);
    n->idx = func_lookup(t->str->s);
    n->str = t->str;
    return n;
}

void parser_reset(void)
{
    cur_func = new_func = NULL;
    loop_depth = 0;
}
