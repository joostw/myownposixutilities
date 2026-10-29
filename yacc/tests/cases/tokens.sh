# token numbers: assigned, automatic (above 256), literals with escapes,
# a name for the end marker, error renumbered, YYEOF and YYEMPTY
cat > g.y <<'G'
%{
#include <stdio.h>
%}
%token A
%token B 500 C
%token END 0
%token error 1000
%token '\n' '\'' '\\' '\x41' '\102' '"'
%%
s : A B C '\n' '\'' '\\' 'A' 'B' '"' END { printf("ok yychar=%d\n", yychar); } ;
%%
static int toks[] = { A, B, C, 10, 39, 92, 65, 66, 34, -5 };
static int k;
int yylex(void) { return toks[k++]; }
void yyerror(const char *s) { printf("%s\n", s); }
int main(void)
{
    printf("A=%d B=%d C=%d END=%d YYEOF=%d YYEMPTY=%d YYERRCODE=%d\n", A, B, C, END, YYEOF, YYEMPTY, YYERRCODE);
    return yyparse();
}
G
$YACC -d g.y || exit 1
cat y.tab.h | grep define
$CC $CWARN -o g y.tab.c && ./g
# large and sparse token numbers
cat > h.y <<'G'
%token BIG 100000 SMALL 7
%%
s : BIG SMALL BIG ;
%%
static int toks[] = { BIG, SMALL, BIG, 0 };
static int k;
int yylex(void) { return toks[k++]; }
void yyerror(const char *s) { (void)s; }
int main(void) { return yyparse(); }
G
$YACC h.y && $CC $CWARN -o h y.tab.c && ./h && echo big ok
