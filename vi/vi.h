/*
 * vi.h - declarations shared by the visual mode modules.
 */
#ifndef VI_H
#define VI_H

typedef struct Lay {
    int row, x, w;
} Lay;

typedef struct Pos {
    int l, o;
} Pos;

/* screen.c */
int layout_line(int lnum, Lay *lay, char ***out);
int line_rows(int lnum);
void vs_refresh(void);
void vs_center(int l);
void vs_bottom(int l);
int vs_last_line_from(int t);
void vs_clear_message(void);
void vs_wait_key(int allow_colon, int *colon);
void vs_text_rows_update(void);
int prefix_width(void);
extern int bot_line;
extern int input_mode_active;
extern const char *mode_indicator;
extern int vs_wait_needed;
extern int status_sticky;
extern int cursor_row, cursor_col;
/* display overlay of the current line during text input */
extern const char *ovl_text;
extern int ovl_len, ovl_line;

/* vinput.c */
enum { IN_INSERT, IN_APPEND, IN_REPLACE, IN_CHANGE, IN_OPEN };
typedef struct InputSpec {
    int kind;          /* IN_* */
    int count;
    int ai_line;       /* the line the autoindent is derived from (0: none) */
    int ai_first;      /* insert autoindent on the first line */
    int linecmd;       /* the text input is part of a line-oriented command (cc, S, o, O) */
    const char *erase; /* initial erase-columns (c within a line) */
    int erase_len;
    const char *replay;  /* repeat the given text literally */
    int open_below;    /* IN_OPEN: o (1) or O (0) */
} InputSpec;
void vi_input(InputSpec *sp, Str *record);
extern Str last_insert;          /* the most recently input text */

/* vi.c */
void vi_beep(void);
void verr(const char *m) __attribute__((noreturn));
int vi_count_lines(void);

#endif
