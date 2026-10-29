
%token CHARCLASS
%token              LOC_NAME
%token              CHAR
%token              NUMBER
%token              COLLSYMBOL COLLELEMENT
%token              CHARSYMBOL OCTAL_CHAR HEX_CHAR DECIMAL_CHAR
%token              ELLIPSIS
%token              EXTENDED_REG_EXP
%token              EOL

%start              locale_definition

%%

locale_definition   : global_statements locale_categories
                    |                   locale_categories
                    ;

global_statements   : global_statements symbol_redefine
                    | symbol_redefine
                    ;

symbol_redefine     : 'escape_char'  CHAR EOL
                    | 'comment_char' CHAR EOL
                    ;

locale_categories   : locale_categories locale_category
                    | locale_category
                    ;

locale_category     : lc_ctype | lc_collate | lc_messages
                    | lc_monetary | lc_numeric | lc_time
                    ;

/* The following grammar rules are common to all categories */

char_list           : char_list char_symbol
                    | char_symbol
                    ;

char_symbol         : CHAR | CHARSYMBOL
                    | OCTAL_CHAR | HEX_CHAR | DECIMAL_CHAR
                    ;

elem_list           : elem_list char_symbol
                    | elem_list COLLSYMBOL
                    | elem_list COLLELEMENT
                    | char_symbol
                    | COLLSYMBOL
                    | COLLELEMENT
                    ;

symb_list           : symb_list COLLSYMBOL
                    | COLLSYMBOL
                    ;

locale_name         : LOC_NAME
                    | '"' LOC_NAME '"'
                    ;

/* The following is the LC_CTYPE category grammar */

lc_ctype            : ctype_hdr ctype_keywords         ctype_tlr
                    | ctype_hdr 'copy' locale_name EOL ctype_tlr
                    ;

ctype_hdr           : 'LC_CTYPE' EOL
                    ;

ctype_keywords      : ctype_keywords ctype_keyword
                    | ctype_keyword
                    ;

ctype_keyword       : charclass_keyword charclass_list EOL
                    | charconv_keyword charconv_list EOL
                    | 'charclass' charclass_namelist EOL
                    ;

charclass_namelist  : charclass_namelist ';' CHARCLASS
                    | CHARCLASS
                    ;

charclass_keyword   : 'upper' | 'lower' | 'alpha' | 'digit'
                    | 'punct' | 'xdigit' | 'space' | 'print'
                    | 'graph' | 'blank' | 'cntrl' | 'alnum'
                    | CHARCLASS
                    ;

charclass_list      : charclass_list ';' char_symbol
                    | charclass_list ';' ELLIPSIS ';' char_symbol
                    | char_symbol
                    ;

charconv_keyword    : 'toupper'
                    | 'tolower'
                    ;

charconv_list       : charconv_list ';' charconv_entry
                    | charconv_entry
                    ;

charconv_entry      : '(' char_symbol ',' char_symbol ')'
                    ;

ctype_tlr           : 'END' 'LC_CTYPE' EOL
                    ;

/* The following is the LC_COLLATE category grammar */

lc_collate          : collate_hdr collate_keywords       collate_tlr
                    | collate_hdr 'copy' locale_name EOL collate_tlr
                    ;

collate_hdr         : 'LC_COLLATE' EOL
                    ;

collate_keywords    :                order_statements
                    | opt_statements order_statements
                    ;

opt_statements      : opt_statements collating_symbols
                    | opt_statements collating_elements
                    | collating_symbols
                    | collating_elements
                    ;

collating_symbols   : 'collating-symbol' COLLSYMBOL EOL
                    ;

collating_elements  : 'collating-element' COLLELEMENT
                    | 'from' '"' elem_list '"' EOL
                    ;

order_statements    : order_start collation_order order_end
                    ;

order_start         : 'order_start' EOL
                    | 'order_start' order_opts EOL
                    ;

order_opts          : order_opts ';' order_opt
                    | order_opt
                    ;

order_opt           : order_opt ',' opt_word
                    | opt_word
                    ;

opt_word            : 'forward' | 'backward' | 'position'
                    ;

collation_order     : collation_order collation_entry
                    | collation_entry
                    ;

collation_entry     : COLLSYMBOL EOL
                    | collation_element weight_list EOL
                    | collation_element             EOL
                    ;

collation_element   : char_symbol
                    | COLLELEMENT
                    | ELLIPSIS
                    | 'UNDEFINED'
                    ;

weight_list         : weight_list ';' weight_symbol
                    | weight_list ';'
                    | weight_symbol
                    ;

weight_symbol       : /* empty */
                    | char_symbol
                    | COLLSYMBOL
                    | '"' elem_list '"'
                    | '"' symb_list '"'
                    | ELLIPSIS
                    | 'IGNORE'
                    ;

order_end           : 'order_end' EOL
                    ;

collate_tlr         : 'END' 'LC_COLLATE' EOL
                    ;

/* The following is the LC_MESSAGES category grammar */

lc_messages         : messages_hdr messages_keywords      messages_tlr
                    | messages_hdr 'copy' locale_name EOL messages_tlr
                    ;

messages_hdr        : 'LC_MESSAGES' EOL
                    ;

messages_keywords   : messages_keywords messages_keyword
                    | messages_keyword
                    ;

messages_keyword    : 'yesexpr' '"' EXTENDED_REG_EXP '"' EOL
                    | 'noexpr'  '"' EXTENDED_REG_EXP '"' EOL
                    ;

messages_tlr        : 'END' 'LC_MESSAGES' EOL
                    ;

/* The following is the LC_MONETARY category grammar */

lc_monetary         : monetary_hdr monetary_keywords       monetary_tlr
                    | monetary_hdr 'copy' locale_name EOL  monetary_tlr
                    ;

monetary_hdr        : 'LC_MONETARY' EOL
                    ;

monetary_keywords   : monetary_keywords monetary_keyword
                    | monetary_keyword
                    ;

monetary_keyword    : mon_keyword_string mon_string EOL
                    | mon_keyword_char NUMBER EOL
                    | mon_keyword_char '-1'   EOL
                    | mon_keyword_grouping mon_group_list EOL
                    ;

mon_keyword_string  : 'int_curr_symbol' | 'currency_symbol'
                    | 'mon_decimal_point' | 'mon_thousands_sep'
                    | 'positive_sign' | 'negative_sign'
                    ;

mon_string          : '"' char_list '"'
                    | '""'
                    ;

mon_keyword_char    : 'int_frac_digits' | 'frac_digits'
                    | 'p_cs_precedes' | 'p_sep_by_space'
                    | 'n_cs_precedes' | 'n_sep_by_space'
                    | 'p_sign_posn' | 'n_sign_posn'
                    | 'int_p_cs_precedes' | 'int_p_sep_by_space'
                    | 'int_n_cs_precedes' | 'int_n_sep_by_space'
                    | 'int_p_sign_posn' | 'int_n_sign_posn'
                    ;

mon_keyword_grouping : 'mon_grouping'
                    ;

mon_group_list      : NUMBER
                    | mon_group_list ';' NUMBER
                    ;

monetary_tlr        : 'END' 'LC_MONETARY' EOL
                    ;

/* The following is the LC_NUMERIC category grammar */

lc_numeric          : numeric_hdr numeric_keywords       numeric_tlr
                    | numeric_hdr 'copy' locale_name EOL numeric_tlr
                    ;

numeric_hdr         : 'LC_NUMERIC' EOL
                    ;

numeric_keywords    : numeric_keywords numeric_keyword
                    | numeric_keyword
                    ;

numeric_keyword     : num_keyword_string num_string EOL
                    | num_keyword_grouping num_group_list EOL
                    ;

num_keyword_string  : 'decimal_point'
                    | 'thousands_sep'
                    ;

num_string          : '"' char_list '"'
                    | '""'
                    ;

num_keyword_grouping: 'grouping'
                    ;

num_group_list      : NUMBER
                    | num_group_list ';' NUMBER
                    ;

numeric_tlr         : 'END' 'LC_NUMERIC' EOL
                    ;

/* The following is the LC_TIME category grammar */

lc_time             : time_hdr time_keywords          time_tlr
                    | time_hdr 'copy' locale_name EOL time_tlr
                    ;

time_hdr            : 'LC_TIME' EOL
                    ;

time_keywords       : time_keywords time_keyword
                    | time_keyword
                    ;

time_keyword        : time_keyword_name time_list EOL
                    | time_keyword_fmt time_string EOL
                    | time_keyword_opt time_list EOL
                    ;

time_keyword_name   : 'abday' | 'day' | 'abmon' | 'mon'
                    ;

time_keyword_fmt    : 'd_t_fmt' | 'd_fmt' | 't_fmt'
                    | 'am_pm' | 't_fmt_ampm'
                    ;

time_keyword_opt    : 'era' | 'era_d_fmt' | 'era_t_fmt'
                    | 'era_d_t_fmt' | 'alt_digits'
                    | 'ab_alt_mon' | 'alt_mon'
                    ;

time_list           : time_list ';' time_string
                    | time_string
                    ;

time_string         : '"' char_list '"'
                    ;

time_tlr            : 'END' 'LC_TIME' EOL
                    ;
