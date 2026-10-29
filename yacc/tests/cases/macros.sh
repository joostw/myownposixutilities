# YYACCEPT, YYABORT, YYERROR, yyclearin, YYRECOVERING(), yychar in actions, yynerrs
cat > g.y <<'G'
%{
#include <stdio.h>
extern int yynerrs;
%}
%token X
%%
s : items 'q'          { printf("accepted\n"); }
  | items '!'          { printf("abort\n"); YYABORT; }
  | items '.'          { printf("accept early\n"); YYACCEPT; }
  ;
items : /* empty */
  | items item
  | items error ';'    { printf("recovered, recovering=%d\n", YYRECOVERING()); yyerrok;
                         printf("after yyerrok %d\n", YYRECOVERING()); }
  ;
item : 'a'             { printf("a, yychar=%d\n", yychar); }
     | 'e' 'e'         { printf("YYERROR\n"); YYERROR; }
     | 'c' 'c'         { printf("clear %d\n", yychar); yyclearin; }
     ;
%%
static const char *in;
int yylex(void) { return *in ? *in++ : 0; }
void yyerror(const char *s) { printf("yyerror: %s\n", s); }
int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        int r;
        in = argv[i];
        r = yyparse();
        printf("[%s] -> %d, yynerrs %d\n", argv[i], r, yynerrs);
    }
    return 0;
}
G
$YACC g.y && $CC $CWARN -o g y.tab.c || exit 1
./g 'aaq' 'aa!' 'a.a' 'axx;aq' 'ee;aq' 'aee' 'ccaq' 'ccxq' 'zz;' 'b' '' 'q' ';;;;q' 'x;y;q'
