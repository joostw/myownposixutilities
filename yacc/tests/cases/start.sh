# %start chooses a symbol other than the first left-hand side; ';' after
# a rule may be followed by '|'; multi-character literals (an extension)
cat > g.y <<'G'
%{
#include <stdio.h>
%}
%start top
%%
other : 'x' ;
top   : other 'y' { printf("top\n"); } ;
      | "zz" other { printf("zz\n"); }
      ;
%%
static const char *in;
int yylex(void) { int c = *in ? *in++ : 0; return c == 'z' ? 257 : c; }
void yyerror(const char *s) { printf("%s\n", s); }
int main(void) { in = "xy"; yyparse(); in = "zx"; yyparse(); in = "x"; return yyparse(); }
G
$YACC g.y && $CC $CWARN -o g y.tab.c && ./g
