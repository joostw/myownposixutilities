/*
 * mailx.h - declarations for mailx (POSIX.1-2024, XCU mailx).
 */
#ifndef MAILX_H
#define MAILX_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <iconv.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <wordexp.h>

/* ---------- util.c ---------- */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
void err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   /* diagnostic to stderr */
extern int exit_status;

typedef struct Buf {
    char *s;
    size_t len, cap;
} Buf;
void buf_add(Buf *b, const char *p, size_t n);
void buf_adds(Buf *b, const char *s);
void buf_addc(Buf *b, int c);
void buf_fmt(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
char *buf_take(Buf *b);
void buf_free(Buf *b);

typedef struct Words {
    char **v;
    int n, cap;
} Words;
void words_add(Words *w, const char *s);
void words_free(Words *w);
int words_has(Words *w, const char *s, int icase);

/* command-line argument splitting with quoting (Commands in mailx) */
int split_args(const char *s, Words *w);     /* returns -1 on unbalanced quotes */
char *expand_filename(const char *name);     /* folder, %, #, &, shell word expansion; NULL on error */
const char *home_dir(void);
const char *login_name(void);
char *system_mailbox(const char *user);
char *mbox_path(void);
char *dead_path(void);
char *folder_dir(void);                      /* NULL if folder is not set */

/* running commands */
int run_shell_cmd(const char *cmd, int in_fd, int out_fd);   /* SHELL -c -- cmd; returns wait status */
int run_interactive(const char *prog, const char *arg);      /* editor, shell */
FILE *open_pager(int lines, pid_t *pid);                       /* stdout or a pipe to PAGER */
void close_pager(FILE *fp, pid_t pid);
char *make_temp(int *fd);
int is_tty_in(void);
int is_tty_out(void);
int screen_rows(void);

/* addresses */
void split_addresses(const char *s, Words *out);    /* a header value or user list */
char *addr_spec(const char *a);                       /* the address part of "Name <a@b>" */
char *addr_login(const char *a);                      /* local part, network addressing removed */
int addr_is_me(const char *a);
char *decode_header(const char *s);                   /* RFC 2047 encoded words */

/* ---------- var.c: internal variables, aliases, header lists ---------- */
const char *var_get(const char *name);       /* NULL if unset */
int var_isset(const char *name);
void var_set(const char *name, const char *value);
void var_unset(const char *name);
void var_list(FILE *fp);
int var_num(const char *name, int dflt);
void alias_add(const char *name, Words *addrs);
Words *alias_get(const char *name);
void alias_remove(const char *name);
void alias_list(FILE *fp, const char *name);
extern Words alternates, ignored, retained;
int header_shown(const char *field);          /* after discard/ignore/retain */
int escape_char(void);                         /* -1 if escaping is disabled */

/* ---------- msg.c: mailboxes and messages ---------- */
enum { M_NEW, M_UNREAD, M_READ, M_DELETED, M_PRESERVED, M_SAVED };
typedef struct Msg {
    size_t off, hdr_end, end;  /* byte offsets in the mailbox text: From line, start of body, end */
    int state;
    int prev_state;            /* before deletion */
    int was_read;              /* read when the mailbox was opened */
    int to_mbox;               /* mbox command */
    int touched;               /* touch command */
    int lines;
    int modified;
} Msg;

typedef struct Mailbox {
    char *path;
    char *text;
    size_t len;
    Msg *m;
    int n;
    int is_system;             /* the system mailbox (moves to mbox on quit) */
    int readonly;
    struct timespec mtime;
    off_t size;
} Mailbox;
extern Mailbox mb;
extern int cur;                /* current message, 1-based; 0 if none */
extern int cur_shown;          /* the current message has been written since it became current */
extern char *prev_file;

int mailbox_open(const char *path, int is_system, int quiet_missing);   /* 0 ok */
int mailbox_close(int do_quit);            /* quit processing; 0 ok */
char *msg_header(Msg *m, const char *field);  /* unfolded value or NULL */
char *msg_from_line(Msg *m);
char *msg_sender(Msg *m);                  /* address shown in summaries */
void msg_write(Msg *m, FILE *fp, int mode);    /* mode: see below */
enum { W_SHOW, W_ALL, W_SAVE, W_BODY, W_FORWARD, W_FORWARD_ALL };
void msg_write_lines(Msg *m, FILE *fp, int mode, const char *prefix, int maxlines);
void summary_line(FILE *fp, int i);
void mark_read(int i);
/* msglist: fills sel (n+1 flags); returns count, or -1 on error */
int parse_msglist(Words *w, int from, int to, int *sel, int for_undelete, int dflt_current);
int first_selected(int *sel);
int last_selected(int *sel);
int message_mbox_append(const char *path, int *sel, int mode, int *newfile);
void set_current_after_delete(int highest);

/* ---------- send.c ---------- */
typedef struct Compose {
    Words to, cc, bcc;
    char *subject;
    Buf body;
    Buf extra;                 /* extra header lines (In-Reply-To, References) */
    char *record;              /* where to record the message */
    Words noalias;             /* recipients taken from a message: no alias expansion */
} Compose;
void compose_free(Compose *c);
int collect(Compose *c, int receive_mode);   /* input mode; returns 0 ok, 1 aborted */
int send_message(Compose *c);                /* 0 ok */
void save_dead(Compose *c);
int compose_and_send(Compose *c, int receive_mode);
extern int opt_E, opt_F;
extern int send_mode;
void expand_aliases(Words *in, Words *out);
char *record_name_for(const char *addr);

/* ---------- cmd.c ---------- */
int execute(const char *line, int interactive);   /* 0 ok, 1 error, 2 quit, 3 exit */
int source_file(const char *path, int startup);
void command_loop(void);
extern int in_startup;
extern sigjmp_buf cmd_jmp;
extern volatile sig_atomic_t cmd_jmp_ok;
extern volatile sig_atomic_t got_sigint;
int line_read(Buf *b);
char *bang_expand(const char *cmd);   /* the bang variable */       /* a line from standard input: 1 line, 0 EOF, -1 interrupted */
void headers_page(int msg);

#endif
