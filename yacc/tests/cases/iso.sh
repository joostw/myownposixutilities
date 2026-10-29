# the code file is ISO C: compiles cleanly with strict options, with and
# without debugging code, as C and with the spec grammars
cat > g.y <<'G'
%{
#include <stdio.h>
%}
%union { int i; double d; }
%token <i> I
%token <d> D
%type <d> e
%left '+'
%%
s : e { printf("%g\n", $1); } ;
e : e '+' e { $$ = $1 + $3; } | I { $$ = $1; } | D ;
%%
static int k;
int yylex(void) { switch (k++) { case 0: yylval.i = 2; return I; case 1: return '+'; case 2: yylval.d = .5; return D; } return 0; }
void yyerror(const char *s) { fprintf(stderr, "%s\n", s); }
int main(void) { return yyparse(); }
G
for f in "" -t; do
    $YACC $f g.y || exit 1
    if [ -n "$CSTDS" ]; then
        for std in $CSTDS; do
            $CC -std=$std -pedantic -Wall -Wextra -Werror -o g y.tab.c || echo "failed with $std $f"
        done
    else
        $CC -o g y.tab.c || echo "failed with $f"     # a compiler without those options
    fi
    ./g
done
