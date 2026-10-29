# the yacc library: main() and yyerror()
cat > g.y <<'G'
%%
s : 'a' 'b' ;
%%
#include <stdio.h>
int yylex(void) { int c = getchar(); return c == '\n' || c == EOF ? 0 : c; }
G
$YACC g.y && $CC -o g y.tab.c -L"$LIBDIR" -ly || exit 1
echo ab | ./g; echo "status $?"
echo ba | ./g; echo "status $?"
