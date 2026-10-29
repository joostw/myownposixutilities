# an application can define ECHO, YYLMAX and YY_DECL in its definitions
cat > g.l <<'L'
%array
%{
#include <stdio.h>
#define ECHO printf("[%s]", yytext)
#define YYLMAX 16
#define YY_DECL int scan(void)
%}
%%
[a-z]+  ECHO;
%%
int main(void) { while (scan()) ; printf(" %d\n", (int)sizeof yytext); return 0; }
L
$LEX g.l && $CC $CWARN -o g lex.yy.c -L"$LIBDIR" -ll && echo 'ab cd' | ./g
