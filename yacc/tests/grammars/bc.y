
%token    EOF NEWLINE STRING LETTER NUMBER

%token    MUL_OP
/*        '*', '/', '%'                           */

%token    ASSIGN_OP
/*        '=', '+=', '-=', '*=', '/=', '%=', '^=' */

%token    REL_OP
/*        '==', '<=', '>=', '!=', '<', '>'        */

%token    INCR_DECR
/*        '++', '--'                              */

%token    Define    Break    Quit    Length
/*        'define', 'break', 'quit', 'length'     */

%token    Return    For    If    While    Sqrt
/*        'return', 'for', 'if', 'while', 'sqrt'  */

%token    Scale    Ibase    Obase    Auto
/*        'scale', 'ibase', 'obase', 'auto'       */

%start    program

%%

program              : EOF
                     | input_item program
                     ;

input_item           : semicolon_list NEWLINE
                     | function
                     ;

semicolon_list       : /* empty */
                     | statement
                     | semicolon_list ';' statement
                     | semicolon_list ';'
                     ;

statement_list       : /* empty */
                     | statement
                     | statement_list NEWLINE
                     | statement_list NEWLINE statement
                     | statement_list ';'
                     | statement_list ';' statement
                     ;

statement            : expression
                     | STRING
                     | Break
                     | Quit
                     | Return
                     | Return '(' return_expression ')'
                     | For '(' expression ';'
                           relational_expression ';'
                           expression ')' statement
                     | If '(' relational_expression ')' statement
                     | While '(' relational_expression ')' statement
                     | '{' statement_list '}'
                     ;

function             : Define LETTER '(' opt_define_list ')'
                           '{' NEWLINE opt_auto_define_list
                           statement_list '}'
                     ;

opt_define_list      : /* empty */
                     | define_list
                     ;

opt_auto_define_list : /* empty */
                     | Auto define_list NEWLINE
                     | Auto define_list ';'
                     ;

define_list          : LETTER
                     | LETTER '[' ']'
                     | define_list ',' LETTER
                     | define_list ',' LETTER '[' ']'
                     ;

opt_argument_list    : /* empty */
                     | argument_list
                     ;

argument_list        : expression
                     | expression ',' argument_list
                     | LETTER '[' ']'
                     | LETTER '[' ']' ',' argument_list
                     ;

relational_expression : expression
                     | expression REL_OP expression
                     ;

return_expression    : /* empty */
                     | expression
                     ;

expression           : named_expression
                     | NUMBER
                     | '(' expression ')'
                     | LETTER '(' opt_argument_list ')'
                     | '-' expression
                     | expression '+' expression
                     | expression '-' expression
                     | expression MUL_OP expression
                     | expression '^' expression
                     | INCR_DECR named_expression
                     | named_expression INCR_DECR
                     | named_expression ASSIGN_OP expression
                     | Length '(' expression ')'
                     | Sqrt '(' expression ')'
                     | Scale '(' expression ')'
                     ;

named_expression     : LETTER
                     | LETTER '[' expression ']'
                     | Scale
                     | Ibase
                     | Obase
                     ;
