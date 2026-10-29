%{
/*
 * bc.y - the grammar of bc (POSIX.1-2024, XCU bc, "Grammar"), with
 * precedence declarations for the operator table.  yacc builds the
 * parser (bc.tab.c) from this file.
 *
 * Extensions: newlines after the ) of if, while and for and after the {
 * of a function, return without parentheses, else, continue, halt, print, empty for clauses,
 * relational operators in any expression, and !, && and ||.
 */
#include "bc.h"

static Node *mk_named(Node *n);
%}

%union {
    Node *n;
    char *s;
    int op;
    Param *p;
}

%token <s> STRING LETTER NUMBER
%token <op> MUL_OP ASSIGN_OP REL_OP INCR_DECR
%token NEWLINE Define Break Quit Length Return For If While Sqrt Scale Ibase Obase Auto
%token Else Continue Halt Print AND OR

%type <n> semicolon_list statement_list statement opt_statement_list expression named_expression
%type <n> opt_argument_list argument_list opt_expression print_list print_item
%type <p> opt_define_list define_list opt_auto_define_list

%left OR
%left AND
%nonassoc '!'
%nonassoc REL_OP
%right ASSIGN_OP
%left '+' '-'
%left MUL_OP
%right '^'
%nonassoc UMINUS
%nonassoc INCR_DECR
%nonassoc IF_WITHOUT_ELSE
%nonassoc Else

%expect 1
%start program
%%

program              : /* empty */
                     | program input_item
                     ;

input_item           : semicolon_list NEWLINE
                         { run_item($1); out_flush(); }
                     | function
                         { out_flush(); }
                     | error NEWLINE
                         { yyerrok; out_flush(); }
                     ;

semicolon_list       : /* empty */
                         { $$ = NULL; }
                     | statement
                         { $$ = $1; }
                     | semicolon_list ';' statement
                         { $$ = append($1, $3); }
                     | semicolon_list ';'
                         { $$ = $1; }
                     ;

statement_list       : /* empty */
                         { $$ = NULL; }
                     | statement
                         { $$ = $1; }
                     | statement_list NEWLINE
                         { $$ = $1; }
                     | statement_list NEWLINE statement
                         { $$ = append($1, $3); }
                     | statement_list ';'
                         { $$ = $1; }
                     | statement_list ';' statement
                         { $$ = append($1, $3); }
                     ;

opt_newlines         : /* empty */
                     | opt_newlines NEWLINE
                     ;

statement            : expression
                         { $$ = node(S_EXPR, $1, NULL); }
                     | STRING
                         { $$ = node(S_STRING, NULL, NULL); $$->str = $1; }
                     | Break
                         { $$ = node(S_BREAK, NULL, NULL); }
                     | Continue
                         { $$ = node(S_CONTINUE, NULL, NULL); }
                     | Quit
                         { bc_quit(0); $$ = NULL; }
                     | Halt
                         { $$ = node(S_HALT, NULL, NULL); }
                     | Return
                         { $$ = node(S_RETURN, NULL, NULL); }
                     | Return '(' ')'
                         { $$ = node(S_RETURN, NULL, NULL); }
                     | Return expression
                         { $$ = node(S_RETURN, $2, NULL); }
                     | For '(' opt_expression ';' opt_expression ';' opt_expression ')' opt_newlines statement
                         { $$ = node(S_FOR, $3, $5); $$->c = $7; $$->d = $10; }
                     | If '(' expression ')' opt_newlines statement %prec IF_WITHOUT_ELSE
                         { $$ = node(S_IF, $3, $6); }
                     | If '(' expression ')' opt_newlines statement Else opt_newlines statement
                         { $$ = node(S_IF, $3, $6); $$->c = $9; }
                     | While '(' expression ')' opt_newlines statement
                         { $$ = node(S_WHILE, $3, $6); }
                     | '{' statement_list '}'
                         { $$ = node(S_BLOCK, $2, NULL); }
                     | Print print_list
                         { $$ = node(S_PRINT, $2, NULL); }
                     ;

print_list           : print_item
                         { $$ = $1; }
                     | print_list ',' print_item
                         { $$ = append($1, $3); }
                     ;

print_item           : STRING
                         { $$ = node(S_STRING, NULL, NULL); $$->str = $1; }
                     | expression
                         { $$ = $1; }
                     ;

opt_expression       : /* empty */
                         { $$ = NULL; }
                     | expression
                         { $$ = $1; }
                     ;

function             : Define LETTER '(' opt_define_list ')' opt_newlines
                       '{' opt_newlines opt_auto_define_list opt_statement_list '}'
                         { define_function(lookup($2), $4, $9, $10, cur_line); free($2); }
                     ;

opt_statement_list   : statement_list
                         { $$ = $1; }
                     ;

opt_define_list      : /* empty */
                         { $$ = NULL; }
                     | define_list
                         { $$ = $1; }
                     ;

opt_auto_define_list : /* empty */
                         { $$ = NULL; }
                     | Auto define_list NEWLINE
                         { $$ = $2; }
                     | Auto define_list ';'
                         { $$ = $2; }
                     ;

define_list          : LETTER
                         { $$ = calloc(1, sizeof(Param)); if (!$$) bc_nomem();
                           $$->sym = lookup($1); free($1); }
                     | LETTER '[' ']'
                         { $$ = calloc(1, sizeof(Param)); if (!$$) bc_nomem();
                           $$->sym = lookup($1); $$->array = 1; free($1); }
                     | define_list ',' LETTER
                         { Param *p = calloc(1, sizeof(Param)), *q; if (!p) bc_nomem();
                           p->sym = lookup($3); free($3);
                           for (q = $1; q->next; q = q->next) ;
                           q->next = p; $$ = $1; }
                     | define_list ',' LETTER '[' ']'
                         { Param *p = calloc(1, sizeof(Param)), *q; if (!p) bc_nomem();
                           p->sym = lookup($3); p->array = 1; free($3);
                           for (q = $1; q->next; q = q->next) ;
                           q->next = p; $$ = $1; }
                     ;

opt_argument_list    : /* empty */
                         { $$ = NULL; }
                     | argument_list
                         { $$ = $1; }
                     ;

argument_list        : expression
                         { $$ = $1; }
                     | expression ',' argument_list
                         { $1->next = $3; $$ = $1; }
                     | LETTER '[' ']'
                         { $$ = node(E_ARRAYARG, NULL, NULL); $$->sym = lookup($1); free($1); }
                     | LETTER '[' ']' ',' argument_list
                         { $$ = node(E_ARRAYARG, NULL, NULL); $$->sym = lookup($1); free($1);
                           $$->next = $5; }
                     ;

expression           : named_expression
                         { $$ = $1; }
                     | NUMBER
                         { $$ = node(E_NUM, NULL, NULL); $$->str = $1; }
                     | '(' expression ')'
                         { $$ = $2; $$->paren = 1; }
                     | LETTER '(' opt_argument_list ')'
                         { $$ = node(E_CALL, $3, NULL); $$->sym = lookup($1); free($1); }
                     | '-' expression %prec UMINUS
                         { $$ = node(E_NEG, $2, NULL); }
                     | expression '+' expression
                         { $$ = node(E_ADD, $1, $3); }
                     | expression '-' expression
                         { $$ = node(E_SUB, $1, $3); }
                     | expression MUL_OP expression
                         { $$ = node($2, $1, $3); }
                     | expression '^' expression
                         { $$ = node(E_POW, $1, $3); }
                     | INCR_DECR named_expression
                         { $$ = node($1 > 0 ? E_PREINC : E_PREDEC, mk_named($2), NULL); }
                     | named_expression INCR_DECR
                         { $$ = node($2 > 0 ? E_POSTINC : E_POSTDEC, mk_named($1), NULL); }
                     | named_expression ASSIGN_OP expression
                         { $$ = node(E_ASSIGN, mk_named($1), $3); $$->op = $2; }
                     | Length '(' expression ')'
                         { $$ = node(E_LENGTH, $3, NULL); }
                     | Sqrt '(' expression ')'
                         { $$ = node(E_SQRT, $3, NULL); }
                     | Scale '(' expression ')'
                         { $$ = node(E_SCALEF, $3, NULL); }
                     | expression REL_OP expression
                         { $$ = node(E_REL, $1, $3); $$->op = $2; }
                     | expression AND expression
                         { $$ = node(E_AND, $1, $3); }
                     | expression OR expression
                         { $$ = node(E_OR, $1, $3); }
                     | '!' expression
                         { $$ = node(E_NOT, $2, NULL); }
                     ;

named_expression     : LETTER
                         { $$ = node(E_VAR, NULL, NULL); $$->sym = lookup($1); free($1); }
                     | LETTER '[' expression ']'
                         { $$ = node(E_ELEM, $3, NULL); $$->sym = lookup($1); free($1); }
                     | Scale
                         { $$ = node(E_SCALE, NULL, NULL); }
                     | Ibase
                         { $$ = node(E_IBASE, NULL, NULL); }
                     | Obase
                         { $$ = node(E_OBASE, NULL, NULL); }
                     ;

%%
static Node *mk_named(Node *n)
{
    return n;
}

void yyerror(const char *s)
{
    bc_error("%s", s);
}
