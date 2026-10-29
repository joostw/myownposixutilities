/*
 * ed.h - declarations for ed (POSIX.1-2024, XCU ed).
 */
#ifndef ED_H
#define ED_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

/* ---------- util.c ---------- */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrndup(const char *s, size_t n);

typedef struct Buf {
    char *s;
    size_t len, cap;
} Buf;
void buf_add(Buf *b, const char *p, size_t n);
void buf_adds(Buf *b, const char *s);
void buf_addc(Buf *b, int c);
void buf_clear(Buf *b);

/* errors: set the message and return -1 */
extern const char *errmsg;
int fail(const char *msg);
extern int help_mode;
extern int had_error;
void report_error(void);         /* "?" and the message in help mode */

/* ---------- the buffer (buf.c) ---------- */
typedef struct Line {
    char *s;
    size_t len;
    int gmark;                   /* marked by g/v/G/V */
} Line;

extern Line **lines;            /* lines[1..nlines] */
extern long nlines, cur;
extern int modified;            /* 0 unchanged, 1 changed, 2 changed-and-warned */
extern Line *marks[26];
extern char *filename;

Line *line_new(const char *s, size_t len);
void buf_insert(long after, Line **v, long n);   /* insert lines after line 'after' */
void buf_delete(long from, long to);
void buf_replace(long i, Line *l);
long line_index(Line *l);                         /* 0 if not in the buffer */
void undo_begin(void);                            /* before a command that may modify */
void undo_end(void);                              /* after it */
void undo_end_global(void);
int undo(void);
void undo_forget(void);
void changed(void);                                /* the buffer was modified */
void buf_clear_all(void);

/* reading and writing files and commands; return bytes or -1 */
long read_file(const char *name, long after, int is_cmd);
long write_file(const char *name, const char *mode, long from, long to, int is_cmd);

/* ---------- regular expressions and substitution (re.c) ---------- */
/* parse a delimited RE starting at *pp (just after the delimiter); compiles it
   or uses the previous one; on return *pp is after the closing delimiter
   (or at the end when it was omitted, and *closed is 0) */
regex_t *get_re(const char **pp, int delim, int *closed);
int re_match(regex_t *re, Line *l, regmatch_t *m, size_t nm, int notbol, size_t start);
int parse_replacement(const char **pp, int delim, int *closed, int *split);  /* 0 ok */
int substitute(long from, long to, regex_t *re, int gflag, long count, int in_global);
extern regex_t *last_re;

/* ---------- output (util.c) ---------- */
enum { PF_P = 1, PF_L = 2, PF_N = 4 };
void print_lines(long from, long to, int flags);
void out_flush(void);

/* ---------- input (util.c) ---------- */
typedef struct Reader {
    int (*next)(struct Reader *r, Buf *line);   /* 1 line, 0 EOF, -1 interrupted */
    char **v;                                    /* lines of a command list */
    int n, i;
} Reader;
extern Reader stdin_reader;
void flush_input(void);
int read_text(Reader *r, Buf *text_lines_out, Line ***v, long *n);  /* input mode */

/* ---------- shell (util.c) ---------- */
int run_shell(const char *cmd);
extern char *last_shell;

/* ---------- main.c ---------- */
extern int opt_s;
extern char *prompt;
extern int prompting;
extern sigjmp_buf jmp;
extern volatile sig_atomic_t jmp_ok, interrupted;
int exec_command(const char **pp, Reader *r, int in_global);

#endif
