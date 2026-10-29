# yylex returning a negative value is the end of input; yychar is then YYEOF
cat > g.y <<'G'
%{
#include <stdio.h>
%}
%%
s : 'a' { printf("reduced, yychar=%d\n", yychar); } ;
%%
static int k;
int yylex(void) { return k++ ? -7 : 'a'; }
void yyerror(const char *s) { printf("%s\n", s); }
int main(void) { int r = yyparse(); printf("%d yychar=%d\n", r, yychar); return 0; }
G
$YACC g.y && $CC $CWARN -o g y.tab.c && ./g
