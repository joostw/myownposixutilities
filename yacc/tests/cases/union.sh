# %union, %type, tags, $<tag>$ in mid-rule actions, $<tag>n
cat > u.y <<'G'
%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
%}
%union {
    double d;
    char *s;
    int i;
}
%token <d> NUM
%token <s> NAME
%type <d> expr term
%type <s> list
%left '+'
%%
top  : stmts ;
stmts: /* empty */ | stmts stmt ;
stmt : NAME '=' { $<i>$ = (int)strlen($1); } expr ';'
           { printf("%s(%d) = %g\n", $1, $<i>3, $4); free($1); }
     | '[' list ']' ';' { printf("list: %s\n", $2); free($2); }
     ;
expr : expr '+' term { $$ = $1 + $3; }
     | term
     ;
term : NUM
     | '(' expr ')' { $$ = $<d>2 * 1; }
     ;
list : NAME
     | list ',' NAME { $$ = malloc(strlen($1) + strlen($3) + 2);
                       sprintf($$, "%s|%s", $1, $3); free($1); free($3); }
     ;
%%
int yylex(void)
{
    int c;
    while ((c = getchar()) == ' ' || c == '\n')
        ;
    if (isdigit(c)) {
        char buf[64];
        int n = 0;
        while ((isdigit(c) || c == '.') && n < 63) {
            buf[n++] = (char)c;
            c = getchar();
        }
        buf[n] = 0;
        ungetc(c, stdin);
        yylval.d = atof(buf);
        return NUM;
    }
    if (isalpha(c)) {
        char buf[64];
        int n = 0;
        while (isalnum(c) && n < 63) {
            buf[n++] = (char)c;
            c = getchar();
        }
        buf[n] = 0;
        ungetc(c, stdin);
        yylval.s = strdup(buf);
        return NAME;
    }
    return c == EOF ? 0 : c;
}
void yyerror(const char *s) { printf("error: %s\n", s); }
int main(void) { return yyparse(); }
G
$YACC -d u.y && $CC $CWARN -D_POSIX_C_SOURCE=200809L -o u y.tab.c || exit 1
echo 'abc = 1.5 + 2 + (3 + 0.25); [x, yy, zzz]; q = 1;' | ./u
grep -c 'typedef union YYSTYPE' y.tab.h
