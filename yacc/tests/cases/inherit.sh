# $0 and $-1: values to the left of the rule on the stack
cat > g.y <<'G'
%{
#include <stdio.h>
%}
%union { int n; char c; }
%token <c> T
%type <n> decl list
%%
decls : /* empty */ | decls decl ;
decl  : T list ';' { $$ = 0; } ;
list  : 'x'          { printf("%c x (left of type: %c)\n", $<c>0, $<c>0); $$ = 1; }
      | list ',' 'x' { printf("%c x\n", $<c>0); $$ = $1 + 1; }
      ;
%%
static const char *in = "Ix,x;Fx;";
int yylex(void)
{
    int c = *in ? *in++ : 0;
    if (c == 'I' || c == 'F') { yylval.c = (char)c; return T; }
    return c;
}
void yyerror(const char *s) { printf("%s\n", s); }
int main(void) { return yyparse(); }
G
$YACC g.y && $CC $CWARN -o g y.tab.c && ./g
