/* yyerror() of the yacc library (-l y) */
#include <stdio.h>

void yyerror(const char *msg)
{
    (void)fprintf(stderr, "%s\n", msg);
}
