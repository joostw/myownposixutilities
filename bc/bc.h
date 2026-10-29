/*
 * bc.h - declarations for bc (POSIX.1-2024, XCU bc).
 */
#ifndef BC_H
#define BC_H

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "num.h"

/* the limits this bc reports and enforces */
#define BC_BASE_MAX_V 999999999L
#define BC_DIM_MAX_V 16777215L
#define BC_SCALE_MAX_V 2147483647L
#define BC_STRING_MAX_V 2147483647L

/* ---------- the syntax tree ---------- */
enum {
    /* expressions */
    E_NUM, E_VAR, E_ELEM, E_SCALE, E_IBASE, E_OBASE, E_CALL, E_NEG, E_ADD, E_SUB, E_MUL, E_DIV, E_MOD,
    E_POW, E_PREINC, E_PREDEC, E_POSTINC, E_POSTDEC, E_ASSIGN, E_LENGTH, E_SQRT, E_SCALEF, E_REL,
    E_AND, E_OR, E_NOT, E_ARRAYARG,
    /* statements */
    S_EXPR, S_STRING, S_BREAK, S_CONTINUE, S_HALT, S_RETURN, S_FOR, S_IF, S_WHILE, S_BLOCK, S_PRINT
};

/* relational operators and assignment operators (op of E_REL / E_ASSIGN) */
enum { R_EQ, R_LE, R_GE, R_NE, R_LT, R_GT };
enum { A_SET, A_ADD, A_SUB, A_MUL, A_DIV, A_MOD, A_POW };

struct Sym;
typedef struct Node {
    int kind, op, line;
    int paren;               /* written in parentheses (an assignment then prints) */
    char *str;               /* E_NUM digits, S_STRING text */
    struct Sym *sym;         /* E_VAR, E_ELEM, E_CALL, E_ARRAYARG */
    struct Node *a, *b, *c, *d;
    struct Node *next;       /* lists: statements, arguments, print items */
    Num cache;               /* E_NUM: the value in base cache_base */
    int cache_base;
} Node;

typedef struct Param {
    struct Sym *sym;
    int array;
    struct Param *next;
} Param;

typedef struct Func {
    int nparams;
    Param *params, *autos;
    Node *body;
    int builtin;             /* 0, or the math library function */
} Func;

typedef struct Array {
    Num *v;
    long n;
} Array;

typedef struct Sym {
    char *name;
    Num *vals;               /* a stack: the current value is the last */
    int nvals, cvals;
    Array **arrs;
    int narrs, carrs;
    Func *func;
    struct Sym *next;
} Sym;

/* ---------- main.c ---------- */
extern int interactive;
extern int had_error;
extern const char *cur_file;
extern int cur_line;
void bc_nomem(void);
void bc_error(const char *fmt, ...);        /* a syntax or definition error */
void bc_runtime_error(const char *fmt, ...); /* aborts the current execution */
void bc_warning(const char *fmt, ...);
void bc_quit(int status);

/* ---------- exec.c ---------- */
Sym *lookup(const char *name);
Node *node(int kind, Node *a, Node *b);
Node *append(Node *list, Node *n);
void free_tree(Node *n);
void run_item(Node *list);                  /* a semicolon_list: check and execute */
void define_function(Sym *name, Param *params, Param *autos, Node *body, int line);
void exec_init(void);
extern int exec_line;               /* the line of the statement being executed */
void set_stack_limit(void *base, size_t size);
extern long reg_scale, reg_ibase, reg_obase;
void exec_abort(void);                       /* back to the top level after an error */
extern volatile sig_atomic_t interrupted;
void out_flush(void);

/* ---------- mathlib.c ---------- */
enum { M_NONE, M_S, M_C, M_A, M_L, M_E, M_J };
void math_define(void);
Num math_call(int which, Num *args, int nargs, long scale);

/* ---------- the parser and scanner ---------- */
int yyparse(void);
int yylex(void);
void yyerror(const char *s);
void scanner_add_file(const char *name);    /* the files to read, then stdin */
void scanner_add_text(const char *name, const char *text);
int scanner_next_input(void);               /* yywrap's work */

#endif
