/*
 * yacc.h - declarations for yacc (POSIX.1-2024, XCU yacc).
 */
#ifndef YACC_H
#define YACC_H

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------- util.c ---------- */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

typedef struct Str {
    char *s;
    size_t len, cap;
} Str;
void str_add(Str *b, const char *p, size_t n);
void str_adds(Str *b, const char *s);
void str_addc(Str *b, int c);
void str_printf(Str *b, const char *fmt, ...);

extern const char *progname;
extern const char *input_name;
extern int nerrors;
void error_at(int line, const char *fmt, ...);     /* a grammar error; continues */
void warning_at(int line, const char *fmt, ...);
void fatal(const char *fmt, ...);                   /* exits */

/* bit sets */
typedef unsigned long Word;
#define WBITS (sizeof(Word) * CHAR_BIT)
#define NWORDS(n) (((size_t)(n) + WBITS - 1) / WBITS)
#define BIT_SET(s, i) ((s)[(size_t)(i) / WBITS] |= (Word)1 << ((size_t)(i) % WBITS))
#define BIT_CLR(s, i) ((s)[(size_t)(i) / WBITS] &= ~((Word)1 << ((size_t)(i) % WBITS)))
#define BIT_TEST(s, i) (((s)[(size_t)(i) / WBITS] >> ((size_t)(i) % WBITS)) & 1)

/* ---------- the grammar (reader.c) ---------- */
enum { UNKNOWN, TERM, NONTERM };
enum { NOASSOC = 0, LEFT, RIGHT, NONASSOC };

typedef struct Symbol {
    char *name;          /* as written: name, or 'c' for a literal */
    int index;           /* internal symbol number after packing */
    int class;           /* UNKNOWN, TERM, NONTERM */
    int value;           /* token number, -1 until assigned */
    int prec, assoc;
    char *tag;           /* type tag or NULL */
    int literal;         /* a character literal */
    int declared;        /* by %token, %left, %right, %nonassoc */
    int line;            /* first appearance */
    int used;            /* appears in a rule */
    struct Symbol *next; /* hash chain */
} Symbol;

typedef struct Rule {
    int lhs;             /* symbol index */
    int rhs;             /* index into ritem */
    int len;             /* number of rhs symbols */
    int prec, assoc;
    char *action;        /* translated action code, or NULL */
    int action_line;
    int line;
    int used;            /* reduced somewhere in the parser */
} Rule;

extern Symbol **symbols;     /* by internal index */
extern int nsyms, ntokens, nnonterms;
extern Rule *rules;
extern int nrules;
extern int *ritem;           /* symbols of right-hand sides; -(r+1) ends rule r */
extern int nitems;
extern int start_symbol;     /* the user's start symbol (index) */
extern int max_token_value;

/* declarations and code to copy */
typedef struct Chunk {
    int is_union;            /* the %union body, else %{ %} code */
    char *text;
    int line;
} Chunk;
extern Chunk *chunks;
extern int nchunks;
extern char *union_body;
extern int union_line;
extern int have_tags;        /* any <tag> appeared in a declaration */
extern char *programs;       /* the programs section, or NULL */
extern int programs_line;
extern int expect_sr, expect_rr;  /* %expect, -1 if not given */

void read_grammar(FILE *fp);
void grammar_abort(void);    /* main.c: give up after errors */

/* ---------- LR(0) and LALR(1) (lalr.c) ---------- */
typedef struct State {
    int nkernel;
    int *kernel;         /* item indices */
    int nshifts;
    int *shift_sym, *shift_to;   /* sorted by symbol */
    int nreds;
    int *red_rule;       /* sorted by rule */
    Word **la;           /* lookahead set per reduction (ntokens bits) */
    int accessing;       /* the symbol that leads here */
} State;

extern State *states;
extern int nstates;
extern int *nullable;        /* by symbol index */
extern int **derives;        /* rule lists per nonterminal, -1 terminated */

void build_lr0(void);
void build_lalr(void);
int goto_state(int state, int sym);    /* -1 if none */
int *closure(const int *kernel, int n, int *nout);

/* ---------- parser actions (tables.c) ---------- */
#define ACT_ERROR   0        /* no action: default */
/* actions per state and terminal: */
enum { A_NONE, A_SHIFT, A_REDUCE, A_ACCEPT, A_ERR };
typedef struct Action {
    unsigned char kind;
    int arg;                 /* state or rule */
} Action;

typedef struct Conflict {
    int state, token;
    int kind;                /* 0 s/r, 1 r/r */
    int rule1;               /* the rule chosen or shift target */
    int rule2;               /* the rule not used */
    int shift_to;            /* for s/r: shift target */
} Conflict;

typedef struct Resolution {
    int state, token, rule;
    int how;                 /* A_SHIFT, A_REDUCE, A_ERR */
} Resolution;

extern Action **actions;     /* [state][terminal] after resolution */
extern int *defred;          /* default reduction per state (rule) or -1 */
extern int *consistent;
extern Conflict *conflicts;
extern int nconflicts;
extern Resolution *resolutions;
extern int nresolutions;
extern int *state_sr, *state_rr;
extern int total_sr, total_rr;
extern int final_state;      /* state that accepts on $end */

void build_actions(void);
void report_conflicts(void);

/* ---------- output (output.c, skeleton.c, verbose.c) ---------- */
extern const char *file_prefix, *sym_prefix;
extern int opt_d, opt_l, opt_t, opt_v;
extern char *code_name, *header_name, *verbose_name;
void write_code(FILE *out, FILE *hdr);
void write_verbose(FILE *out);
void write_verbose_error(FILE *out);
void line_directive(FILE *out, int line, const char *file);
int is_c_name(const char *s);

#endif
