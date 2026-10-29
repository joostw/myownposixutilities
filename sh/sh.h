/*
 * sh.h - shared declarations for a POSIX (IEEE Std 1003.1-2024) shell.
 */
#ifndef SH_H
#define SH_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <locale.h>
#include <wchar.h>
#include <wctype.h>

/* ------------------------------------------------------------------ */
/* memory                                                             */
/* ------------------------------------------------------------------ */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* stack allocator: released in LIFO order with marks */
typedef struct StackMark { void *chunk; size_t used; } StackMark;
void *stalloc(size_t n);
char *ststrdup(const char *s);
char *ststrndup(const char *s, size_t n);
void setstackmark(StackMark *m);
void popstackmark(StackMark *m);

/* growable byte buffer (heap) */
typedef struct StrBuf { char *s; size_t len, cap; } StrBuf;
void sb_init(StrBuf *b);
void sb_addc(StrBuf *b, int c);
void sb_add(StrBuf *b, const char *s, size_t n);
void sb_adds(StrBuf *b, const char *s);
void sb_printf(StrBuf *b, const char *fmt, ...);
char *sb_stack(StrBuf *b);   /* copy to stack memory, free heap buffer */
char *sb_take(StrBuf *b);    /* return heap string, reset buffer */
void sb_free(StrBuf *b);

/* ------------------------------------------------------------------ */
/* exceptions                                                         */
/* ------------------------------------------------------------------ */
struct jmploc { jmp_buf loc; };
extern struct jmploc *handler;
extern int exception;
enum { EX_ERROR = 1, EX_EXIT, EX_INT, EX_EXEC };
_Noreturn void raise_exc(int e);

/* errors */
extern const char *arg0;            /* shell name for messages */
extern char *commandname;           /* utility name for messages, or NULL */
void sh_warn(const char *fmt, ...);
_Noreturn void sh_error(const char *fmt, ...);    /* shell error: EX_ERROR */
_Noreturn void syntax_error_at(int line, const char *fmt, ...);

/* ------------------------------------------------------------------ */
/* output buffering for builtins                                      */
/* ------------------------------------------------------------------ */
typedef struct Out { int fd; char buf[4096]; size_t len; int err; } Out;
extern Out out1, out2;
void outc(Out *o, int c);
void outs(Out *o, const char *s);
void outn(Out *o, const char *s, size_t n);
void outfmt(Out *o, const char *fmt, ...);
int  outflush(Out *o);
void flushall(void);

/* ------------------------------------------------------------------ */
/* options                                                            */
/* ------------------------------------------------------------------ */
enum {
    O_ALLEXPORT, O_NOTIFY, O_NOCLOBBER, O_ERREXIT, O_NOGLOB, O_HASHALL,
    O_INTERACTIVE, O_MONITOR, O_NOEXEC, O_NOUNSET, O_VERBOSE, O_XTRACE,
    O_IGNOREEOF, O_NOLOG, O_PIPEFAIL, O_VI, O_STDIN, O_CMDSTR, NOPTS
};
extern char opts[NOPTS];
#define Oflag(o) (opts[o])
extern const struct OptDesc { const char *name; char letter; } optdesc[NOPTS];
int set_option_letter(int c, int on);       /* -1 if unknown */
int set_option_name(const char *name, int on);
char *optflags_string(void);                 /* for $- (stack) */

/* ------------------------------------------------------------------ */
/* words, AST                                                         */
/* ------------------------------------------------------------------ */
/* control bytes inside encoded words */
#define CTLESC    '\201'    /* next byte is a quoted literal */
#define CTLVAR    '\202'    /* ${...}: CTLVAR type name '=' word CTLENDVAR */
#define CTLENDVAR '\203'
#define CTLBACKQ  '\204'    /* command substitution: CTLBACKQ index ';' */
#define CTLARI    '\205'    /* arithmetic: CTLARI word CTLENDARI */
#define CTLENDARI '\206'
#define CTLQUOTE  '\207'    /* start of double-quoted region */
#define CTLENDQ   '\210'    /* end of double-quoted region */
#define CTLQNULL  '\211'    /* marks a quoting (e.g. '' ) at this place */
#define ISCTL(c)  ((unsigned char)(c) >= 0x81 && (unsigned char)(c) <= 0x89)

/* ${} subtypes (stored as a printable byte) */
enum {
    VS_NORMAL = 'a', VS_MINUS, VS_PLUS, VS_QUESTION, VS_ASSIGN,
    VS_TRIMLEFT, VS_TRIMLEFTMAX, VS_TRIMRIGHT, VS_TRIMRIGHTMAX, VS_LENGTH
};
#define VS_COLON_OFF 16     /* VS_x + 16 encodes the ':' variant */

struct Node;
typedef struct Word {
    char *s;                /* encoded text (NUL terminated) */
    struct Node **subs;     /* command substitutions referenced by index */
    int nsubs;
    int quoted;             /* contained any quoting */
    struct Word *next;
} Word;

enum {
    R_IN, R_OUT, R_CLOBBER, R_APPEND, R_RDWR, R_DUPIN, R_DUPOUT,
    R_HERE, R_HERESTRIP
};

typedef struct Redir {
    int type;
    int fd;
    Word *word;             /* target or here-doc delimiter */
    Word *here;             /* here-document body (encoded) */
    int hquoted;            /* here-doc delimiter was quoted: no expansion */
    struct Redir *next;
} Redir;

enum {
    N_SIMPLE, N_PIPE, N_AND, N_OR, N_LIST, N_ASYNC, N_NOT, N_SUBSHELL,
    N_BRACE, N_IF, N_WHILE, N_UNTIL, N_FOR, N_CASE, N_FUNCDEF
};

typedef struct CaseItem {
    Word *patterns;
    struct Node *body;
    int fallthrough;        /* ;& */
    struct CaseItem *next;
} CaseItem;

typedef struct Node {
    int type;
    int lineno;
    Redir *redir;           /* for compound commands and simple commands */
    union {
        struct { Word *assigns; Word *words; } simple;
        struct { struct Node **cmds; int n; } pipe;        /* N_PIPE */
        struct { struct Node *a, *b; } bin;                /* AND OR LIST */
        struct { struct Node *a; char *text; } un;         /* ASYNC NOT SUBSHELL BRACE */
        struct { struct Node *cond, *then, *els; } nif;
        struct { struct Node *cond, *body; } loop;
        struct { char *var; Word *words; int has_in; struct Node *body; } nfor;
        struct { Word *word; CaseItem *items; } ncase;
        struct { char *name; struct Node *body; } func;
    } u;
} Node;

/* parser */
Node *parse_command(int *eof);      /* next complete_command from current input */
Node *parse_string_list(const char *s, int lineno);  /* whole string as compound list */
Node *copy_tree(Node *n);           /* deep copy to heap */
void free_tree(Node *n);
extern int parse_lineno;
int is_name(const char *s);
int is_name_n(const char *s, size_t n);
extern int checkkwd_disabled;

struct Alias;
/* ------------------------------------------------------------------ */
/* input                                                              */
/* ------------------------------------------------------------------ */
void push_input_fd(int fd, int interactive_src);
void push_input_string(const char *s, int heap);
void push_alias(const char *s, struct Alias *a);
void pop_input(void);
int  input_depth(void);
int  pgetc(void);
void pungetc(int c);
void input_sync(void);               /* give back read-ahead on stdin */
void set_prompt(int which);          /* 1 = PS1, 2 = PS2, 0 = none */
extern int input_lineno_base;
int  input_is_interactive(void);
void input_discard_line(void);
int  input_at_eof(void);

/* aliases */
typedef struct Alias {
    char *name, *val;
    int inuse;
    struct Alias *next;
} Alias;
Alias *alias_lookup(const char *name);
void alias_set(const char *name, const char *val);
int alias_unset(const char *name);
void alias_unset_all(void);
void alias_print_all(void);
void alias_print(Alias *a);

/* ------------------------------------------------------------------ */
/* variables                                                          */
/* ------------------------------------------------------------------ */
enum { V_EXPORT = 1, V_READONLY = 2 };
typedef struct Var {
    char *name;
    char *val;              /* NULL: unset */
    int flags;
    struct Var *next;
} Var;

void var_init(char **envp);
Var *var_lookup(const char *name);
const char *var_get(const char *name);          /* NULL if unset */
int  var_set(const char *name, const char *val, int flags);  /* -1 readonly */
void var_set_or_die(const char *name, const char *val);
int  var_unset(const char *name);
void var_export(const char *name);
char **var_environ(void);                       /* heap array */
void var_list(Out *o, int flagmask, const char *prefix);
void var_set_int(const char *name, long v);
const char *ifs_value(void);    /* IFS value, or default " \t\n" if unset */
int ifs_isset(void);

/* temporary assignments (for regular builtins and functions) */
typedef struct VarSave { char *name; char *val; int flags; int existed; struct VarSave *next; } VarSave;
VarSave *var_save(const char *name);
void var_restore(VarSave *s);

/* positional parameters */
typedef struct Params { int n; char **v; int malloced; } Params;
extern Params posparams;
void params_set(char **argv, int n);
void params_free(Params *p);
extern char *arg_zero;      /* $0 */
extern int exitstatus;      /* $? */
extern pid_t shell_pid;     /* $$ */
extern pid_t last_bgpid;    /* $! */
extern int last_bgpid_set;

/* ------------------------------------------------------------------ */
/* expansion                                                          */
/* ------------------------------------------------------------------ */
enum { EXP_FULL = 1, EXP_NOSPLIT = 2, EXP_PATTERN = 4, EXP_ASSIGN = 8,
       EXP_TILDE = 16, EXP_REDIR = 32, EXP_CASEWORD = 64, EXP_HEREDOC = 128 };
typedef struct ArgList { char **v; int n, cap; } ArgList;
void arglist_add(ArgList *l, char *s);
void expand_words(Word *w, ArgList *out);       /* full expansion */
void expand_word_full(Word *w, ArgList *out);
char *expand_word_str(Word *w, int flags);       /* single string (stack) */
char *expand_heredoc(Word *w);                  /* stack */
char *expand_prompt(const char *ps);            /* stack */
int  pattern_match(const char *pat, const char *s);   /* backslash-escaped pattern */
int  has_glob_chars(const char *pat);
long arith_eval(const char *expr);
extern int cmdsub_status_set, cmdsub_status;
int  is_assignment_word(Word *w);               /* NAME= with unquoted '=' */
char *word_literal(Word *w);                    /* literal text if unquoted & no expansions, else NULL */

/* ------------------------------------------------------------------ */
/* execution                                                          */
/* ------------------------------------------------------------------ */
enum { EV_EXIT = 1, EV_TESTED = 2, EV_BACKGROUND = 4 };
int  eval_tree(Node *n, int flags);
void eval_string(const char *s, int flags);
int  eval_string_status(const char *s);
extern int evalskip;        /* SKIPBREAK, SKIPCONT, SKIPRETURN */
extern int skipcount;
extern int loopnest;
extern int funcnest;
extern int dotnest;
extern int errexit_suppressed;
enum { SKIPBREAK = 1, SKIPCONT, SKIPRETURN };
_Noreturn void exitshell(int status);
_Noreturn void sh_exit_now(int status);
extern int in_subshell;
void enter_subshell(int keep_traps);
char *find_command(const char *name, const char *path, int *is_builtin_assoc);
void hash_clear(void);
void hash_print(void);
int  hash_add(const char *name);
int  run_file(const char *path, int must_exist);
_Noreturn void exec_external(char **argv, char **envp, const char *path_override);
int  redirect(Redir *r, int save);      /* returns 0 ok, -1 failure (message written) */
void undo_redirects(int depth);
int  redir_depth(void);
void clear_redir_saves(void);

/* functions */
typedef struct Func { char *name; Node *body; struct Func *next; } Func;
Func *func_lookup(const char *name);
void func_define(const char *name, Node *body);
int  func_unset(const char *name);
void func_clear_all(void);

/* builtins */
typedef int (*bltin_fn)(int argc, char **argv);
enum { BI_SPECIAL = 1, BI_INTRINSIC = 2, BI_REGULAR = 4, BI_DECL = 8 };
typedef struct Builtin { const char *name; bltin_fn fn; int flags; } Builtin;
const Builtin *builtin_lookup(const char *name);
extern const Builtin builtins[];
int run_builtin(const Builtin *b, int argc, char **argv);
extern int builtin_running;

/* option parsing helper for builtins */
extern int bopt_ind;
extern char *bopt_arg;
int bgetopt(int argc, char **argv, const char *optstr);

/* ------------------------------------------------------------------ */
/* jobs and signals                                                   */
/* ------------------------------------------------------------------ */
typedef struct Proc { pid_t pid; int status; int state; } Proc;
enum { JOB_RUNNING, JOB_STOPPED, JOB_DONE };
typedef struct Job {
    int used;
    int num;
    pid_t pgid;
    int nprocs;
    Proc *procs;
    int state;
    int changed;            /* status changed, not reported */
    int bg;                 /* background job */
    int jobctl;             /* created with job control */
    int pipefail;
    char *cmd;
} Job;
Job *job_new(int nprocs, int bg);
void job_add_proc(Job *j, pid_t pid);
int  job_wait(Job *j);              /* foreground wait; returns status */
int  job_status(Job *j);
void job_free(Job *j);
void jobs_reap(int block);
void jobs_notify(void);
Job *job_by_spec(const char *spec, int report);
Job *job_by_pid(pid_t pid);
void jobs_init(void);
int  job_ctl_enabled(void);
void set_job_control(int on);
extern pid_t shell_pgid;
extern int tty_fd;
int  wait_status_to_exit(int st);
char *job_state_str(Job *j, char *buf, size_t n);
extern Job *jobtab;
extern int njobs;
extern int curjob, prevjob;
void job_set_current(Job *j);
void jobs_forget_all(void);

/* traps */
#define NSIGS 65
extern char *trap_actions[NSIGS];   /* index 0: EXIT */
extern volatile sig_atomic_t pending_sig[NSIGS];
extern volatile sig_atomic_t gotsig;
extern volatile sig_atomic_t intpending;
void signals_init(void);
void run_pending_traps(void);
void set_signal(int sig);
void reset_traps_for_subshell(void);
int  sig_by_name(const char *name);
const char *sig_name(int sig);
void signals_child_reset(int async_nojc);
extern int sig_ignored_at_entry[NSIGS];
extern char **saved_parent_traps;
extern int in_trap;
extern int trap_status_before;

/* ------------------------------------------------------------------ */
/* interactive                                                        */
/* ------------------------------------------------------------------ */
char *read_line_interactive(const char *prompt);    /* heap; NULL on EOF */
void history_add(const char *line);
void history_init(void);
int  history_next_number(void);
void history_save(void);
int  bi_fc(int argc, char **argv);
void edit_reset_mode(void);

bltin_fn bltin_exec_fn(void);
const char *command_default_path(void);
void exec_external_path(const char *path, char **argv, char **envp);
void set_job_control_child(void);
void clear_redir_saves_keep_fds(void);
void history_new_entry(void);
int mb_mode_cont(int c);
void input_reset_eof(void);
char *expand_prompt_params(const char *s);
Word *parse_prompt_word(const char *s);
void mark_continuation_source(void);
void print_job(Out *o, Job *j, int longfmt);
int job_continue(Job *j, int fg);
void set_notify(int on);
void mail_check(void);
int notify_pending(void);
void getopts_reset(void);

char *node_text(Node *n);
void *parser_push_state(void);
void parser_pop_state(void *p);

/* misc */
extern int interactive;
extern int shell_exiting;
long parse_long(const char *s, int *ok);
char *quote_for_reinput(const char *s);     /* stack */
int  is_number(const char *s);
extern char *pwd_logical;
void pwd_init(void);
char *getcwd_heap(void);
extern int cmd_lineno;

#endif
