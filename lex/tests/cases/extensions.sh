# accepted where flex is stricter or the standard leaves room: empty
# alternatives and {0}, a lone {, %option noyywrap, table sizes, comments
# in the definitions section, yylineno without an option, <*>
cat > g.l <<'L'
/* a comment
   on two lines */
%p 3000
%n 600
%a 2500
%e 1200
%k 1100
%o 4000
%option noyywrap
%s S
%{
#include <stdio.h>
%}
%%
x(a|)y      printf("<%s>", yytext);
(|b)c       printf("[%s]", yytext);
h{0}i       printf("(h0i:%s)", yytext);
{           printf("<brace>");
<*>q        { printf("<q%d>", YY_START); BEGIN S; }
\n          printf("%d\n", yylineno);
%%
int main(void) { while (yylex()) ; return 0; }
L
$LEX g.l && $CC $CWARN -o g lex.yy.c && printf 'xyxaybcc hi i{q q\nx\n' | ./g
