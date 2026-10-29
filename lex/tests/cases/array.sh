# %array: yytext is a char array of YYLMAX; a longer token is an error;
# YYLMAX can be raised
cat > g.l <<'L'
%array
%{
#include <stdio.h>
%}
%%
a+      printf("<%d %d>", yyleng, (int)sizeof yytext);
\n      ;
L
$LEX g.l && $CC -o g lex.yy.c -L"$LIBDIR" -ll && awk 'BEGIN { while (i++ < 100) printf "a"; print "" }' | ./g; echo " status $?"
awk 'BEGIN { while (i++ < 9000) printf "a"; print "" }' | ./g; echo " status $?"
$CC -DYYLMAX=20000 -o g2 lex.yy.c -L"$LIBDIR" -ll && awk 'BEGIN { while (i++ < 9000) printf "a"; print "" }' | ./g2; echo " status $?"
grep -c 'char yytext\[YYLMAX\]' lex.yy.c
