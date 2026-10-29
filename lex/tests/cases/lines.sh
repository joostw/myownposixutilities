# #line directives point into the source; yylineno counts lines
cat > g.l <<'L'
%{
int x;
%}
%%
a   { x++; }
L
$LEX g.l && grep '#line' lex.yy.c | sed 's/^#line [0-9]* "lex.yy.c"/#line N "lex.yy.c"/' | sort | uniq -c
awk '/^#line [0-9]+ "lex.yy.c"/ { if ($2 != NR + 1) print "wrong", NR, $2 }' lex.yy.c
$CC $CWARN -c lex.yy.c && echo compiled
cat > n.l <<'L'
%{
#include <stdio.h>
%}
%%
[a-z]+  printf("%d:%s ", yylineno, yytext);
\n\n    ;
"!"     { unput('\n'); unput('\n'); }
.|\n    ;
L
$LEX n.l && $CC -o n lex.yy.c -L"$LIBDIR" -ll && printf 'a\nb\n\nc!d\ne' | ./n; echo
