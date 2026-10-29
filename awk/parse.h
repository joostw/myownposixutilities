/*
 * parse.h - the interface between the scanner (awk.l), the grammar
 * (awk.y) and the symbol tables (parse.c).
 */
#ifndef PARSE_H
#define PARSE_H

/* a token: its TK_ code, where it starts in the program text (for the
   context in messages) and its value */
typedef struct Tok {
    int type;
    size_t start;
    int line;
    Str *str;           /* STRING, ERE, NAME, FUNC_NAME, BUILTIN */
    double num;         /* NUMBER */
    int bi;             /* BUILTIN: the function */
} Tok;

/* a list of nodes being built (linked by next) */
typedef struct List { Node *head, *last; int n; } List;

/* scanner */
extern Tok *lex_la;                 /* the token read last (the lookahead) */
void lex_begin(const char *src, size_t len);
void lex_reset(void);
int yylex(void);
_Noreturn void syntax_error_at(const Tok *t, const char *msg);
void lex_need_lookahead(void);      /* read the lookahead now if it is not read */
int lex_pipe_getline(void);         /* the token after the lookahead "|" is getline */

/* parse.c */
int word_token(const char *w, int *bi);   /* TK_ keyword, TK_BUILTIN or 0 */
void add_rule(Node *p1, Node *p2, Node *act);
void add_begin(Node *act);
void add_end(Node *act);

/* awk.y */
int yyparse(void);
void parser_reset(void);

#endif
