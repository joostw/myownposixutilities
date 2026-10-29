# -t compiles the debugging code; yydebug turns it on; YYDEBUG=0 overrides -t
cat > g.y <<'G'
%{
#include <stdio.h>
%}
%%
s : 'a' s | 'b' ;
%%
static const char *in = "aab";
int yylex(void) { return *in ? *in++ : 0; }
void yyerror(const char *s) { printf("%s\n", s); }
int main(int argc, char **argv) { (void)argv; yydebug = argc > 1; return yyparse(); }
G
$YACC -t g.y && $CC $CWARN -o g y.tab.c || exit 1
./g x 2>&1 | grep -c 'yydebug:'
./g x 2>&1 | grep 'reducing by rule' | head -n 2
./g 2>&1 | wc -l
$CC $CWARN -DYYDEBUG=0 -o g0 y.tab.c && ./g0 x 2>&1 | wc -l
$YACC g.y && $CC $CWARN -o g1 y.tab.c && ./g1 x 2>&1 | wc -l
$CC $CWARN -DYYDEBUG=1 -o g2 y.tab.c && ./g2 x 2>&1 | grep -c 'yydebug:'
