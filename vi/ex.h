/*
 * ex.h - shared declarations for the ex/vi editor (POSIX.1-2024, XCU ex and vi).
 */
#ifndef EX_H
#define EX_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#define VERSION "posix-vi 1.0 (POSIX.1-2024 ex/vi)"

/* ---------- util.c ---------- */
typedef struct Str {
    char *s;
    size_t len, cap;
} Str;

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
void *xcalloc(size_t a, size_t b);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
void str_init(Str *b);
void str_addc(Str *b, int c);
void str_addn(Str *b, const char *s, size_t n);
void str_adds(Str *b, const char *s);
void str_fmt(Str *b, const char *fmt, ...);
void str_clear(Str *b);
void str_free(Str *b);
char *str_detach(Str *b);

/* error handling: ex_err() writes a message and longjmps to the innermost
 * handler established by protect(). */
extern jmp_buf *errjmp;
void ex_err(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void ex_err_silent(void) __attribute__((noreturn));
int protect(void (*fn)(void *), void *arg);

/* messages and output */
void out_write(const char *s, size_t n);   /* raw stdout */
void out_str(const char *s);
void out_flush(void);
void msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));  /* informational */
void errmsg_show(const char *text);
void diag(const char *fmt, ...) __attribute__((format(printf, 1, 2)));  /* stderr */
void bell(void);

/* characters */
extern int mb;                 /* locale has multibyte characters */
int ch_len(const char *s, int len, int pos);     /* bytes in char at pos (>=1) */
int ch_prev(const char *s, int pos);            /* start of char before pos */
wint_t ch_wc(const char *s, int len, int pos);  /* character value */
int ch_width(const char *s, int len, int pos, int col); /* display width at column col (0-based) */
int is_wordc(wint_t c);
int is_blankc(int c);
int col_of(const char *s, int len, int off);   /* display column (0-based) where char at off starts */
int off_of_col(const char *s, int len, int col); /* char whose display covers col */
int char_disp(Str *out, const char *s, int len, int pos, int col, int list); /* render one char */

/* ---------- options.c ---------- */
enum {
    OP_AUTOINDENT, OP_AUTOPRINT, OP_AUTOWRITE, OP_BEAUTIFY, OP_DIRECTORY, OP_EDCOMPATIBLE,
    OP_ERRORBELLS, OP_EXRC, OP_IGNORECASE, OP_LIST, OP_MAGIC, OP_MESG, OP_NUMBER,
    OP_PARAGRAPHS, OP_PROMPT, OP_READONLY, OP_REDRAW, OP_REMAP, OP_REPORT, OP_SCROLL,
    OP_SECTIONS, OP_SHELL, OP_SHIFTWIDTH, OP_SHOWMATCH, OP_SHOWMODE, OP_SLOWOPEN,
    OP_TABSTOP, OP_TAGLENGTH, OP_TAGS, OP_TERM, OP_TERSE, OP_WARN, OP_WINDOW,
    OP_WRAPMARGIN, OP_WRAPSCAN, OP_WRITEANY,

    OP_NOPTS
};
enum { OT_BOOL, OT_NUM, OT_STR };
typedef struct Opt {
    const char *name, *abbr;
    int type;
    long num;
    char *str;
    int changed;      /* differs from default / explicitly set */
    int user_set;     /* explicitly set by the user (readonly) */
    int readonly_opt; /* informational only */
} Opt;
extern Opt opts[];
#define OV(o) (opts[o].num)
#define OS(o) (opts[o].str)
void opts_init(void);
void cmd_set(const char *arg);
void opt_set_num(int o, long v);
void opt_window_set(long v);
void opts_screen_changed(void);

/* ---------- buf.c ---------- */
typedef struct Line {
    char *s;
    int len;
    unsigned long id;
} Line;

extern int nlines;                 /* number of lines in the edit buffer */
Line *lp(int i);                   /* 1-based; valid until next modification */
#define LT(i) (lp(i)->s)
#define LL(i) (lp(i)->len)
unsigned long new_line_id(void);
void buf_insert(int after, char *const *texts, const int *lens, int n);
void buf_insert1(int after, const char *s, int len);
void buf_delete(int from, int to);
void buf_change(int i, const char *s, int len);
void buf_reset(void);              /* empty the buffer, no undo */
void buf_load_line(const char *s, int len);  /* append during file load (no undo) */
int line_of_id(unsigned long id);
extern int modified;
extern long change_count;          /* incremented on every modification */

/* undo */
void undo_open(void);    /* begin a new undoable command (lazily) */
void undo_close(void);
void undo_discard(void);
int do_undo(int *first_changed, int *line_before_del, int *what);
extern int undo_inhibit_new;  /* nesting: keep adding to the current group */

/* lines added/deleted accounting for report option */
extern long rep_added, rep_deleted;

/* marks */
#define MARK_CONTEXT 26
typedef struct Mark {
    unsigned long id;
    int off;          /* byte offset of character */
    int set;
} Mark;
extern Mark marks[27];
int mark_index(int c);
void mark_set(int idx, int line, int off);
int mark_get(int idx, int *line, int *off);  /* 0 if unset/deleted */
void set_context_mark(void);

/* registers (buffers): 0 = unnamed, 1..9 numeric, 10..35 a..z */
typedef struct Reg {
    int linemode;
    int n;
    char **t;
    int *l;
} Reg;
extern Reg regs[36];
int reg_index(int c, int *append);
void reg_free(Reg *r);
void reg_copy(Reg *dst, const Reg *src);
void reg_store_lines(int c, int from, int to, int shift_numeric);
void reg_store(int c, Reg *r, int shift_numeric);   /* takes ownership of r contents */
Reg *reg_get(int c);

/* ---------- regex.c ---------- */
typedef struct Regex Regex;
#define NSUB 10
typedef struct Match {
    int so[NSUB], eo[NSUB];
} Match;
Regex *re_compile(const char *src, int delim, const char **endp, int magic, char **errp);
void re_free(Regex *re);
Regex *re_ref(Regex *re);
int re_exec(Regex *re, const char *s, int len, int start, int anchored, Match *m);
int re_nsub(Regex *re);
const char *re_skip(const char *p, int delim);   /* end of a delimited RE */
/* the last regular expression, last s pattern, and last replacement */
extern Regex *last_re;
extern char *last_repl;         /* previous replacement text (for ~) */
void set_last_re(Regex *re);
Regex *get_re(const char **pp, int delim, int set_last);  /* parse delimited RE; empty = last */
/* replacement strings */
typedef struct Repl Repl;
Repl *repl_compile(const char *src, int delim, const char **endp, int magic);
void repl_free(Repl *r);
void repl_apply(Repl *r, const char *line, int len, Match *m, Str *out);
#define REPL_SPLIT '\n'    /* in output: a newline splits the line */

/* ---------- excmd.c ---------- */
enum { MODE_EX, MODE_VI, MODE_OPEN };
extern int mode;
extern int cur;                 /* current line */
extern int cur_off;             /* current character (byte offset) */
extern int want_col;            /* remembered display column (0-based), -1 = end of line */
extern int silent;              /* -s or stdin not a terminal */
extern int interactive;         /* stdin is a terminal */
extern int batch_stdin;         /* stdin is not a terminal */
extern volatile sig_atomic_t intr;
extern volatile sig_atomic_t got_winch, got_cont, got_hup;
extern int in_global;           /* executing global/v */
extern int in_exec_buffer;      /* executing a buffer */
extern int search_dir;          /* remembered search direction: 0 unset, 1 fwd, -1 back */
extern int last_print_flags;
extern char *curpath, *altpath;
extern int path_changed_by_file;   /* current pathname set by file/read */
extern int ex_quit_pending;     /* last command was a quit attempt */

typedef struct Source Source;
struct Source {
    char *(*get)(Source *s, int textmode, int lnum, const char *indent);
    int kind;
    const char *p;          /* string sources */
    char *buf;
    int fd;
    Source *prev;
    int lineno;
};
enum { SRC_TTY, SRC_FD, SRC_STR, SRC_VI };
void src_string(Source *s, const char *text);
void src_fd(Source *s, int fd);
char *src_tty_get(Source *s, int textmode, int lnum, const char *indent);
int ex_run(const char *text, Source *src);   /* execute command lines; -1 on error */
int ex_run_source(Source *src, int stop_on_error);
void ex_loop(void);
void set_nonblank(void);
const char *skipbl(const char *p);
int first_nonblank(int line);
void print_lines(int a, int b, int flags);
#define PF_NUM 1
#define PF_LIST 2
#define PF_PRINT 4
void autoprint_reset(void);
extern int command_lines_printed;
void report_changes(long before_lines, const char *what, long n);
char *expand_file_arg(const char *arg, int multi, char ***list, int *nlist);
void arg_list_set(char **files, int n);
extern char **arglist;
extern int nargs, argidx;
int edit_file(const char *path, int force, const char *pluscmd, int is_initial);
void ex_cmd_from_vi(const char *cmd);
extern Source *cur_source;
extern int exit_status;
void ex_exit(int status) __attribute__((noreturn));
void hup_exit(void) __attribute__((noreturn));
int file_write(int a, int b, const char *path, int append, int whole, int force);
void tag_goto(const char *tag, int force);
void cmd_visual(int line, int type, int count);
void shift_lines(int a, int b, int dir, int times);
void shift_line(int i, int amount);
void make_indent(Str *b, int width);
void buf_move(int a1, int a2, int dest);
void chars_init(void);
int term_capable_standout(void);
void join_lines(int a, int b, int bang);
int do_subst_cmd(int a, int b, const char *arg, int kind, int *err_nomatch);
void put_lines(int after, int c, int vi_mode);

/* ---------- exfile.c ---------- */
int read_file_into(const char *path, int after, long *nl, long *nb, int *incomplete, int *exists);
int run_shell(const char *cmd, int in_from, int a, int b, Str *capture, int tty_io);
char *shell_echo(const char *text);
void preserve_buffer(int mail);
void recover_list(void);
int recover_file(const char *path, int *older);
void recover_cleanup(void);
void mail_user(const char *subject, const char *body);
int filter_lines(int a, int b, const char *cmd, int replace);
void restore_stdin_position(void);
void ex_typeahead(const char *s, int n);
void ex_typeahead_to_keys(void);
int run_rc_text(const char *text);
int recover_available(const char *path);
void show_file_info(void);
extern char **c_cmds;
extern int n_c_cmds, c_cmds_done, rflag, ro_from_R, window_from_w;
extern char *pending_msg;
int term_capable_visual(void);
extern char *last_bang_cmd;
char *expand_bang(const char *cmd, int *changed);
int tag_lookup(const char *tag, char **file, char **addr);
extern char *recover_path;      /* recovery file this session recovered from */
extern char *preserve_path;     /* recovery file written by this session */

/* ---------- term.c ---------- */
extern int rows, cols;
extern int term_raw;
extern int tty_fd;
void term_get_size(void);
void term_init(void);
void term_raw_on(void);
void term_raw_off(void);
void tputs_(const char *s);
void tputn(const char *s, size_t n);
int erase_char(void);
int kill_char(void);
int werase_char(void);
int eof_char(void);
void key_push_buffer(const char *s, int n);
void term_install_tstp(void);
void term_remove_tstp(void);
void tflush(void);
void tmove(int r, int c);
void tclear_eol(void);
void tclear(void);
void tstandout(int on);
#define K_INTR   (-2)
#define K_EOF    (-3)
#define K_RESIZE (-4)
#define K_UP     (-10)
#define K_DOWN   (-11)
#define K_LEFT   (-12)
#define K_RIGHT  (-13)
#define KF_MAPPED 0x100000   /* flag bits stored with queued keys */
int key_get(int map_mode, int *from_map);  /* map_mode: 0 none, 1 command, 2 input */
void key_push(const char *s, int n, int flags, int front);
int key_pending(void);
int key_pending_mapped(void);
void key_flush_mapped(void);
int key_raw(void);
int key_peek_timeout(int ms);
typedef struct Map {
    char *lhs, *rhs;
    int input;   /* text input mode map (map!) */
    struct Map *next;
} Map;
extern Map *maps;
extern Map *abbrevs;
void cmd_map(const char *arg, int bang, int unmap);
void cmd_abbrev(const char *arg, int unab);

/* ---------- screen.c / vi.c ---------- */
void vi_main(int line, int type, int count);
extern int vi_active;
void vs_ex_output(const char *line, size_t n);  /* ex output while in visual */
void vs_message(const char *text, int error);
char *vs_getline(const char *prompt, int textmode);
void vs_redraw_all(void);
void vs_suspend(void);
void vs_resume(void);
extern int vs_ex_lines;
extern int vs_wait_needed;  /* lines of ex output written over the screen */
void vs_ex_begin(void);
void vs_ex_end(int wait_key);
int vs_confirm(const char *line1, const char *line2);
extern int top_line;
extern int vs_text_rows;

#endif
