# -p and -b: two parsers in one program, each with its own lexer and header
cat > one.y <<'G'
%{
#include <stdio.h>
%}
%token A
%%
s : A A { printf("one: A A (yychar %d)\n", onechar); } ;
%%
static int n;
int onelex(void) { return n++ < 2 ? A : 0; }
void oneerror(const char *s) { printf("one: %s\n", s); }
G
cat > two.y <<'G'
%{
#include <stdio.h>
%}
%token B C
%%
s : B C { printf("two: B C, yylval %d\n", yylval); } ;
%%
static int n;
int twolex(void) { n++; twolval = n * 10; return n == 1 ? B : n == 2 ? C : 0; }
void twoerror(const char *s) { printf("two: %s\n", s); }
G
cat > main.c <<'G'
#include <stdio.h>
#include "one.tab.h"
int oneparse(void);
int twoparse(void);
int main(void) { int a = oneparse(); int b = twoparse(); printf("%d %d\n", a, b); return 0; }
G
$YACC -d -p one -b one one.y && $YACC -d -p two -b two two.y || exit 1
ls *.tab.* | sort
$CC $CWARN -o p main.c one.tab.c two.tab.c || exit 1
./p
grep 'oneparse\|onelval' one.tab.h
