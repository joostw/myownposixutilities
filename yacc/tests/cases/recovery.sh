# error recovery step by step: no yyerror until three tokens are shifted,
# a token is discarded when nothing was shifted since the error, the
# lookahead that caused the error is kept, and parsing can end normally
# after recovery
cat > g.y <<'G'
%{
#include <stdio.h>
%}
%%
s    : list           { printf("list done\n"); } ;
list : /* empty */
     | list item ';'  { printf("item\n"); }
     | list error ';' { printf("error item\n"); }
     ;
item : 'x' | 'x' 'x' ;
t    : e '-' e        { printf("t\n"); } ;
e    : /* empty */ | error { printf("e: error\n"); } ;
%%
static const char *in;
int yylex(void)
{
    int c = *in ? *in++ : 0;
    return c;
}
void yyerror(const char *s) { printf("yyerror: %s\n", s); }
int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        in = argv[i];
        printf("[%s]\n", argv[i]);
        printf("-> %d\n", yyparse());
    }
    return 0;
}
G
$YACC g.y 2>&1 && $CC $CWARN -o g y.tab.c || exit 1
./g 'x;' 'q;x;' 'qq;x;' 'q;q;' 'x;q;x;' 'xxx;x;' 'x;q' 'q;qx;x;'
sed 's/^%%$/%start t\n%%/;' g.y | awk 'NR==1 || !/^%start t$/ || !done++' > h.y
$YACC h.y 2>/dev/null && $CC $CQUIET -o h y.tab.c || exit 1
./h '-' '-b' 'b-' 'bb-b' '-bb'
