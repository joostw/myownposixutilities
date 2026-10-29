/* main() of the yacc library (-l y) */
#include <locale.h>
#include <stdlib.h>

int yyparse(void);

int main(void)
{
    setlocale(LC_ALL, "");
    exit(yyparse() ? EXIT_FAILURE : EXIT_SUCCESS);
}
