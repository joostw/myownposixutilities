/*
 * parse.h - the interface between the shell grammar (sh.y) and the
 * token classifier in parser.c.
 */
#ifndef PARSE_H
#define PARSE_H

/* a token as the grammar sees it */
typedef struct Tok {
    int type;           /* the raw token (T_* in parser.c) */
    int ytok;           /* the token code given to the grammar */
    int line;
    Word *word;         /* WORD, ASSIGNMENT_WORD, FNAME and reserved words */
    int ionum;          /* IO_NUMBER */
    int word_derived;   /* a word, possibly recognized as a reserved word */
    int cmd_pos;        /* in a place where a command must begin */
    Redir *here;        /* the redirection whose here-document this word delimits */
} Tok;

/* growing lists for the grammar's actions */
typedef struct Seq { Node **v; int n, cap; } Seq;           /* pipeline */
typedef struct WList { Word *head, **tail; } WList;
typedef struct RList { Redir *head, **tail; } RList;
typedef struct CList { CaseItem *head, **tail; } CList;
typedef struct SCmd { Node *n; Word **at, **wt; Redir **rt; } SCmd;
typedef struct RV { Redir *r; int line; } RV;               /* a redirection and its first line */

/* syntax error contexts: what the parser expected */
enum { CTX_GENERIC, CTX_EXPECT, CTX_RPAREN, CTX_CMDSUB };
_Noreturn void shy_syntax(int ctx, const char *expected);

extern Node *parse_result;
extern int parse_at_eof;

Node *new_node(int type, int line);
Node *make_list(Node *a, Node *b, int line);
int node_first_line(Node *n);

int shylex(void);
void shyerror(const char *s);

#endif
