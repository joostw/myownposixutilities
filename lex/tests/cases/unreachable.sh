# code in the rules section: before the first rule it runs on each call of
# yylex; after a rule it is copied where it cannot run (as historically)
cat > g.l <<'L'
%{
#include <stdio.h>
%}
%%
    static int calls;
    printf("[yylex %d]", ++calls);
a   { printf("A"); return 1; }
    /* an indented comment between rules */
    printf("never");
b   { printf("B"); }
%{
    printf("never either");
%}
%%
int main(void) { while (yylex()) ; printf("\n"); return 0; }
L
$LEX g.l && $CC $CQUIET -o g lex.yy.c -L"$LIBDIR" -ll && printf 'aba\n' | ./g
