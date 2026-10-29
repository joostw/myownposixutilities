/*
 * make.h - declarations for make (POSIX.1-2024, XCU make).
 */
#ifndef MAKE_H
#define MAKE_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ---------- util.c ---------- */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
extern const char *progname;
void diag(const char *fmt, ...) __attribute__((format(printf, 1, 2)));            /* make: ... */
void fatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2))); /* make: *** ...  Stop. */
void file_fatal(const char *file, int line, const char *fmt, ...)
    __attribute__((noreturn, format(printf, 3, 4)));
void file_warn(const char *file, int line, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

typedef struct Buf {
    char *s;
    size_t len, cap;
} Buf;
void buf_add(Buf *b, const char *p, size_t n);
void buf_adds(Buf *b, const char *s);
void buf_addc(Buf *b, int c);
void buf_reset(Buf *b);
char *buf_take(Buf *b);    /* the string, never NULL; the buffer is reset */
void buf_free(Buf *b);

/* a list of strings */
typedef struct Words {
    char **v;
    int n, cap;
} Words;
void words_add(Words *w, char *s);   /* takes ownership */
void words_free(Words *w);
/* split on blanks and newlines; parentheses keep "lib(a.o b.o)" together if parens != 0 */
void split_words(const char *s, Words *w, int parens);
int is_blank(int c);

/* ---------- var.c: macros ---------- */
enum { SRC_CMDLINE = 1, SRC_MAKEFLAGS = 2, SRC_ENV = 3, SRC_FILE = 4, SRC_BUILTIN = 5 };
typedef struct Var {
    char *name;
    char *value;
    int delayed;          /* delayed-expansion (recursive) */
    int source;
    int from_env;         /* was in the environment of make: exported with its current value */
    int expanding;
    struct Var *next;
} Var;
Var *var_get(const char *name);
/* define a macro from a source; obeys the override rules; returns 0 if ignored */
int var_set(const char *name, const char *value, int delayed, int source);
void var_append(const char *name, const char *value, int source);
void var_set_cond(const char *name, const char *value, int source);
void var_shell(const char *name, const char *cmd, int source);
char *expand(const char *s);                 /* full expansion, $$ -> $ */
char *expand_keep(const char *s);            /* :::= expansion, $$ kept */
void expand_into(Buf *out, const char *s, int keep_dd);
void var_foreach(void (*fn)(Var *v));
extern int opt_e;

/* internal macros of the target being processed */
typedef struct Auto {
    const char *at, *pct, *lt, *star;
    const char *q, *hat, *plus;  /* space-separated lists */
} Auto;
extern Auto *cur_auto;

/* ---------- nodes and rules ---------- */
typedef struct Cmds {
    char **v;      /* command lines as read (backslash-newline kept, continuation tabs removed) */
    int n;
    const char *file;
    int line;
} Cmds;

enum { ST_NEW, ST_SCAN, ST_RUN, ST_DONE };

typedef struct Node {
    char *name;
    struct Node **pre;     /* explicit prerequisites, in order; may contain wait_node */
    int npre, cappre;
    Cmds *cmds;            /* from the target rule with commands */
    int is_target;         /* named as a target in a rule */
    int phony, precious, silent, ignore;
    int dcolon;            /* :: rules */
    struct DRule *drules;  /* :: rules, in order */
    char *lib, *member;    /* archive library member lib(member) */
    /* runtime */
    int state, ok;
    int stat_done, exists;
    struct timespec mtime;
    dev_t dev;
    ino_t ino;
    int newer;             /* made and does not exist: newer than anything */
    int made;              /* commands were run (or would have been) */
    int visiting;
    int no_infer;          /* an implicit prerequisite: no inference rule for it */
    struct Node **all;     /* explicit + implicit prerequisites, in processing order */
    int nall;
    struct Node *implicit; /* the inferred prerequisite */
    Cmds *rcmds;           /* the commands to run */
    char *stem;            /* $* */
    int from_default;      /* commands from .DEFAULT */
    int dflt_ar;           /* commands are the built-in archive member insertion */
    struct timespec start_mtime;
    int start_exists;
    struct Node *hnext;
} Node;

typedef struct DRule {
    Node **pre;
    int npre, cappre;
    Cmds *cmds;
    struct DRule *next;
} DRule;

extern Node *wait_node;
Node *node_get(const char *name);       /* create if missing */
Node *node_find(const char *name);
void node_add_pre(Node *n, Node *p);
void node_foreach(void (*fn)(Node *n));
void node_reset_all(void);

/* inference rules */
typedef struct Infer {
    char *name;        /* ".c.o" or ".c" */
    Cmds *cmds;
    struct Infer *next;
} Infer;
Infer *infer_get(const char *name);
void infer_set(const char *name, Cmds *c);
void infer_clear_all(void);
void infer_foreach(void (*fn)(Infer *r));

/* pattern rules (extension): target pattern with one '%' */
typedef struct Pattern {
    char *target;
    char **pre;
    int npre;
    Cmds *cmds;
    struct Pattern *next;
} Pattern;
extern Pattern *patterns;

extern Words suffixes;
extern Cmds *default_cmds, *sccs_get_cmds;
extern int all_silent, all_ignore, all_precious, not_parallel, posix_mode;
extern Node *first_target;

/* ---------- parse.c ---------- */
void read_makefile(const char *name, int must_exist);   /* "-" is stdin */
void parse_string(const char *text, const char *name);  /* built-in rules */
int is_special_name(const char *s);
int is_inference_name(const char *s);

/* ---------- build.c ---------- */
extern int opt_i, opt_k, opt_n, opt_p, opt_q, opt_r, opt_s, opt_t, max_jobs;
extern int jobserver_r, jobserver_w;
char *shell_path(void);         /* the SHELL macro's value */
int build_goals(Words *goals, int report_uptodate);  /* returns 0 ok, 1 error, (q) 1 out of date */
int make_include(const char *name, int optional);    /* immediate remaking of an include file */
void setup_signals(void);
extern int question_outdated;
char **child_environ(int recursive);

/* ---------- ar.c ---------- */
/* modification time of a member of an archive library; 0 if found */
int ar_member_time(const char *lib, const char *member, struct timespec *t);
int ar_touch_member(const char *lib, const char *member);
int parse_lib_member(const char *name, char **lib, char **member);

/* ---------- main.c ---------- */
extern char *makeflags_value;
void print_database(void);

#endif
