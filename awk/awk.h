/*
 * awk.h - shared declarations for a POSIX (IEEE Std 1003.1-2024) awk.
 */
#ifndef AWK_H
#define AWK_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <locale.h>
#include <regex.h>
#include <setjmp.h>
#include <errno.h>
#include <limits.h>
#include <wchar.h>
#include <wctype.h>
#include <stdarg.h>

/* ---------- reference-counted immutable strings ---------- */
typedef struct Str {
    size_t ref;
    size_t len;
    char s[];          /* always NUL terminated, may contain NULs */
} Str;

extern Str *str_empty;
Str *str_new(const char *s, size_t len);
Str *str_cstr(const char *s);
Str *str_static(const char *s);         /* never freed */
void str_free_(Str *s);
static inline Str *str_ref(Str *s) { s->ref++; return s; }
static inline void str_unref(Str *s) { if (s && --s->ref == 0) str_free_(s); }
unsigned long str_hash(const char *s, size_t len);

/* ---------- growable byte buffer ---------- */
typedef struct Buf {
    char *p;
    size_t len, cap;
} Buf;
void buf_init(Buf *b);
void buf_grow(Buf *b, size_t extra);
void buf_add(Buf *b, const char *s, size_t n);
void buf_addc(Buf *b, int c);
void buf_printf(Buf *b, const char *fmt, ...);
Str *buf_str(Buf *b);       /* copies contents into a new Str, resets len */
void buf_free(Buf *b);

/* ---------- values ---------- */
enum {
    T_UNINIT,   /* uninitialized: both 0 and "" */
    T_NUM,      /* number */
    T_STR,      /* string */
    T_STRNUM,   /* numeric string: s and n both valid */
    T_ARRAY,    /* array (only in variable cells) */
    T_REF       /* function parameter bound to an uninitialized caller cell */
};

typedef struct Array Array;
typedef struct Val {
    unsigned char t;
    unsigned char own;      /* T_ARRAY: cell owns the array */
    double n;
    Str *s;
    union { Array *a; struct Val *ref; } u;
} Val;

static inline void val_free(Val *v);
Val val_strnum(Str *s);     /* consumes s: T_STRNUM if numeric-looking else T_STR */
static inline Val val_num(double d) { Val v; v.t = T_NUM; v.own = 0; v.n = d; v.s = NULL; v.u.a = NULL; return v; }
static inline Val val_str(Str *s) { Val v; v.t = T_STR; v.own = 0; v.n = 0; v.s = s; v.u.a = NULL; return v; }
static inline Val val_uninit(void) { Val v; v.t = T_UNINIT; v.own = 0; v.n = 0; v.s = NULL; v.u.a = NULL; return v; }

/* ---------- arrays ---------- */
typedef struct Elem {
    Str *key;
    unsigned long hash;
    Val v;
    struct Elem *hnext;         /* hash chain */
    struct Elem *prev, *next;   /* insertion order */
} Elem;

struct Array {
    Elem **tab;
    size_t size, count;
    Elem *first, *last;
};

Array *array_new(void);
void array_clear(Array *a);
void array_free(Array *a);
Val *array_get(Array *a, Str *key, int create);
void array_delete(Array *a, Str *key);
Str **array_keys(Array *a, size_t *n);
void array_set_cstr(Array *a, const char *key, Val v);

static inline void val_free(Val *v)
{
    if (v->t == T_STR || v->t == T_STRNUM)
        str_unref(v->s);
    else if (v->t == T_ARRAY && v->own)
        array_free(v->u.a);
    v->t = T_UNINIT;
}

static inline Val val_copy(const Val *v)
{
    Val r = *v;
    if (r.t == T_STR || r.t == T_STRNUM)
        str_ref(r.s);
    return r;
}

/* ---------- numbers ---------- */
extern int radix_char;          /* LC_NUMERIC decimal point */
size_t scan_number(const char *s, size_t len, int radix, int allow_sign);
double conv_number(const char *s, size_t n, int radix);
double str2num(const char *s, size_t len);
int looks_numeric(const char *s, size_t len, double *out);
Str *num2str(double d, Str *fmt);

/* ---------- formatting (printf / sprintf) ---------- */
void format_awk(Buf *out, const char *fmt, size_t flen, Val *args, int nargs);

/* ---------- regular expressions ---------- */
typedef struct Regex {
    regex_t re;
} Regex;
Regex *re_compile(const char *src, size_t len, int add_newline_alt);
Regex *re_dynamic(Str *s);
Regex *re_dynamic_fs(Str *s, int paragraph);
int re_exec(Regex *r, const char *s, int notbol, size_t *so, size_t *eo);
int parse_escape(const char *s, size_t i, size_t n, int *ch);

/* ---------- multibyte helpers ---------- */
extern int mb_mode;
size_t mb_charlen(const char *s, size_t n);
size_t mb_strlen(const char *s, size_t n);
size_t mb_offset(const char *s, size_t n, size_t nchars);
extern int collate_c;

/* ---------- errors / memory ---------- */
extern const char *progname;
_Noreturn void fatal(const char *fmt, ...);
void warning(const char *fmt, ...);
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* ---------- AST ---------- */
enum {
    /* expressions */
    N_NUM, N_STR, N_ERE, N_GVAR, N_LVAR, N_INDEX, N_FIELD, N_ASSIGN,
    N_COND, N_OR, N_AND, N_NOT, N_IN, N_MATCH, N_CMP, N_CONCAT, N_ARITH,
    N_NEG, N_PLUS, N_PREINC, N_PREDEC, N_POSTINC, N_POSTDEC, N_CALL,
    N_BUILTIN, N_GETLINE, N_GROUP, N_GROUPLIST,
    /* statements */
    N_PRINT, N_PRINTF, N_IF, N_WHILE, N_DO, N_FOR, N_FORIN, N_BLOCK,
    N_EXPRSTMT, N_DELETE, N_NEXT, N_NEXTFILE, N_EXIT, N_RETURN, N_BREAK,
    N_CONTINUE
};

typedef struct Node {
    int type;
    int op;
    int line;
    int idx;
    int nargs;
    struct Node *a, *b, *c, *d;
    struct Node *next;
    double num;
    Str *str;
    Regex *re;
} Node;

/* builtin function ids */
enum {
    B_ATAN2, B_CLOSE, B_COS, B_EXP, B_FFLUSH, B_GSUB, B_INDEX, B_INT,
    B_LENGTH, B_LOG, B_MATCH, B_RAND, B_SIN, B_SPLIT, B_SPRINTF, B_SQRT,
    B_SRAND, B_SUB, B_SUBSTR, B_SYSTEM, B_TOLOWER, B_TOUPPER
};

/* tokens used as operators inside nodes */
enum {
    TK_EOF = 256, TK_NEWLINE, TK_LBRACE, TK_RBRACE, TK_LPAREN, TK_RPAREN,
    TK_LBRACKET, TK_RBRACKET, TK_COMMA, TK_SEMI, TK_PLUS, TK_MINUS,
    TK_STAR, TK_SLASH, TK_PERCENT, TK_CARET, TK_NOT, TK_GT, TK_LT,
    TK_PIPE, TK_QUESTION, TK_COLON, TK_TILDE, TK_DOLLAR, TK_ASSIGN,
    TK_ADD_ASSIGN, TK_SUB_ASSIGN, TK_MUL_ASSIGN, TK_DIV_ASSIGN,
    TK_MOD_ASSIGN, TK_POW_ASSIGN, TK_OR, TK_AND, TK_NOMATCH, TK_EQ,
    TK_LE, TK_GE, TK_NE, TK_INCR, TK_DECR, TK_APPEND,
    TK_NUMBER, TK_STRING, TK_ERE, TK_NAME, TK_FUNC_NAME, TK_BUILTIN,
    TK_BEGIN, TK_END, TK_BREAK, TK_CONTINUE, TK_DELETE, TK_DO, TK_ELSE,
    TK_EXIT, TK_FOR, TK_FUNCTION, TK_GETLINE, TK_IF, TK_IN, TK_NEXT,
    TK_NEXTFILE, TK_PRINT, TK_PRINTF, TK_RETURN, TK_WHILE
};

typedef struct Func {
    char *name;
    int nparams;
    char **params;
    Node *body;
    int defined;
    int line;
} Func;

typedef struct Rule {
    Node *pat1, *pat2;      /* pat2 != NULL: range pattern */
    Node *action;           /* NULL: { print } */
    int inrange;
} Rule;

/* special variable slots in the global table */
enum {
    V_NF, V_NR, V_FNR, V_FS, V_OFS, V_ORS, V_RS, V_FILENAME, V_SUBSEP,
    V_RSTART, V_RLENGTH, V_CONVFMT, V_OFMT, V_ENVIRON, V_ARGC, V_ARGV,
    V_NSPECIAL
};

/* program (filled by parser) */
extern Val **gvars;
extern char **gnames;
extern int ngvars;
extern Func **funcs;
extern int nfuncs;
extern Node **begin_actions, **end_actions;
extern int nbegin, nend;
extern Rule *rules;
extern int nrules;

int global_lookup(const char *name, int create);   /* -1 if absent */
int func_lookup(const char *name);
int is_reserved_word(const char *name);
void parse_program(const char *src, size_t len);

/* interpreter */
void run_init(int argc, char **argv, char **envp);
int  run_program(void);
void cmdline_assign(const char *arg);
int  is_assignment_operand(const char *s);
Str *process_cmdline_escapes(const char *s);

#endif
