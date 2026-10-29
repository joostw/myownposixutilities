# the generated scanner is ISO C: strict compilation with C99, C11 and C17,
# with %pointer and %array, trailing context, REJECT and start conditions
cat > s.l <<'L'
%{
#include <stdio.h>
%}
%s A
%x B
%%
<A>x+/y     printf("<x:%s>", yytext);
a+/b+       printf("<a:%s>", yytext);
c           { printf("c"); REJECT; }
[c-d]       { printf("[cd]"); yymore(); }
e           { yyless(0); BEGIN B; }
<B>e        { BEGIN A; printf("E"); }
<B>.|\n     ECHO;
f           { int c = input(); if (c) unput(c); printf("f"); }
L
for arr in '' '%array'; do
    { printf '%s\n' "$arr"; cat s.l; } > t.l
    $LEX t.l || exit 1
    if [ -n "$CSTDS" ]; then
        for std in $CSTDS; do
            $CC -std=$std -pedantic -Wall -Wextra -Werror -c lex.yy.c || echo "failed: $std $arr"
        done
    else
        $CC -c lex.yy.c || echo "failed: $arr"     # a compiler without those options
    fi
done
echo compiled
