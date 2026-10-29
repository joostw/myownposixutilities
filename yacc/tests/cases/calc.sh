# precedence, %prec, error recovery with yyerrok, default $$ = $1
cat > calc.y <<'G'
%{
#include <stdio.h>
#include <ctype.h>
%}
%token NUM
%left '+' '-'
%left '*' '/' '%'
%right '^'
%right UMINUS
%%
lines : /* empty */
      | lines expr '\n'   { printf("%d\n", $2); }
      | lines '\n'
      | lines error '\n'  { printf("(recovered)\n"); yyerrok; }
      ;
expr  : expr '+' expr { $$ = $1 + $3; }
      | expr '-' expr { $$ = $1 - $3; }
      | expr '*' expr { $$ = $1 * $3; }
      | expr '/' expr { $$ = $3 ? $1 / $3 : 0; }
      | expr '%' expr { $$ = $3 ? $1 % $3 : 0; }
      | expr '^' expr { int i, r = 1; for (i = 0; i < $3; i++) r *= $1; $$ = r; }
      | '-' expr %prec UMINUS { $$ = -$2; }
      | '(' expr ')' { $$ = $2; }
      | NUM
      ;
%%
int yylex(void)
{
    int c;
    while ((c = getchar()) == ' ' || c == '\t')
        ;
    if (isdigit(c)) {
        yylval = 0;
        while (isdigit(c)) {
            yylval = yylval * 10 + c - '0';
            c = getchar();
        }
        ungetc(c, stdin);
        return NUM;
    }
    return c == EOF ? 0 : c;
}
void yyerror(const char *s) { printf("error: %s\n", s); }
int main(void) { int r = yyparse(); printf("yyparse: %d\n", r); return 0; }
G
$YACC calc.y && $CC $CPEDANTIC -o calc y.tab.c || exit 1
printf '1+2*3\n2^3^2\n-2^2\n10-4-3\n(1+2)*-3\n7 %% 4\n\n1 2\n3+\n)(\n4*5\n' | ./calc
printf '1+\n' | ./calc
printf '1+1' | ./calc
