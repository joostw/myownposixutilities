/*
 * lex.h - declarations for lex (POSIX.1-2024, XCU lex).
 */
#ifndef LEX_H
#define LEX_H

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
extern int nerrors;
/* a position in the (concatenated) input: file name and line */
void where(int line, const char **file, int *fline);
void error_at(int line, const char *fmt, ...);
void warning_at(int line, const char *fmt, ...);
void fatal(const char *fmt, ...);

/* ---------- character sets ---------- */
typedef struct CSet {
    unsigned char b[32];
} CSet;
#define CS_HAS(s, c) (((s)->b[(unsigned char)(c) >> 3] >> ((unsigned char)(c) & 7)) & 1)
#define CS_ADD(s, c) ((s)->b[(unsigned char)(c) >> 3] |= (unsigned char)(1 << ((unsigned char)(c) & 7)))

/* ---------- regular expressions (re.c) ---------- */
enum { N_EMPTY, N_SET, N_CAT, N_ALT, N_STAR, N_PLUS, N_QUEST, N_REP };
typedef struct Node {
    int type;
    int set;                 /* N_SET: index into the set table */
    struct Node *a, *b;
    int min, max;            /* N_REP: max -1 for no upper bound */
} Node;

extern CSet *sets;
extern int nsets;
int add_set(const CSet *s);
int fixed_length(Node *n);   /* -1 if the length varies */

/* a rule of the rules section */
typedef struct Rule {
    Node *re;                /* the whole pattern (r, or r followed by x) */
    Node *r, *x;             /* with trailing context: the two parts */
    int bol;                 /* ^ */
    int trailing;            /* has trailing context (r/x or $) */
    int rlen, xlen;          /* fixed lengths, or -1 */
    unsigned char *sc;       /* active in start condition i, per condition */
    int line;
    char *action;            /* NULL: "|" (the next rule's action) */
    int action_line;
    int uses_reject;
} Rule;

extern Rule *rules;
extern int nrules;

/* parses the pattern starting at *pp; returns 0 on error */
int parse_rule_pattern(const char **pp, Rule *r, int line);

/* definitions */
void define(const char *name, const char *subst, int line);

/* start conditions: 0 is INITIAL */
typedef struct SCond {
    char *name;
    int exclusive;
} SCond;
extern SCond *sconds;
extern int nsconds;
int find_scond(const char *name, size_t len);

/* ---------- the lex source (scan.c) ---------- */
typedef struct Code {
    char *text;
    int line;
} Code;
extern Code *defcode;        /* definitions section code, in order */
extern int ndefcode;
extern Code *precode;        /* rules section code before the first rule */
extern int nprecode;
extern Code *midcode;        /* rules section code after rules: unreachable */
extern int nmidcode;
extern int *midcode_after;   /* the rule each piece follows */
extern char *subroutines;
extern int subroutines_line;
extern int yytext_array;     /* %array */
extern int opt_noyywrap;
extern int table_sizes_given;
extern int any_reject, any_yymore;

void read_source(const char *text, size_t len);

/* ---------- automata (dfa.c) ---------- */
typedef struct DState {
    int *trans;              /* per character class, -1 for none */
    int nacc;
    int *acc;                /* rules accepted, in priority order */
    int racc, xacc;          /* accepts r / x of a trailing-context rule */
} DState;

extern DState *dstates;
extern int ndstates;
extern int nclasses;
extern int ec[256];          /* byte -> class */
extern int *start_state;     /* [2 * condition + bol] */
extern int *tc_rstart, *tc_xstart;  /* per rule: r and x automata, or -1 */
extern int nnfa;             /* statistics */
extern int ndstates_before_min;

void build_automata(void);

/* ---------- output (gen.c) ---------- */
extern const char *out_name;
extern long table_entries;
void generate(FILE *out);

#endif
