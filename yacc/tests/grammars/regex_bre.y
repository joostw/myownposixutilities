
%token    ORD_CHAR QUOTED_CHAR DUP_COUNT

%token    BACKREF L_ANCHOR R_ANCHOR

%token    Back_open_paren  Back_close_paren
/*          '\('             '\)'        */

%token    Back_open_brace  Back_close_brace
/*          '\{'             '\}'        */

/* The following shall be tokens on implementations where
   \?, \+, and \| are not included in QUOTED_CHAR */

%token    Back_qm  Back_plus  Back_bar
/*          '\?'     '\+'       '\|'     */

/* The following tokens are for the Bracket Expression
   grammar common to both REs and EREs. */

%token    COLL_ELEM_SINGLE COLL_ELEM_MULTI META_CHAR

%token    Open_equal Equal_close Open_dot Dot_close Open_colon Colon_close
/*           '[='       '=]'        '[.'     '.]'      '[:'       ':]'  */

%token    class_name
/* class_name is a keyword to the LC_CTYPE locale category */
/* (representing a character class) in the current locale */
/* and is only recognized between [: and :] */

%start    basic_reg_exp
%%

/* --------------------------------------------
   Basic Regular Expression
   --------------------------------------------
*/
basic_reg_exp   :                        BRE_branch
                | basic_reg_exp Back_bar BRE_branch /* if Back_bar
                                                       is a token */
                ;
BRE_branch      :            BRE_expression
                | BRE_branch BRE_expression 
                ;
BRE_expression  :          simple_BRE
                | L_ANCHOR
                |                     R_ANCHOR
                | L_ANCHOR            R_ANCHOR
                | L_ANCHOR simple_BRE
                |          simple_BRE R_ANCHOR
                | L_ANCHOR simple_BRE R_ANCHOR
                ;
simple_BRE      : nondupl_BRE
                | nondupl_BRE BRE_dupl_symbol
                ;
nondupl_BRE     : one_char_or_coll_elem_BRE
                | Back_open_paren basic_reg_exp Back_close_paren
                | BACKREF
                ;
one_char_or_coll_elem_BRE  : ORD_CHAR
                | QUOTED_CHAR
                | '.'
                | bracket_expression
                ;
BRE_dupl_symbol : '*'
                | Back_qm   /* if Back_qm is a token */
                | Back_plus /* if Back_plus is a token */
                | Back_open_brace DUP_COUNT               Back_close_brace
                | Back_open_brace DUP_COUNT ','           Back_close_brace
                | Back_open_brace DUP_COUNT ',' DUP_COUNT Back_close_brace
                ;

/* --------------------------------------------
   Bracket Expression
   -------------------------------------------
*/
bracket_expression : '[' matching_list ']'
               | '[' nonmatching_list ']'
               ;
matching_list  : bracket_list
               ;
nonmatching_list : '^' bracket_list
               ;
bracket_list   : follow_list
               | follow_list '-'
               ;
follow_list    :             expression_term
               | follow_list expression_term
               ;
expression_term : single_expression
               | range_expression
               ;
single_expression : end_range
               | character_class
               | equivalence_class
               ;
range_expression : start_range end_range
               | start_range '-'
               ;
start_range    : end_range '-'
               ;
end_range      : COLL_ELEM_SINGLE
               | collating_symbol
               ;
collating_symbol : Open_dot COLL_ELEM_SINGLE Dot_close
               | Open_dot COLL_ELEM_MULTI Dot_close
               | Open_dot META_CHAR Dot_close
               ;
equivalence_class : Open_equal COLL_ELEM_SINGLE Equal_close
               | Open_equal COLL_ELEM_MULTI Equal_close
               ;
character_class : Open_colon class_name Colon_close
               ;
