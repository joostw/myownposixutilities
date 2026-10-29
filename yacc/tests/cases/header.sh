# -d: a separately compiled lexer uses the token numbers and yylval
cat > g.y <<'G'
%{
#include <stdio.h>
void yyerror(const char *);
%}
%union { int n; const char *s; }
%token <n> NUM 300
%token <s> WORD
%token PLUS 400 dotted.name
%type <n> sum
%%
top : sum WORD { printf("%d %s\n", $1, $2); } ;
sum : NUM | sum PLUS NUM { $$ = $1 + $3; } ;
%%
void yyerror(const char *s) { printf("%s\n", s); }
int main(void) { return yyparse(); }
G
cat > lex.c <<'G'
#include "y.tab.h"
int yylex(void)
{
    static int k;
    switch (k++) {
    case 0: yylval.n = 4; return NUM;
    case 1: return PLUS;
    case 2: yylval.n = 38; return NUM;
    case 3: yylval.s = "done"; return WORD;
    }
    return 0;
}
G
$YACC -d g.y || exit 1
grep '#define' y.tab.h
$CC $CPEDANTIC -o g y.tab.c lex.c && ./g
