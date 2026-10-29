# the lex library: main() calls yylex until it returns 0; yywrap returns 1;
# an application's own yywrap replaces it
cat > g.l <<'L'
%%
x   return 1;
L
$LEX g.l && $CC -o g lex.yy.c -L"$LIBDIR" -ll && echo axbxc | ./g; echo "status $?"
cat > h.l <<'L'
%{
#include <stdio.h>
static int n;
%}
%%
%%
int yywrap(void) { printf("[wrap %d]", n); return n++ > 0; }
L
$LEX h.l && $CC -o h lex.yy.c -L"$LIBDIR" -ll && echo hi | ./h; echo
