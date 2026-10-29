/*
 * more.c - more: display files on a page-by-page basis
 *   (POSIX.1-2024, XCU more)
 *
 * Synopsis: more [-ceisu] [-n number] [-p command] [-t tagstring] [file...]
 *
 * If standard output is not a terminal, the files are copied to it as
 * they are (with -s, runs of empty lines become one), and nothing else.
 *
 * On a terminal:
 *   - input is read as far as the display needs it, so a pipe that never
 *     ends (yes | more) is paged as it arrives; what was read is kept, so
 *     backward motion works on pipes too.
 *   - commands are read from standard error (from /dev/tty if standard
 *     error cannot be read); the prompt is written there too.
 *   - lines are folded at the width of the display, wide characters are
 *     not split, tabs go to multiples of 8, overstriking (backspaces) makes
 *     bold and underlined text; other control characters are written as
 *     ^X, and bytes that are not characters (or not printable) as \ooo.
 *   - forward motion that keeps lines on the screen scrolls the terminal,
 *     and so does backward motion (with reverse index), when the screen is
 *     the whole terminal; otherwise the screen is redrawn.  -c redraws
 *     from the top, line by line.
 *   - "the current position" is the top line of the screen (for m, ' and
 *     '' as well).
 *   - at the end of a file, f, ^F, <space>, j, <newline>, d, ^D and s go
 *     to the next file; at the end of the last file any other command
 *     exits, as the standard says.
 *   - SIGINT, SIGQUIT, SIGTERM and SIGHUP keep their default action, but
 *     the terminal modes are restored first; SIGTSTP likewise, and on
 *     SIGCONT the screen is refreshed.
 *   - the terminal is driven with ANSI (ECMA-48) sequences.
 *
 * The tags of -t and :t are looked up in the file "tags" (ctags format).
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include <wordexp.h>

#undef CTRL
#define CTRL(x) ((x) & 0x1f)

/* ---- options ---- */

static bool  opt_c, opt_e, opt_i, opt_s, opt_u;
static int   opt_n;             /* lines per screenful (0: not given) */
static char *opt_p, *opt_t;

static int exit_status;

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        perror("more");
        exit(1);
    }
    return p;
}
static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) {
        perror("more");
        exit(1);
    }
    return p;
}
static char *xstrdup(const char *s)
{
    char *t = strdup(s);
    if (!t) {
        perror("more");
        exit(1);
    }
    return t;
}

/* ====================================================================== */
/*  Filter mode: standard output is not a terminal                        */
/* ====================================================================== */

static void filter_file(const char *name, FILE *fp)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    bool prev_blank = false;
    while ((n = getline(&line, &cap, fp)) >= 0) {
        bool blank = n == 1 && line[0] == '\n';
        if (opt_s && blank && prev_blank)
            continue;
        prev_blank = blank;
        if (fwrite(line, 1, (size_t)n, stdout) != (size_t)n) {
            perror("more: write error");
            exit(1);
        }
    }
    if (ferror(fp)) {
        fprintf(stderr, "more: %s: %s\n", name, strerror(errno));
        exit_status = 1;
    }
    free(line);
}

/* ====================================================================== */
/*  The terminal                                                          */
/* ====================================================================== */

static FILE *out;               /* the file contents: standard output */
static FILE *ctl;               /* prompts and messages */
static int   ctl_fd = -1;       /* where commands are read from */
static struct termios orig_term;
static bool  have_term, raw_on;

static int scr_rows, scr_cols;  /* the screen: prompt on row scr_rows */
static int page_lines;          /* lines of text per screen: scr_rows - 1 */
static int term_rows;           /* the terminal's own size (0: unknown) */

static volatile sig_atomic_t got_winch, got_cont;

static void set_raw(bool raw)
{
    if (!have_term)
        return;
    struct termios t = orig_term;
    if (raw) {
        t.c_lflag &= ~(tcflag_t)(ECHO | ICANON);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
    }
    tcsetattr(ctl_fd, TCSADRAIN, &t);
    raw_on = raw;
}

static void restore_term(void)
{
    if (have_term && raw_on) {
        tcsetattr(ctl_fd, TCSADRAIN, &orig_term);
        raw_on = false;
    }
}

static int env_number(const char *name)
{
    const char *v = getenv(name);
    if (!v || !*v)
        return 0;
    long n = 0;
    for (const char *p = v; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        n = n * 10 + (*p - '0');
        if (n > 100000)
            return 0;
    }
    return (int)n;
}

static void update_size(void)
{
    int rows = 0, cols = 0;
    struct winsize ws;
    term_rows = 0;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        rows = ws.ws_row;
        cols = ws.ws_col;
        term_rows = ws.ws_row;
    }
    int c = env_number("COLUMNS"), l = env_number("LINES");
    if (c > 0)
        cols = c;
    if (l > 0)
        rows = l;
    if (opt_n > 0)
        rows = opt_n;           /* -n (also from MORE) takes precedence */
    scr_rows = rows >= 2 ? rows : 24;
    scr_cols = cols >= 1 ? cols : 80;
    page_lines = scr_rows - 1;
}

/* ---- signals ---- */

static void on_winch(int sig) { (void)sig; got_winch = 1; }

static void on_fatal(int sig)
{
    if (have_term && raw_on)
        tcsetattr(ctl_fd, TCSANOW, &orig_term);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void on_tstp(int sig)
{
    int e = errno;
    if (have_term && raw_on)
        tcsetattr(ctl_fd, TCSANOW, &orig_term);
    signal(SIGTSTP, SIG_DFL);
    raise(SIGTSTP);
    /* continued */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_tstp;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTSTP, &sa, NULL);
    if (have_term && raw_on) {
        struct termios t = orig_term;
        t.c_lflag &= ~(tcflag_t)(ECHO | ICANON);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        tcsetattr(ctl_fd, TCSANOW, &t);
    }
    got_cont = 1;
    (void)sig;
    errno = e;
}

static void on_cont(int sig) { (void)sig; got_cont = 1; }

static void setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;            /* no SA_RESTART: a read is interrupted */
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = on_cont;
    sigaction(SIGCONT, &sa, NULL);
    sa.sa_handler = on_tstp;
    sigaction(SIGTSTP, &sa, NULL);
    sa.sa_handler = on_fatal;
    int fatal[] = { SIGINT, SIGQUIT, SIGTERM, SIGHUP };
    for (size_t i = 0; i < sizeof fatal / sizeof *fatal; i++) {
        struct sigaction old;
        sigaction(fatal[i], NULL, &old);
        if (old.sa_handler != SIG_IGN)
            sigaction(fatal[i], &sa, NULL);
    }
}

/* ====================================================================== */
/*  The file being examined: lines read as they are needed                */
/* ====================================================================== */

typedef struct {
    char *text;
    size_t len;
    long long off;              /* byte offset of the line in the file */
} Line;

static Line *lines;
static int   nlines, lines_cap;
static FILE *src;
static bool  src_eof, src_is_stdin, src_prev_blank;
static long long src_off, src_size;
static char *rd_buf;
static size_t rd_cap;

static void close_source(void)
{
    if (src && !src_is_stdin)
        fclose(src);
    src = NULL;
    for (int i = 0; i < nlines; i++)
        free(lines[i].text);
    nlines = 0;
}

/* open a file to examine; -1 (with a message written) if it cannot be */
static int open_source(const char *path, char *err, size_t errlen)
{
    FILE *fp;
    bool is_stdin = strcmp(path, "-") == 0;
    if (is_stdin) {
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp) {
            snprintf(err, errlen, "%s: %s", path, strerror(errno));
            return -1;
        }
        struct stat st;
        if (fstat(fileno(fp), &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(err, errlen, "%s: is a directory", path);
            fclose(fp);
            return -1;
        }
    }
    close_source();
    src = fp;
    src_is_stdin = is_stdin;
    src_eof = false;
    src_prev_blank = false;
    src_off = 0;
    src_size = -1;
    struct stat st;
    if (fstat(fileno(fp), &st) == 0 && S_ISREG(st.st_mode))
        src_size = (long long)st.st_size;
    return 0;
}

/* read until there are n lines (or the end); true if there are */
static bool ensure(int n)
{
    while (nlines < n && !src_eof) {
        ssize_t r = getline(&rd_buf, &rd_cap, src);
        if (r < 0) {
            src_eof = true;
            if (ferror(src))
                exit_status = 1;
            clearerr(src);
            break;
        }
        long long off = src_off;
        src_off += r;
        size_t len = (size_t)r;
        if (len && rd_buf[len - 1] == '\n')
            len--;
        if (!opt_u && len && rd_buf[len - 1] == '\r')
            len--;              /* a <carriage-return> at the end is ignored */
        bool blank = len == 0;
        if (opt_s && blank && src_prev_blank)
            continue;
        src_prev_blank = blank;
        if (nlines == lines_cap) {
            lines_cap = lines_cap ? lines_cap * 2 : 1024;
            lines = xrealloc(lines, (size_t)lines_cap * sizeof *lines);
        }
        lines[nlines].text = xmalloc(len + 1);
        memcpy(lines[nlines].text, rd_buf, len);
        lines[nlines].text[len] = 0;
        lines[nlines].len = len;
        lines[nlines].off = off;
        nlines++;
    }
    return nlines >= n;
}

/* ====================================================================== */
/*  Rendering a line into display rows                                    */
/* ====================================================================== */

enum { K_PRINT, K_CTRL, K_BAD, K_TAB, K_BS };
enum { A_UNDER = 1, A_BOLD = 2 };

typedef struct {
    char b[24];                 /* the bytes written for it */
    unsigned char n;
    signed char w;              /* columns (a tab: computed when folding) */
    unsigned char kind, attr;
    wint_t wc;                  /* for comparing characters */
} Cell;

typedef struct { char *s; size_t len, cap; } Str;

static void str_add(Str *s, const char *p, size_t n)
{
    if (s->len + n + 1 > s->cap) {
        s->cap = (s->len + n + 1) * 2;
        s->s = xrealloc(s->s, s->cap);
    }
    memcpy(s->s + s->len, p, n);
    s->len += n;
    s->s[s->len] = 0;
}
static void str_adds(Str *s, const char *p) { str_add(s, p, strlen(p)); }

typedef struct { Str *row; int n, cap; } Rows;

static void rows_clear(Rows *r)
{
    for (int i = 0; i < r->n; i++)
        r->row[i].len = 0;
    r->n = 0;
}
static Str *rows_new(Rows *r)
{
    if (r->n == r->cap) {
        int nc = r->cap ? r->cap * 2 : 8;
        r->row = xrealloc(r->row, (size_t)nc * sizeof *r->row);
        memset(r->row + r->cap, 0, (size_t)(nc - r->cap) * sizeof *r->row);
        r->cap = nc;
    }
    Str *s = &r->row[r->n++];
    s->len = 0;
    if (s->s)
        s->s[0] = 0;
    else
        str_add(s, "", 0);
    return s;
}

/* the characters of a line, as cells */
static Cell *decode_line(const char *t, size_t len, size_t *ncell)
{
    Cell *c = xmalloc((len + 1) * sizeof *c);
    size_t n = 0;
    mbstate_t st;
    memset(&st, 0, sizeof st);
    for (size_t i = 0; i < len; ) {
        Cell *x = &c[n];
        memset(x, 0, sizeof *x);
        wchar_t w;
        size_t k = mbrtowc(&w, t + i, len - i, &st);
        if (k == (size_t)-1 || k == (size_t)-2 || k == 0) {
            memset(&st, 0, sizeof st);
            if (k == 0) {
                x->kind = K_CTRL;   /* a NUL */
                x->wc = 0;
                memcpy(x->b, "^@", 2);
                x->n = 2;
                x->w = 2;
            } else {
                x->kind = K_BAD;
                x->wc = WEOF;
                x->n = (unsigned char)snprintf(x->b, sizeof x->b, "\\%03o", (unsigned char)t[i]);
                x->w = 4;
            }
            i++;
            n++;
            continue;
        }
        x->wc = (wint_t)w;
        if (w == L'\t') {
            x->kind = K_TAB;
        } else if (w == L'\b' && !opt_u) {
            x->kind = K_BS;
        } else if (w < 0x20 || w == 0x7f) {
            x->kind = K_CTRL;
            x->b[0] = '^';
            x->b[1] = w == 0x7f ? '?' : (char)('@' + w);
            x->n = 2;
            x->w = 2;
        } else if (!iswprint((wint_t)w)) {
            x->kind = K_BAD;
            for (size_t j = 0; j < k && (size_t)x->n + 4 < sizeof x->b; j++)
                x->n += (unsigned char)snprintf(x->b + x->n, sizeof x->b - x->n, "\\%03o",
                                                (unsigned char)t[i + j]);
            x->w = (signed char)x->n;
        } else {
            int cw = wcwidth(w);
            if (cw == 0 && n > 0 && c[n - 1].kind == K_PRINT &&
                c[n - 1].n + k < sizeof c[n - 1].b) {
                /* a combining character: part of the one before */
                memcpy(c[n - 1].b + c[n - 1].n, t + i, k);
                c[n - 1].n += (unsigned char)k;
                i += k;
                continue;
            }
            x->kind = K_PRINT;
            memcpy(x->b, t + i, k);
            x->n = (unsigned char)k;
            x->w = (signed char)(cw > 0 ? cw : 1);
        }
        i += k;
        n++;
    }
    *ncell = n;
    return c;
}

static bool is_us(const Cell *c) { return c->kind == K_PRINT && c->wc == L'_'; }

/*
 * Overstriking: a character, backspaces over it and as many underscores
 * (or the other way round) is underlined; a character, backspaces and the
 * same character is bold; other backspaces discard the character before.
 */
static size_t overstrike(Cell *in, size_t n)
{
    size_t high = 0, pos = 0;
    int bs = 0, absorb = 0;
    for (size_t i = 0; i < n; i++) {
        Cell ch = in[i];
        if (ch.kind == K_BS) {
            bs++;
            continue;
        }
        if (bs) {
            while (bs > 0 && pos > 0) {
                int w = in[pos - 1].kind == K_TAB ? 1 : in[pos - 1].w;
                pos--;
                bs -= w;
            }
            bs = 0;
            absorb = 0;
        }
        if (pos < high) {
            Cell *old = &in[pos];
            if (is_us(&ch) && !is_us(old)) {
                old->attr |= A_UNDER;
                absorb = old->w - 1;
            } else if (is_us(old) && !is_us(&ch)) {
                unsigned char a = old->attr;
                *old = ch;
                old->attr = a | A_UNDER;
                /* the further underscores under a wide character */
                int extra = ch.w - 1;
                while (extra-- > 0 && pos + 1 < high && is_us(&in[pos + 1])) {
                    memmove(&in[pos + 1], &in[pos + 2], (high - pos - 2) * sizeof *in);
                    high--;
                }
            } else if (old->kind == ch.kind && old->wc == ch.wc && old->n == ch.n &&
                       memcmp(old->b, ch.b, ch.n) == 0) {
                old->attr |= A_BOLD;
            } else {
                *old = ch;
            }
            pos++;
            continue;
        }
        if (absorb > 0 && is_us(&ch)) {
            absorb--;
            continue;
        }
        absorb = 0;
        in[high++] = ch;        /* high <= i, so this never overtakes the input */
        pos = high;
    }
    return high;
}

static void attr_seq(Str *s, unsigned char from, unsigned char to)
{
    if (from == to)
        return;
    str_adds(s, "\033[0m");
    if (to & A_BOLD)
        str_adds(s, "\033[1m");
    if (to & A_UNDER)
        str_adds(s, "\033[4m");
}

/* the display rows of line idx */
static void render(int idx, Rows *R)
{
    size_t n;
    Cell *c = decode_line(lines[idx].text, lines[idx].len, &n);
    if (!opt_u)
        n = overstrike(c, n);
    else
        for (size_t i = 0; i < n; i++)
            if (c[i].kind == K_BS) {
                c[i].kind = K_CTRL;
                memcpy(c[i].b, "^H", 2);
                c[i].n = 2;
                c[i].w = 2;
            }
    rows_clear(R);
    Str *row = rows_new(R);
    int col = 0;
    unsigned char attr = 0;
    for (size_t i = 0; i < n; i++) {
        int w = c[i].kind == K_TAB ? 8 - col % 8 : c[i].w;
        if (c[i].kind == K_TAB && col + w > scr_cols)
            w = scr_cols - col;
        if (col + w > scr_cols && col > 0) {
            attr_seq(row, attr, 0);
            attr = 0;
            row = rows_new(R);
            col = 0;
            if (c[i].kind == K_TAB)
                w = 8;
            if (w > scr_cols)
                w = scr_cols;
        }
        attr_seq(row, attr, c[i].attr);
        attr = c[i].attr;
        if (c[i].kind == K_TAB) {
            for (int j = 0; j < w; j++)
                str_add(row, " ", 1);
        } else {
            str_add(row, c[i].b, c[i].n);
        }
        col += w;
    }
    attr_seq(row, attr, 0);
    free(c);
}

static Rows rbuf;

static int line_rows(int idx)
{
    render(idx, &rbuf);
    return rbuf.n;
}

/* ====================================================================== */
/*  The screen                                                            */
/* ====================================================================== */

static int  top;                /* the first line on the screen */
static int  bot;                /* the last line on the screen (maybe in part) */
static int  bot_shown, bot_total;   /* rows of bot on the screen, and in all */
static bool screen_full;        /* page_lines rows of text are on the screen */
static bool first_screen = true;    /* nothing written by more yet */
static bool suppress;           /* -p: commands run without display */
static int  prompt_row_known;   /* the cursor is on the prompt row */

static void out_flush(void)
{
    if (fflush(out) == EOF) {
        restore_term();
        exit(1);
    }
}

/* where the screen from line t ends: sets bot, bot_shown, bot_total, screen_full */
static void layout(int t)
{
    int left = page_lines;
    bot = t;
    bot_shown = bot_total = 0;
    screen_full = false;
    for (int i = t; left > 0 && ensure(i + 1); i++) {
        int r = line_rows(i);
        bot = i;
        bot_total = r;
        bot_shown = r < left ? r : left;
        left -= bot_shown;
    }
    screen_full = left == 0;
}

static void emit_row(const Str *s)
{
    fputs("\033[K", out);
    fwrite(s->s, 1, s->len, out);
    fputs("\n", out);
}

/* draw the screen from line t: from the top (cleared, or -c line by line) */
static void redraw(int t)
{
    top = t;
    if (suppress) {
        layout(t);
        return;
    }
    if (first_screen && !opt_c) {
        /* the first screen is written where the cursor is */
        fputs("\r", out);
    } else if (opt_c && !first_screen) {
        fputs("\033[H", out);
    } else {
        fputs("\033[H\033[2J", out);
    }
    first_screen = false;
    int left = page_lines;
    bot = t;
    bot_shown = bot_total = 0;
    for (int i = t; left > 0 && ensure(i + 1); i++) {
        render(i, &rbuf);
        int k = rbuf.n < left ? rbuf.n : left;
        for (int r = 0; r < k; r++)
            emit_row(&rbuf.row[r]);
        bot = i;
        bot_total = rbuf.n;
        bot_shown = k;
        left -= k;
    }
    screen_full = left == 0;
    if (opt_c)
        fputs("\033[J", out);
    prompt_row_known = 1;
    out_flush();
}

/* scrolling works when the screen is all of the terminal */
static bool can_scroll(void)
{
    return !opt_c && !suppress && screen_full && term_rows == scr_rows && !first_screen;
}

/*
 * Forward to line t, writing at the bottom the rows that follow the
 * screen, so that the terminal scrolls.  false if it cannot.
 */
static bool scroll_fwd_to(int t)
{
    if (!can_scroll() || t <= top)
        return false;
    int need = 0;
    for (int i = top; i < t; i++)
        need += line_rows(i);
    /* the rows there are after the screen */
    int avail = bot_total - bot_shown;
    for (int i = bot + 1; avail < need && ensure(i + 1); i++)
        avail += line_rows(i);
    if (avail < need)
        return false;
    fputs("\r\033[K", out);
    int done = 0;
    if (bot_shown < bot_total) {
        render(bot, &rbuf);
        for (int r = bot_shown; r < bot_total && done < need; r++, done++)
            emit_row(&rbuf.row[r]);
    }
    for (int i = bot + 1; done < need; i++) {
        render(i, &rbuf);
        for (int r = 0; r < rbuf.n && done < need; r++, done++)
            emit_row(&rbuf.row[r]);
    }
    top = t;
    layout(t);
    out_flush();
    return true;
}

/* backward to line t with reverse index at the top; false if it cannot */
static bool scroll_bwd_to(int t, bool all)
{
    if (!can_scroll() || t >= top)
        return false;
    int need = 0;
    for (int i = t; i < top; i++)
        need += line_rows(i);
    if (!all && need >= page_lines)
        return false;
    for (int i = top - 1; i >= t; i--) {
        render(i, &rbuf);
        for (int r = rbuf.n - 1; r >= 0; r--) {
            fputs("\033[H\033M\033[K", out);
            fwrite(rbuf.row[r].s, 1, rbuf.row[r].len, out);
        }
    }
    top = t;
    layout(t);
    fprintf(out, "\033[%d;1H\033[K", scr_rows);
    out_flush();
    return true;
}

/*
 * Show the screen from line t.  Forward motion scrolls when the new screen
 * has lines in common with this one, or always with fwd_all (<space>, j,
 * <newline>, d: "the entire count lines shall be written"); backward
 * likewise with bwd_all (k, u).  Otherwise the screen is redrawn (f and b
 * with a count over a screenful: "only the final screenful").
 */
static void show_at(int t, bool fwd_all, bool bwd_all)
{
    if (t < 0)
        t = 0;
    if (t > top) {
        if (!((fwd_all || t <= bot) && scroll_fwd_to(t)))
            redraw(t);
    } else if (t < top) {
        if (!scroll_bwd_to(t, bwd_all))
            redraw(t);
    } else {
        redraw(t);
    }
}

static bool at_eof(void)
{
    if (nlines == 0) {
        ensure(1);
        return nlines == 0;
    }
    return bot_shown == bot_total && !ensure(bot + 2);
}

/* the top line of the last screenful */
static int last_top(void)
{
    ensure(INT_MAX);
    int left = page_lines, t = nlines;
    while (t > 0) {
        int r = line_rows(t - 1);
        if (r > left)
            break;
        left -= r;
        t--;
    }
    if (t == nlines && nlines > 0)
        t = nlines - 1;     /* a last line taller than the screen */
    return t;
}

/* the top line for moving forward n lines from line from: at most the last screenful */
static int forward_target(int from, int n)
{
    long long want = (long long)from + n;
    int t = want > INT_MAX / 2 ? INT_MAX / 2 : (int)want;
    int rows = 0;
    for (int i = t; ensure(i + 1); i++) {
        rows += line_rows(i);
        if (rows >= page_lines)
            return t;           /* a whole screenful from t */
    }
    int lt = last_top();
    return lt > from ? lt : from;
}

/* ====================================================================== */
/*  Prompt, messages and commands                                         */
/* ====================================================================== */

static const char *cur_name;
static int cur_num, num_files;
static const char *next_name;

static void prompt_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void prompt_line(const char *fmt, ...)
{
    if (suppress)
        return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    /* within the line, so that the prompt never wraps */
    size_t keep = 0;
    int cols = 0;
    mbstate_t st;
    memset(&st, 0, sizeof st);
    while (buf[keep]) {
        wchar_t w;
        size_t k = mbrtowc(&w, buf + keep, strlen(buf + keep), &st);
        int cw = 1;
        if (k == (size_t)-1 || k == (size_t)-2 || k == 0) {
            k = 1;
            memset(&st, 0, sizeof st);
        } else if ((cw = wcwidth(w)) < 0) {
            cw = 1;
        }
        if (cols + cw > scr_cols - 1)
            break;
        cols += cw;
        keep += k;
    }
    buf[keep] = 0;
    out_flush();
    fprintf(ctl, "\r\033[K\033[7m%s\033[0m", buf);
    fflush(ctl);
}

static void show_prompt(void)
{
    char where[64] = "";
    if (num_files > 1)
        snprintf(where, sizeof where, " [%d/%d]", cur_num, num_files);
    if (at_eof()) {
        if (next_name)
            prompt_line("--More--(%s%s: END; next: %s)", cur_name, where, next_name);
        else
            prompt_line("--More--(%s%s: END)", cur_name, where);
    } else if (src_size > 0 && nlines > 0) {
        long long b = bot + 1 < nlines ? lines[bot + 1].off : src_off;
        prompt_line("--More--(%s%s %d%%)", cur_name, where, (int)(b * 100 / src_size));
    } else {
        prompt_line("--More--(%s%s)", cur_name, where);
    }
}

static void clear_prompt(void)
{
    if (suppress)
        return;
    fputs("\r\033[K", ctl);
    fflush(ctl);
}

static const char *playback;    /* -p commands still to run */
static bool pb_failed;

/* a key from the user (or from -p); 0 when the screen needs refreshing */
static int get_key(void)
{
    if (playback) {
        if (*playback)
            return (unsigned char)*playback++;
        playback = NULL;
    }
    for (;;) {
        if (got_winch || got_cont)
            return 0;
        unsigned char c;
        ssize_t n = read(ctl_fd, &c, 1);
        if (n == 1)
            return c;
        if (n < 0 && errno == EINTR)
            continue;
        return 'q';             /* the end of command input */
    }
}

/* a message: with "press a key" when it is not in the prompt */
static void message(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void message(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (playback || suppress) {
        /* -p: a command failed; no more of them for this file */
        pb_failed = true;
        playback = NULL;
        return;
    }
    prompt_line("%s  [press a key]", buf);
    get_key();
    clear_prompt();
}

static void bell(void)
{
    if (suppress)
        return;
    fputc('\a', ctl);
    fflush(ctl);
}

/* a line of input after a prompt character (from -p if it is running) */
static char *get_line(const char *lead)
{
    Str s = {0};
    str_add(&s, "", 0);
    if (playback) {
        while (*playback && *playback != '\n')
            str_add(&s, playback++, 1);
        if (*playback == '\n')
            playback++;
        return s.s;
    }
    fprintf(ctl, "\r\033[K%s", lead);
    fflush(ctl);
    set_raw(false);
    for (;;) {
        char c;
        ssize_t n = read(ctl_fd, &c, 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0 || c == '\n')
            break;
        str_add(&s, &c, 1);
    }
    set_raw(true);
    clear_prompt();
    return s.s;
}

/* ---- search ---- */

static regex_t last_re;
static bool    have_re, last_fwd = true, last_neg;

static bool set_pattern(const char *pat)
{
    regex_t re;
    int rc = regcomp(&re, pat, REG_NOSUB | (opt_i ? REG_ICASE : 0));
    if (rc != 0) {
        char e[256];
        regerror(rc, &re, e, sizeof e);
        message("%s", e);
        return false;
    }
    if (have_re)
        regfree(&last_re);
    last_re = re;
    have_re = true;
    return true;
}

static bool matches(int i, bool neg)
{
    bool m = regexec(&last_re, lines[i].text, 0, NULL, 0) == 0;
    return neg ? !m : m;
}

/* the count-th line after from (or before it) that matches; -1 if none */
static int search(int from, bool fwd, bool neg, int count)
{
    if (!have_re)
        return -1;
    int found = 0;
    if (fwd) {
        for (int i = from + 1; ensure(i + 1); i++)
            if (matches(i, neg) && ++found == count)
                return i;
    } else {
        for (int i = from - 1; i >= 0; i--)
            if (matches(i, neg) && ++found == count)
                return i;
    }
    return -1;
}

/* ---- tags ---- */

static bool find_tag(const char *tag, char **file, char **pattern, int *lineno)
{
    FILE *tf = fopen("tags", "r");
    char *line = NULL;
    size_t cap = 0;
    bool ok = false;
    *file = *pattern = NULL;
    *lineno = 0;
    if (!tf)
        return false;
    while (getline(&line, &cap, tf) >= 0) {
        char *t1 = strchr(line, '\t');
        if (!t1 || (size_t)(t1 - line) != strlen(tag) || memcmp(line, tag, strlen(tag)) != 0)
            continue;
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2)
            continue;
        *t2 = 0;
        *file = xstrdup(t1 + 1);
        char *addr = t2 + 1;
        addr[strcspn(addr, "\n")] = 0;
        if (*addr == '/' || *addr == '?') {
            char d = *addr;
            Str s = {0};
            str_add(&s, "", 0);
            for (char *q = addr + 1; *q && *q != d; q++) {
                if (*q == '\\' && q[1] == d)
                    q++;
                str_add(&s, q, 1);
            }
            *pattern = s.s;
        } else {
            *lineno = atoi(addr);
        }
        ok = true;
        break;
    }
    free(line);
    fclose(tf);
    return ok;
}

/* the line of a tag in the file being examined, or -1 */
static int tag_line(const char *pattern, int lineno)
{
    if (pattern) {
        if (!set_pattern(pattern))
            return -1;
        last_fwd = true;
        last_neg = false;
        return search(-1, true, false, 1);
    }
    if (lineno > 0 && ensure(lineno))
        return lineno - 1;
    return -1;
}

/* ====================================================================== */
/*  Paging one file                                                       */
/* ====================================================================== */

enum { R_NEXT, R_PREV, R_QUIT, R_EXAMINE, R_GOTO };

static char *pending_note;      /* a message for after the first screen */
static char *prev_examined;     /* the file examined before this one (:e #) */
static int  marks[26];
static int  prev_pos;           /* '' */
static int  half;               /* d and u */
static char *examine_name;      /* R_EXAMINE: the file */
static int   examine_tagline = -1;
static char *examine_tagpat;
static int   goto_index;        /* R_GOTO: the file */

static void move_to(int t, bool fwd_all, bool bwd_all)
{
    int dist = t > top ? t - top : top - t;
    if (dist > page_lines)
        prev_pos = top;
    show_at(t, fwd_all, bwd_all);
}

static void show_help(void)
{
    static const char *help[] = {
        "Commands (count: an optional number before the command):",
        "  h                 this summary",
        "  [count]f ^F       forward count lines (default: one screenful)",
        "  [count]b ^B       backward count lines (default: one screenful)",
        "  [count]<space>    forward count lines (default: one screenful)",
        "  [count]j <newline>  forward count lines (default: one)",
        "  [count]k          backward count lines (default: one)",
        "  [count]d ^D       forward count lines (default: half a screen)",
        "  [count]u ^U       backward count lines (default: half a screen)",
        "  [count]s          the screen count lines after the last line",
        "  [count]g          go to line count (default: the first)",
        "  [count]G          go to line count (default: the last screen)",
        "  r ^L              refresh;  R  refresh, rereading the file",
        "  m<letter>         mark the position;  '<letter>  return to it",
        "  ''                return to before the last large movement",
        "  [count]/[!]pattern  search forward (! for lines not matching)",
        "  [count]?[!]pattern  search backward",
        "  [count]n  [count]N  repeat the search, in the other direction",
        "  :e [file]         examine a file (# the previous one)",
        "  [count]:n  [count]:p  the next or previous file",
        "  :t tag            go to a tag",
        "  v                 edit the file ($EDITOR, or vi)",
        "  = ^G              the position in the file",
        "  q :q ZZ           quit",
    };
    fputs("\033[H\033[2J", out);
    for (size_t i = 0; i < sizeof help / sizeof *help; i++)
        fprintf(out, "%s\n", help[i]);
    prompt_line("--More--(help: END)");
    get_key();
    clear_prompt();
    redraw(top);
}

static void edit_file(void)
{
    if (src_is_stdin) {
        message("cannot edit the standard input");
        return;
    }
    const char *editor = getenv("EDITOR");
    if (!editor || !*editor)
        editor = "vi";
    const char *base = strrchr(editor, '/');
    base = base ? base + 1 : editor;
    char num[32];
    snprintf(num, sizeof num, "%d", top + 1);
    clear_prompt();
    restore_term();
    out_flush();
    pid_t pid = fork();
    if (pid == 0) {
        if (strcmp(base, "vi") == 0 || strcmp(base, "ex") == 0)
            execlp(editor, editor, "-c", num, cur_name, (char *)NULL);
        else
            execlp(editor, editor, cur_name, (char *)NULL);
        _exit(127);
    }
    if (pid > 0)
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
            ;
    set_raw(true);
    /* the same file and screen: read again, as it may have changed */
    char err[512];
    int t = top;
    if (open_source(cur_name, err, sizeof err) == 0) {
        ensure(t + 1);
        if (t >= nlines)
            t = nlines ? nlines - 1 : 0;
    }
    first_screen = false;
    fputs("\033[H\033[2J", out);
    redraw(t);
}

static void show_position(void)
{
    long long b = bot + 1 < nlines || !src_eof
                  ? (ensure(bot + 2) ? lines[bot + 1].off : src_off) : src_off;
    if (src_is_stdin || src_size < 0) {
        message("%s [%d/%d]", cur_name, cur_num, num_files);
        return;
    }
    message("%s [%d/%d] line %d  byte %lld/%lld  %d%%", cur_name, cur_num, num_files,
            bot + 2, b, src_size, src_size > 0 ? (int)(b * 100 / src_size) : 100);
}

static bool continues(int c)
{
    return c == 'f' || c == CTRL('F') || c == ' ' || c == 'j' || c == '\n' ||
           c == '\r' || c == 'd' || c == CTRL('D') || c == 's';
}

static int page(int start_line, bool run_p)
{
    memset(marks, -1, sizeof marks);
    prev_pos = 0;
    half = page_lines / 2 > 0 ? page_lines / 2 : 1;
    pb_failed = false;

    if (run_p && opt_p) {
        playback = opt_p;
        suppress = true;
    }
    top = -1;
    redraw(start_line);

    for (;;) {
        if (got_winch || got_cont) {
            bool cont = got_cont;
            int old_rows = scr_rows, old_cols = scr_cols;
            got_winch = got_cont = 0;
            update_size();
            if (cont || old_rows != scr_rows || old_cols != scr_cols)
                redraw(top);
        }
        if (suppress && (!playback || !*playback)) {
            playback = NULL;
            suppress = false;
            redraw(top);
            if (pb_failed)
                message("a -p command failed; the rest were not run");
        }
        if (pending_note && !suppress) {
            char *n = pending_note;
            pending_note = NULL;
            message("%s", n);
            free(n);
        }
        bool eof = at_eof();
        if (eof && opt_e && cur_num == num_files && !suppress) {
            clear_prompt();
            return R_QUIT;
        }
        if (!suppress)
            show_prompt();

        int count = 0;
        bool has_count = false;
        int c;
        while ((c = get_key()) >= '0' && c <= '9') {
            if (count < 100000000)
                count = count * 10 + (c - '0');
            has_count = true;
        }
        if (c == 0)
            continue;           /* refresh */
        clear_prompt();

        if (eof) {
            if (continues(c))
                return R_NEXT;
            if (cur_num == num_files)
                return R_QUIT;
        }

        switch (c) {
        case 'h':
            show_help();
            break;
        case 'f': case CTRL('F'):
            move_to(forward_target(top, has_count ? count : page_lines), false, false);
            break;
        case ' ':
            move_to(forward_target(top, has_count ? count : page_lines), true, false);
            break;
        case 'j': case '\n': case '\r':
            move_to(forward_target(top, has_count ? count : 1), true, false);
            break;
        case 'd': case CTRL('D'):
            if (has_count)
                half = count ? count : half;
            move_to(forward_target(top, half), true, false);
            break;
        case 's': {
            int t = forward_target(bot, has_count ? count : 1);
            move_to(t, false, false);
            break;
        }
        case 'b': case CTRL('B'):
        case 'k':
        case 'u': case CTRL('U'): {
            int n;
            if (c == 'k')
                n = has_count ? count : 1;
            else if (c == 'u' || c == CTRL('U')) {
                if (has_count)
                    half = count ? count : half;
                n = half;
            } else
                n = has_count ? count : page_lines;
            if (top == 0) {
                bell();
                break;
            }
            move_to(top - n < 0 ? 0 : top - n, false, c != 'b' && c != CTRL('B'));
            break;
        }
        case 'g': {
            int t = has_count && count > 0 ? count - 1 : 0;
            if (!ensure(t + 1)) {
                bell();
                break;
            }
            move_to(t, false, false);
            break;
        }
        case 'G':
            if (has_count) {
                if (count < 1 || !ensure(count)) {
                    bell();
                    break;
                }
                move_to(count - 1, false, false);
            } else {
                move_to(last_top(), false, false);
            }
            break;
        case 'r': case CTRL('L'):
            first_screen = false;
            fputs("\033[H\033[2J", out);
            redraw(top);
            break;
        case 'R': {
            /* discard buffered input: read the file again (not a pipe) */
            int t = top;
            char err[512];
            if (!src_is_stdin && open_source(cur_name, err, sizeof err) == 0) {
                ensure(t + 1);
                if (t >= nlines)
                    t = nlines ? nlines - 1 : 0;
            }
            first_screen = false;
            fputs("\033[H\033[2J", out);
            redraw(t);
            break;
        }
        case 'm': {
            int l = get_key();
            if (l >= 'a' && l <= 'z')
                marks[l - 'a'] = top;
            else
                bell();
            break;
        }
        case '\'': {
            int l = get_key();
            if (l == '\'')
                move_to(prev_pos, false, false);
            else if (l >= 'a' && l <= 'z' && marks[l - 'a'] >= 0)
                move_to(marks[l - 'a'], false, false);
            else
                message("mark not set");
            break;
        }
        case '/': case '?': {
            char *pat = get_line(c == '/' ? "/" : "?");
            bool neg = false;
            char *p = pat;
            if (*p == '!') {
                neg = true;
                p++;
            }
            if (*p) {
                if (!set_pattern(p)) {
                    free(pat);
                    break;
                }
            } else if (!have_re) {
                message("no previous pattern");
                free(pat);
                break;
            }
            free(pat);
            last_fwd = c == '/';
            last_neg = neg;
            /* forward: after the first line shown; backward: before it */
            int f = search(top, last_fwd, neg, has_count && count ? count : 1);
            if (f < 0)
                message("pattern not found");
            else
                move_to(f, false, false);
            break;
        }
        case 'n': case 'N': {
            if (!have_re) {
                message("no previous pattern");
                break;
            }
            bool fwd = c == 'n' ? last_fwd : !last_fwd;
            int f = search(top, fwd, last_neg, has_count && count ? count : 1);
            if (f < 0)
                message("pattern not found");
            else
                move_to(f, false, false);
            break;
        }
        case ':': {
            int c2 = get_key();
            if (c2 == 'q')
                return R_QUIT;
            if (c2 == 'n' || c2 == 'p') {
                int k = has_count && count ? count : 1;
                int target = c2 == 'n' ? cur_num - 1 + k : cur_num - 1 - k;
                if (target < 0 || target >= num_files) {
                    message(c2 == 'n' ? "no next file" : "no previous file");
                    break;
                }
                goto_index = target;
                return R_GOTO;
            }
            if (c2 == 'e') {
                char *arg = get_line(":e ");
                char *a = arg;
                while (*a == ' ' || *a == '\t')
                    a++;
                char *name;
                if (!*a) {
                    name = xstrdup(cur_name);
                } else if (strcmp(a, "#") == 0) {
                    if (!prev_examined) {
                        message("no previously examined file");
                        free(arg);
                        break;
                    }
                    name = xstrdup(prev_examined);
                } else {
                    wordexp_t we;
                    if (wordexp(a, &we, WRDE_NOCMD) == 0) {
                        name = xstrdup(we.we_wordc ? we.we_wordv[0] : a);
                        wordfree(&we);
                    } else {
                        name = xstrdup(a);
                    }
                }
                /* not accessible (or not seekable): the file and screen stay */
                FILE *chk = strcmp(name, "-") == 0 ? NULL : fopen(name, "r");
                if (!chk) {
                    message("%s: %s", name, strcmp(name, "-") == 0
                            ? "the standard input cannot be examined again" : strerror(errno));
                    free(name);
                    free(arg);
                    break;
                }
                fclose(chk);
                free(arg);
                free(examine_name);
                examine_name = name;
                examine_tagline = -1;
                return R_EXAMINE;
            }
            if (c2 == 't') {
                char *tag = get_line(":t ");
                char *file, *pat;
                int ln;
                if (!*tag || !find_tag(tag, &file, &pat, &ln)) {
                    message("tag not found: %s", tag);
                    free(tag);
                    break;
                }
                free(tag);
                free(examine_name);
                examine_name = file;
                free(examine_tagpat);
                examine_tagpat = pat;
                examine_tagline = ln;
                return R_EXAMINE;
            }
            bell();
            break;
        }
        case 'v':
            edit_file();
            break;
        case '=': case CTRL('G'):
            show_position();
            break;
        case 'q':
            return R_QUIT;
        case 'Z':
            if (get_key() == 'Z')
                return R_QUIT;
            bell();
            break;
        default:
            bell();
            break;
        }
    }
}

/* ====================================================================== */
/*  main                                                                  */
/* ====================================================================== */

static void usage(void)
{
    fputs("usage: more [-ceisu] [-n number] [-p command] [-t tagstring] [file...]\n", stderr);
    exit(1);
}

static void finish(void)
{
    if (out) {
        fputs("\r\033[K", ctl ? ctl : stderr);
        fflush(ctl ? ctl : stderr);
        fflush(out);
    }
    restore_term();
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    /* as if the command line were: more $MORE options operands */
    int nargc = 0;
    char **nargv = xmalloc(((size_t)argc + 2) * sizeof *nargv);
    nargv[nargc++] = argv[0];
    const char *env = getenv("MORE");
    char *envcopy = env ? xstrdup(env) : NULL;
    if (envcopy) {
        for (char *w = strtok(envcopy, " \t\n"); w; w = strtok(NULL, " \t\n")) {
            nargv = xrealloc(nargv, ((size_t)argc + (size_t)nargc + 2) * sizeof *nargv);
            nargv[nargc++] = w;
        }
    }
    for (int i = 1; i < argc; i++)
        nargv[nargc++] = argv[i];
    nargv[nargc] = NULL;

    int opt;
    while ((opt = getopt(nargc, nargv, "ceisun:p:t:")) != -1) {
        switch (opt) {
        case 'c': opt_c = true; break;
        case 'e': opt_e = true; break;
        case 'i': opt_i = true; break;
        case 's': opt_s = true; break;
        case 'u': opt_u = true; break;
        case 'n': {
            long v = 0;
            const char *p = optarg;
            if (!*p)
                v = -1;
            for (; *p; p++) {
                if (*p < '0' || *p > '9' || v > 100000) {
                    v = -1;
                    break;
                }
                v = v * 10 + (*p - '0');
            }
            if (v <= 0) {
                fprintf(stderr, "more: invalid number of lines: %s\n", optarg);
                return 1;
            }
            opt_n = (int)v;
            break;
        }
        case 'p': opt_p = optarg; break;
        case 't': opt_t = optarg; break;
        default:
            usage();
        }
    }
    char **files = nargv + optind;
    int nfiles = nargc - optind;
    static char *dash[] = { "-", NULL };

    if (!isatty(STDOUT_FILENO)) {
        if (nfiles == 0) {
            files = dash;
            nfiles = 1;
        }
        for (int i = 0; i < nfiles; i++) {
            if (strcmp(files[i], "-") == 0) {
                filter_file("standard input", stdin);
                clearerr(stdin);
                continue;
            }
            FILE *fp = fopen(files[i], "r");
            if (!fp) {
                fprintf(stderr, "more: %s: %s\n", files[i], strerror(errno));
                exit_status = 1;
                continue;
            }
            filter_file(files[i], fp);
            fclose(fp);
        }
        if (fflush(stdout) == EOF || ferror(stdout)) {
            perror("more: write error");
            return 1;
        }
        return exit_status;
    }

    /* the terminal: commands from standard error if it can be read */
    out = stdout;
    int fl = fcntl(STDERR_FILENO, F_GETFL);
    if (isatty(STDERR_FILENO) && fl >= 0 && (fl & O_ACCMODE) != O_WRONLY) {
        ctl_fd = STDERR_FILENO;
        ctl = stderr;
    } else {
        ctl_fd = open("/dev/tty", O_RDWR);
        if (ctl_fd < 0 || !(ctl = fdopen(ctl_fd, "w"))) {
            fprintf(stderr, "more: cannot read user commands: %s\n", strerror(errno));
            return 1;
        }
    }
    if (tcgetattr(ctl_fd, &orig_term) == 0)
        have_term = true;
    update_size();
    setup_signals();
    set_raw(true);
    atexit(finish);

    /* the files: the tag's file first with -t */
    char **list;
    int nlist;
    char *tfile = NULL, *tpat = NULL;
    int tline = 0;
    bool tag_ok = true;
    if (opt_t) {
        if (!find_tag(opt_t, &tfile, &tpat, &tline)) {
            restore_term();
            fprintf(stderr, "more: tag not found: %s\n", opt_t);
            return 1;
        }
    }
    if (nfiles == 0 && !tfile) {
        files = dash;
        nfiles = 1;
    }
    nlist = nfiles + (tfile ? 1 : 0);
    list = xmalloc((size_t)nlist * sizeof *list);
    if (tfile)
        list[0] = tfile;
    for (int i = 0; i < nfiles; i++)
        list[i + (tfile ? 1 : 0)] = files[i];
    num_files = nlist;

    int idx = 0, dir = 1;
    char *override = NULL;      /* a file from :e or :t, in place of list[idx] */
    char err[1024];
    bool first = true;

    while (idx >= 0 && idx < nlist) {
        const char *name = override ? override : list[idx];
        if (open_source(name, err, sizeof err) < 0) {
            if (first_screen) {
                fprintf(ctl, "\r\033[Kmore: %s\n", err);
                fflush(ctl);
            } else {
                message("%s", err);
            }
            exit_status = 1;
            free(override);
            override = NULL;
            idx += dir;
            continue;
        }
        cur_name = name;
        cur_num = idx + 1;
        next_name = idx + 1 < nlist ? list[idx + 1] : NULL;

        int sl = 0;
        bool run_p = true;
        if (first && tfile && idx == 0) {
            int l = tag_line(tpat, tline);
            if (l < 0) {
                tag_ok = false;
                run_p = false;          /* no -p for this file at any time */
                pending_note = xstrdup("the tag's line was not found in the file");
            } else {
                sl = l;
            }
        } else if (override && (examine_tagline >= 0 || examine_tagpat)) {
            int l = tag_line(examine_tagpat, examine_tagline);
            sl = l < 0 ? 0 : l;
            examine_tagline = -1;
            free(examine_tagpat);
            examine_tagpat = NULL;
        }
        first = false;
        if (tfile && idx == 0 && !tag_ok && !override)
            run_p = false;      /* the tag was not found: never -p here */
        int r = page(sl, run_p);

        free(prev_examined);
        prev_examined = xstrdup(name);

        if (r == R_QUIT)
            break;
        if (r == R_NEXT) {
            free(override);
            override = NULL;
            idx++;
            dir = 1;
        } else if (r == R_GOTO) {
            free(override);
            override = NULL;
            dir = goto_index < idx ? -1 : 1;
            idx = goto_index;
        } else if (r == R_EXAMINE) {
            free(override);
            override = examine_name;
            examine_name = NULL;
        }
    }
    free(override);
    close_source();
    return exit_status;
}
