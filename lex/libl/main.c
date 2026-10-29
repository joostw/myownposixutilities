/* main() of the lex library (-l l) */
#include <stdlib.h>

int yylex(void);

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    while (yylex() != 0)
        ;
    return EXIT_SUCCESS;
}
