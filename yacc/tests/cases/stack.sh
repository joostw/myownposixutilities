# the stacks grow up to YYMAXDEPTH, then the parse fails
cat > g.y <<'G'
%{
#include <stdio.h>
#include <stdlib.h>
%}
%%
s : 'a' s { $$ = $2 + 1; } | 'b' { $$ = 0; } ;
top_unused : ;
%%
static long n, k;
int yylex(void) { k++; return k <= n ? 'a' : k == n + 1 ? 'b' : 0; }
void yyerror(const char *s) { printf("%s\n", s); }
int main(int argc, char **argv) { (void)argc; n = atol(argv[1]); printf("%d\n", yyparse()); return 0; }
G
$YACC g.y 2>&1 && $CC $CWARN -o g y.tab.c || exit 1
./g 100; ./g 9990; ./g 20000
$CC $CWARN -DYYMAXDEPTH=100000 -o g2 y.tab.c && k=0 ./g2 50000
