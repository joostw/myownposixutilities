# no lookahead is read in a state whose only actions besides errors are
# reductions by one rule: the actions run before yylex is called again
# (except where %nonassoc put an error action in such a state: then the
# lookahead is read, so that n<n<n is an error)
cat > g.y <<'G'
%{
#include <stdio.h>
%}
%nonassoc '<'
%%
s : e ';'       { printf("statement\n"); }
  ;
e : e '<' e     { printf("compare (yychar %d)\n", yychar); }
  | 'n'         { printf("n (yychar %d)\n", yychar); }
  ;
%%
static const char *in;
int yylex(void) { int c = *in ? *in++ : 0; printf("yylex -> %c\n", c ? c : '0'); return c; }
void yyerror(const char *s) { printf("%s\n", s); }
int main(int argc, char **argv) { (void)argc; in = argv[1]; printf("-> %d\n", yyparse()); return 0; }
G
$YACC g.y && $CC $CWARN -o g y.tab.c || exit 1
./g 'n<n;'
./g 'n<n<n;'
