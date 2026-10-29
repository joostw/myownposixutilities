/*
 * cmd.h - ex command structures (internal to the ex command modules).
 */
#ifndef CMD_H
#define CMD_H

typedef struct Ctx {
    const char *p;        /* rest of the text being executed */
    Source *src;          /* where further input lines come from (or NULL) */
    int prev_bar;         /* previous command ended with '|' */
    int first;
    char *first_text;     /* a|text */
} Ctx;

struct Cmd;
typedef struct CmdDef {
    const char *name, *abbr;
    void (*fn)(struct Cmd *);
    int addrs;            /* 0, 1 or 2 */
    unsigned flags;
} CmdDef;

/* CmdDef flags */
#define AZERO     0x0001   /* address 0 is valid */
#define AEMPTY    0x0002   /* 1addr: valid (as 0) in an empty buffer */
#define AEMPTY2   0x0004   /* 2addr: valid (0,0) in an empty buffer */
#define ADEFALL   0x0008   /* default range is the whole file */
#define A1DEFLAST 0x0010   /* 1addr default is $ */
#define AOPT      0x0020
#define BANG      0x0040
#define NOAP      0x0080   /* never autoprint */
#define ARG_MASK  0x0f00
#define ARG_NORMAL 0x0000
#define ARG_TEXT  0x0100
#define ARG_BANG  0x0200
#define ARG_READ  0x0300
#define ARG_WRITE 0x0400
#define ARG_EDIT  0x0500
#define ARG_DELIM 0x0600
#define ARG_MAP   0x0700
#define ARG_VISUAL 0x0800

typedef struct Cmd {
    const CmdDef *def;
    char name[32];
    int naddr, a1, a2;
    int bang;
    int shell;            /* r !cmd, w !cmd */
    char *arg;
    int term_bar;
    Ctx *ctx;
    int flags, flagoff;   /* print flags and +/- offset */
    int no_flag_print;
    int no_autoprint;
} Cmd;

const char *skipbl(const char *p);
int parse_count(const char **pp, int *count);
int parse_flags(const char **pp, int *flags, int *off);
void end_args(const char *p);
void apply_count(Cmd *c, int count);
char *ctx_text_line(Ctx *ctx, int lnum, const char *indent);
extern int last_line_printed;
extern long buffer_generation;
void hup_exit(void) __attribute__((noreturn));
int autowrite_check(int force);

void c_abbrev(Cmd *), c_append(Cmd *), c_args(Cmd *), c_change(Cmd *), c_cd(Cmd *);
void c_copy(Cmd *), c_delete(Cmd *), c_edit(Cmd *), c_file(Cmd *), c_global(Cmd *);
void c_insert(Cmd *), c_join(Cmd *), c_print(Cmd *), c_map(Cmd *), c_mark(Cmd *);
void c_move(Cmd *), c_next(Cmd *), c_open(Cmd *), c_preserve(Cmd *), c_put(Cmd *);
void c_quit(Cmd *), c_read(Cmd *), c_recover(Cmd *), c_rewind(Cmd *), c_set(Cmd *);
void c_shell(Cmd *), c_source(Cmd *), c_subst(Cmd *), c_subst_amp(Cmd *);
void c_subst_tilde(Cmd *), c_suspend(Cmd *), c_tag(Cmd *), c_unabbrev(Cmd *);
void c_undo(Cmd *), c_unmap(Cmd *), c_version(Cmd *), c_visual(Cmd *), c_write(Cmd *);
void c_xit(Cmd *), c_yank(Cmd *), c_z(Cmd *), c_bang(Cmd *), c_shift(Cmd *);
void c_ctrld(Cmd *), c_equal(Cmd *), c_exec(Cmd *);

#endif
