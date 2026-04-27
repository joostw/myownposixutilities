/*
 * more — display files on a page-by-page basis  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis:
 *   [UP] more [-ceisu] [-n number] [-p command] [-t tagstring] [file...]
 *
 * Output split per spec:
 *   stdout  — file content display
 *   stderr  — prompts and reading user commands (fallback: /dev/tty)
 */

#define _POSIX_C_SOURCE 200809L

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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
#include <wordexp.h>

/* ================================================================ */
/*  Constants                                                         */
/* ================================================================ */

#ifndef CTRL
#define CTRL(x)     ((x) & 0x1f)
#endif
#define DEFAULT_LINES 24
#define DEFAULT_COLS  80

/* Return values from the interactive loop */
#define CMD_NEXT   1    /* advance to next file */
#define CMD_PREV  -1    /* go back to previous file */
#define CMD_QUIT  -2    /* exit more */
#define CMD_JUMP  -3    /* jump to file by index (stored in jump_target) */

/* ================================================================ */
/*  Options                                                           */
/* ================================================================ */

static bool opt_c;          /* -c: redraw (don't scroll) */
static bool opt_e;          /* -e: exit at EOF of last file */
static bool opt_i;          /* -i: case-insensitive search */
static int  opt_n;          /* -n: lines per screenful (0 = from terminal) */
static char *opt_p;         /* -p: command string to execute per file */
static bool opt_s;          /* -s: squeeze blank lines */
static char *opt_t;         /* -t: tagstring */
static bool opt_u;          /* -u: raw backspace */

/* ================================================================ */
/*  Terminal / screen state                                           */
/* ================================================================ */

/*
 * out: stdout — file content is written here (spec: "standard output shall
 *      be used to write the contents of the input files").
 * ctl: stderr or /dev/tty — prompts and user command input (spec: "standard
 *      error shall be used to read commands / write a prompting string").
 */
static FILE            *out;          /* content output (stdout) */
static FILE            *ctl;          /* control: prompts + command input */
static int              ctl_fd;       /* fd of ctl for termios */
static struct termios   orig_term;
static bool             raw_active;

static int  scr_rows;     /* terminal height */
static int  scr_cols;     /* terminal width */
static int  page_lines;   /* lines per page = scr_rows - 1 */
static int  half_sz;      /* half-screen size for d/u commands */

static volatile sig_atomic_t got_winch;

/* ================================================================ */
/*  Line buffer                                                       */
/* ================================================================ */

typedef struct {
    char   *text;   /* raw line content, without trailing newline (allocated) */
    size_t  len;    /* strlen(text) */
} line_t;

static line_t  *lbuf;
static int      lbuf_n;    /* number of lines */
static int      lbuf_cap;

static void lbuf_clear(void)
{
    for (int i = 0; i < lbuf_n; i++) free(lbuf[i].text);
    lbuf_n = 0;
}

static int lbuf_add(const char *text, size_t len)
{
    if (lbuf_n == lbuf_cap) {
        int nc = lbuf_cap ? lbuf_cap * 2 : 512;
        line_t *tmp = realloc(lbuf, nc * sizeof *tmp);
        if (!tmp) { perror("more"); return -1; }
        lbuf     = tmp;
        lbuf_cap = nc;
    }
    char *copy = malloc(len + 1);
    if (!copy) { perror("more"); return -1; }
    memcpy(copy, text, len);
    copy[len] = '\0';
    lbuf[lbuf_n].text = copy;
    lbuf[lbuf_n].len  = len;
    lbuf_n++;
    return 0;
}

/* ================================================================ */
/*  Text processing: backspace/underline/bold                         */
/* ================================================================ */

typedef struct {
    char ch;
    int  attr;  /* 0=normal, 1=underline, 2=bold */
} cell_t;

/*
 * Process a raw line with backspace sequences into display cells.
 * Handles:
 *   X\bX  → bold X          (same char overstruck)
 *   X\b_  → underline X     (char, backspace, underscore)
 *   _\bX  → underline X     (underscore, backspace, char)
 * Other \b characters erase the previous cell (like a real terminal).
 *
 * Returns the number of cells; caller must free *out.
 */
static size_t process_bs(const char *raw, size_t rawlen, cell_t **outp)
{
    cell_t *buf  = malloc((rawlen + 1) * sizeof *buf);
    if (!buf) { *outp = NULL; return 0; }
    size_t  pos  = 0;   /* current write position */
    size_t  high = 0;   /* high-water mark */

    for (size_t i = 0; i < rawlen; ) {
        unsigned char c = (unsigned char)raw[i++];

        if (c == '\r') continue; /* strip CR at end of line */

        if (c == '\b') {
            if (pos > 0) pos--;
            continue;
        }

        if (pos == high) {
            buf[pos].ch   = (char)c;
            buf[pos].attr = 0;
            high++;
        } else {
            /* Overwrite existing cell — apply bold/underline logic */
            char prev = buf[pos].ch;
            if ((char)c == '_' && prev != '_') {
                buf[pos].attr = 1; /* underline: keep prev char */
            } else if (prev == '_' && (char)c != '_') {
                buf[pos].ch   = (char)c;
                buf[pos].attr = 1; /* underline: use new char */
            } else if ((char)c == prev) {
                buf[pos].attr = 2; /* bold */
            } else {
                buf[pos].ch   = (char)c;
                buf[pos].attr = 0;
            }
        }
        pos++;
    }

    *outp = buf;
    return high;
}

static void write_cells(const cell_t *cells, size_t n)
{
    int attr = 0;
    for (size_t i = 0; i < n; i++) {
        if (cells[i].attr != attr) {
            if (attr) fputs("\033[0m", out);
            if (cells[i].attr == 1) fputs("\033[4m", out);
            else if (cells[i].attr == 2) fputs("\033[1m", out);
            attr = cells[i].attr;
        }
        fputc(cells[i].ch, out);
    }
    if (attr) fputs("\033[0m", out);
}

/* Visual width of a character (simplified ASCII). */
static int char_cols(unsigned char c)
{
    if (c >= 0x20 && c < 0x7f) return 1;
    if (c == '\t') return 8;
    return 2; /* ^X notation */
}

/* Visual width of a processed cell sequence. */
static int cells_width(const cell_t *cells, size_t n)
{
    int w = 0;
    for (size_t i = 0; i < n; i++)
        w += char_cols((unsigned char)cells[i].ch);
    return w;
}

/* How many display rows does a visual width occupy? */
static int visual_rows(int vis_width)
{
    if (vis_width == 0) return 1;
    return (vis_width + scr_cols - 1) / scr_cols;
}

/* ================================================================ */
/*  Screen-size management                                            */
/* ================================================================ */

static void update_screen_size(void)
{
    /* Priority: -n option > LINES env > terminal window size > default
     * Per spec SIGWINCH: query size from the terminal on *standard output*. */
    int rows = 0, cols = 0;

#ifdef TIOCGWINSZ
    {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            if (ws.ws_row > 0) rows = ws.ws_row;
            if (ws.ws_col > 0) cols = ws.ws_col;
        }
    }
#endif

    /* COLUMNS env overrides ioctl for cols; LINES overrides rows (unless -n). */
    const char *ev;
    ev = getenv("COLUMNS");
    if (ev && *ev) {
        char *e; long v = strtol(ev, &e, 10);
        if (*e == '\0' && v > 0) cols = (int)v;
    }
    if (!opt_n) {
        ev = getenv("LINES");
        if (ev && *ev) {
            char *e; long v = strtol(ev, &e, 10);
            if (*e == '\0' && v > 1) rows = (int)v;
        }
    }

    scr_rows   = (rows > 1) ? rows : DEFAULT_LINES;
    scr_cols   = (cols > 0) ? cols : DEFAULT_COLS;
    page_lines = opt_n ? opt_n : scr_rows - 1;
    if (page_lines < 1) page_lines = 1;
    half_sz = page_lines / 2;
    if (half_sz < 1) half_sz = 1;
}

/* ================================================================ */
/*  Signal handlers                                                   */
/* ================================================================ */

static void handle_sigwinch(int sig)
{
    (void)sig;
    got_winch = 1;
}

static void handle_sigcont(int sig)
{
    (void)sig;
    got_winch = 1; /* treat SIGCONT like SIGWINCH: always refresh */
}

/* ================================================================ */
/*  Terminal raw mode                                                 */
/* ================================================================ */

static void leave_raw(void)
{
    if (raw_active && ctl) {
        tcsetattr(ctl_fd, TCSANOW, &orig_term);
        raw_active = false;
    }
}

static void enter_raw(void)
{
    if (!ctl) return;
    tcgetattr(ctl_fd, &orig_term);
    struct termios t = orig_term;
    t.c_lflag &= ~(unsigned)(ECHO | ICANON);
    t.c_cc[VMIN]  = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(ctl_fd, TCSANOW, &t);
    raw_active = true;
}

static void cleanup_exit(void)
{
    leave_raw();
    if (out) {
        fputs("\033[?25h", out); /* show cursor */
        fflush(out);
    }
}

/* ================================================================ */
/*  Display                                                           */
/* ================================================================ */

static int  cur_top;    /* index of first displayed file line */
static int  cur_bot;    /* index of last displayed file line */
static bool first_screen = true;
static bool suppress_display; /* true during -p playback (no intermediate results) */

/*
 * Write one file line to stdout (out), handling backspace sequences
 * or -u raw mode.  Returns the number of visual display rows consumed.
 */
static int write_line(int idx)
{
    const line_t *l = &lbuf[idx];
    int vr;

    if (opt_u) {
        int col = 0;
        for (size_t i = 0; i < l->len; i++) {
            unsigned char c = (unsigned char)l->text[i];
            if (c == '\t') {
                int stop = (col + 8) & ~7;
                for (; col < stop; col++) fputc(' ', out);
            } else if (c >= 0x20 && c < 0x7f) {
                fputc(c, out);
                col++;
            } else {
                fputc('^', out);
                fputc((c == 0x7f) ? '?' : (char)('@' + c), out);
                col += 2;
            }
        }
        vr = visual_rows(col);
    } else {
        cell_t *cells;
        size_t  nc = process_bs(l->text, l->len, &cells);
        int     vw = cells_width(cells, nc);
        write_cells(cells, nc);
        free(cells);
        vr = visual_rows(vw);
    }

    fputs("\033[K\n", out); /* clear to EOL, newline */
    return vr;
}

/*
 * Compute cur_bot without any output — used during suppress_display
 * so that at_eof() and other position queries remain correct.
 */
static void compute_cur_bot(void)
{
    int rows_left = page_lines;
    int fl        = cur_top;
    while (rows_left > 0 && fl < lbuf_n) {
        /* Simplified: treat every line as 1 row for cur_bot purposes.
         * The real write_line wraps long lines; good enough for navigation. */
        rows_left--;
        fl++;
    }
    cur_bot = fl - 1;
    if (cur_bot < cur_top) cur_bot = cur_top;
}

static void display_screen(void)
{
    if (suppress_display) {
        compute_cur_bot();
        return;
    }

    if (first_screen || opt_c) {
        fputs("\033[H\033[J", out);
        first_screen = false;
    } else {
        fputs("\033[H", out);
    }

    int rows_left = page_lines;
    int fl        = cur_top;

    while (rows_left > 0 && fl < lbuf_n) {
        int vr = write_line(fl);
        if (vr > rows_left) vr = rows_left;
        rows_left -= vr;
        fl++;
    }

    cur_bot = fl - 1;
    if (cur_bot < cur_top) cur_bot = cur_top;

    /* Clear any remaining screen lines */
    while (rows_left-- > 0)
        fputs("\033[K\n", out);

    fflush(out);
}

/* ================================================================ */
/*  Prompt (written to ctl = stderr)                                  */
/* ================================================================ */

static void show_prompt(const char *filename, int file_num, int n_files,
                        bool at_eof, const char *next_name)
{
    fprintf(ctl, "\033[%d;1H\033[K", scr_rows);
    fputs("\033[7m", ctl); /* reverse video */

    if (at_eof) {
        if (next_name)
            fprintf(ctl, "--More-- (Next file: %s)", next_name);
        else
            fprintf(ctl, "--More-- (END)");
    } else {
        if (n_files > 1)
            fprintf(ctl, "--More--(%s [%d/%d])", filename, file_num, n_files);
        else
            fprintf(ctl, "--More--(%s)", filename);
    }

    fputs("\033[0m", ctl);
    fflush(ctl);
}

static void clear_prompt(void)
{
    fprintf(ctl, "\033[%d;1H\033[K", scr_rows);
    fflush(ctl);
}

static void status_msg(const char *fmt, ...)
{
    va_list ap;
    fprintf(ctl, "\033[%d;1H\033[K\033[7m", scr_rows);
    va_start(ap, fmt);
    vfprintf(ctl, fmt, ap);
    va_end(ap);
    fputs("\033[0m", ctl);
    fflush(ctl);
}

/* ================================================================ */
/*  Command input                                                     */
/* ================================================================ */

/* Playback pointer for -p option command string */
static const char *playback;

static int ctl_getchar(void)
{
    if (playback && *playback)
        return (unsigned char)*playback++;
    fflush(ctl);
    int c = fgetc(ctl);
    return (c == EOF) ? 'q' : c;
}

/*
 * Read an optional decimal count followed by a command character.
 * *has_count is set to true iff the user actually typed digit(s).
 * *count is the parsed value (0 if no digits typed).
 */
static int read_cmd(int *count, bool *has_count)
{
    *count     = 0;
    *has_count = false;
    int c;

    for (;;) {
        c = ctl_getchar();
        if (got_winch) {
            update_screen_size();
            got_winch = 0;
            display_screen();
            return 0; /* null command → redisplay */
        }
        if (c == EOF || c == 'q') return 'q';
        if (isdigit(c)) {
            *count     = *count * 10 + (c - '0');
            *has_count = true;
        } else {
            break;
        }
    }
    return c;
}

/* Read a single character (used for m, ' commands) */
static int read_one(void)
{
    return ctl_getchar();
}

/* Read a pattern terminated by newline; returns allocated string or NULL */
static char *read_pattern(char lead)
{
    fprintf(ctl, "\033[%d;1H\033[K%c", scr_rows, lead);
    fflush(ctl);

    /* Temporarily restore canonical mode for comfortable pattern entry */
    if (raw_active) {
        struct termios t = orig_term;
        t.c_lflag |= ECHO | ICANON;
        tcsetattr(ctl_fd, TCSANOW, &t);
    }

    char buf[256] = {0};
    if (!fgets(buf, sizeof buf, ctl)) {
        enter_raw();
        return NULL;
    }
    enter_raw();

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
    return strdup(buf);
}

/* ================================================================ */
/*  Search                                                            */
/* ================================================================ */

static char    last_pat[256];
static bool    last_fwd  = true;
static bool    last_neg  = false;
static regex_t last_re;
static bool    re_valid  = false;

static bool compile_re(const char *pat)
{
    if (re_valid) { regfree(&last_re); re_valid = false; }
    int flags = REG_NOSUB | (opt_i ? REG_ICASE : 0);
    int rc    = regcomp(&last_re, pat, flags);
    if (rc != 0) {
        char errbuf[256];
        regerror(rc, &last_re, errbuf, sizeof errbuf);
        status_msg("bad pattern: %s", errbuf);
        return false;
    }
    re_valid = true;
    strncpy(last_pat, pat, sizeof last_pat - 1);
    return true;
}

static bool line_matches(int idx)
{
    if (!re_valid) return false;
    return regexec(&last_re, lbuf[idx].text, 0, NULL, 0) == 0;
}

/* Search forward from line 'from' (exclusive) for count-th match. */
static int search_fwd(int from, bool neg, int count)
{
    int found = 0;
    for (int i = from + 1; i < lbuf_n; i++) {
        bool m = line_matches(i);
        if (neg ? !m : m) {
            if (++found == count) return i;
        }
    }
    return -1;
}

/* Search backward from line 'from' (exclusive) for count-th match. */
static int search_bwd(int from, bool neg, int count)
{
    int found = 0;
    for (int i = from - 1; i >= 0; i--) {
        bool m = line_matches(i);
        if (neg ? !m : m) {
            if (++found == count) return i;
        }
    }
    return -1;
}

/* ================================================================ */
/*  Scrolling helpers                                                 */
/* ================================================================ */

static int  prev_pos;           /* for '' (return to prev large movement) */
static int  marks[26];          /* a-z marks */
static int  jump_target;        /* used with CMD_JUMP */

/*
 * Move cur_top to 'line'.  Per spec, '' returns to the position of the
 * last "large movement" — defined as movement of more than a screenful.
 */
static void goto_line(int line)
{
    if (line < 0)       line = 0;
    if (line >= lbuf_n) line = (lbuf_n > 0) ? lbuf_n - 1 : 0;
    if (abs(line - cur_top) > page_lines)
        prev_pos = cur_top;
    cur_top = line;
}

static void scroll_fwd(int n)
{
    int new_top = cur_top + n;
    if (new_top + page_lines > lbuf_n)
        new_top = lbuf_n - page_lines;
    goto_line(new_top);
}

static void scroll_bwd(int n)
{
    goto_line(cur_top - n);
}

static bool at_eof(void) { return cur_bot >= lbuf_n - 1; }

/* ================================================================ */
/*  Tag lookup                                                        */
/* ================================================================ */

static char *find_tag(const char *tagstr, char **filename, int *lineno)
{
    FILE *tf = fopen("tags", "r");
    if (!tf) return NULL;

    char  *line = NULL;
    size_t cap  = 0;
    char  *result = NULL;
    *filename = NULL;
    *lineno   = 0;

    while (getline(&line, &cap, tf) >= 0) {
        if (line[0] == '!') continue;
        char *tab1 = strchr(line, '\t');
        if (!tab1) continue;
        size_t taglen = (size_t)(tab1 - line);
        if (taglen != strlen(tagstr) || memcmp(line, tagstr, taglen) != 0)
            continue;

        char *tab2 = strchr(tab1 + 1, '\t');
        if (!tab2) continue;
        *filename = strndup(tab1 + 1, (size_t)(tab2 - tab1 - 1));

        char *addr = tab2 + 1;
        char *nl = strchr(addr, '\n'); if (nl) *nl = '\0';

        if (*addr == '/' || *addr == '?') {
            char *end = strrchr(addr + 1, *addr);
            if (end) *end = '\0';
            result = strdup(addr + 1);
        } else {
            *lineno = (int)strtol(addr, NULL, 10);
        }
        break;
    }

    free(line);
    fclose(tf);
    return result;
}

/* ================================================================ */
/*  Interactive loop                                                  */
/* ================================================================ */

static void show_help(void)
{
    static const char help[] =
        "  h              Display this help\n"
        "  [n]f  [n]^F   Forward  n lines (default: one screenful)\n"
        "  [n]b  [n]^B   Backward n lines (default: one screenful)\n"
        "  [n]SPACE       Forward n lines (default: one screenful)\n"
        "  [n]j [n]ENTER  Forward n lines (default: 1)\n"
        "  [n]k           Backward n lines\n"
        "  [n]d  [n]^D   Forward  n half-screens\n"
        "  [n]u  [n]^U   Backward n half-screens\n"
        "  [n]s           Skip n lines forward\n"
        "  [n]g           Go to line n (default: first line)\n"
        "  [n]G           Go to line n (default: last line)\n"
        "  r   ^L         Refresh screen\n"
        "  R              Refresh screen (discard buffered input)\n"
        "  m{a-z}         Mark current position with letter\n"
        "  '{a-z}         Return to marked position\n"
        "  ''             Return to previous large-movement position\n"
        "  [n]/[!]pat     Search forward  for nth match of pat\n"
        "  [n]?[!]pat     Search backward for nth match of pat\n"
        "  [n]n           Repeat last search (same direction)\n"
        "  [n]N           Repeat last search (opposite direction)\n"
        "  :e [file]      Examine new file ('#' = previously examined)\n"
        "  [n]:n          Examine next file\n"
        "  [n]:p          Examine previous file\n"
        "  :t tagstring   Go to tag\n"
        "  v              Invoke editor\n"
        "  =  ^G          Display current position info\n"
        "  q  :q  ZZ      Quit\n";

    fputs("\033[H\033[J", out);
    fputs(help, out);
    fflush(out);
    fputs("\n--Press any key to continue--", ctl);
    fflush(ctl);
    ctl_getchar();
    display_screen();
}

static void invoke_editor(const char *filename, int lineno)
{
    leave_raw();
    fputs("\033[?25h", out);
    fflush(out);

    const char *editor = getenv("EDITOR");
    if (!editor || !*editor) editor = "vi";

    const char *base = strrchr(editor, '/');
    base = base ? base + 1 : editor;
    bool is_vi = (strcmp(base, "vi") == 0 || strcmp(base, "ex") == 0);

    char linebuf[32];
    snprintf(linebuf, sizeof linebuf, "%d", lineno + 1);

    pid_t pid = fork();
    if (pid == 0) {
        if (is_vi)
            execlp(editor, editor, "-c", linebuf, filename, (char *)NULL);
        else
            execlp(editor, editor, filename, (char *)NULL);
        _exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
    }

    enter_raw();
    fputs("\033[?25l", out);
    fflush(out);
    first_screen = true;
    display_screen();
}

static int load_file(const char *path, FILE *fp_override);
static char *examine_buf;      /* set by :e/:t for next file to load */
static char *prev_filename;    /* for ':e #' — the previously examined file */
static int   examine_line;     /* 1-based target line after :t (0 = regex search, -1 = unused) */

/*
 * Main interactive loop for one file.
 *
 * filename: display name
 * file_num, n_files: position in file list (1-based)
 * next_name: name of next file, or NULL
 *
 * Returns CMD_NEXT, CMD_PREV, CMD_QUIT, or CMD_JUMP.
 */
static int interact(const char *filename, int file_num, int n_files,
                    const char *next_name)
{
    cur_top      = 0;
    cur_bot      = 0;
    first_screen = true;
    memset(marks, -1, sizeof marks);
    prev_pos = 0;

    /* Reset half_sz to half the page for each new file */
    half_sz = page_lines / 2;
    if (half_sz < 1) half_sz = 1;

    const char *saved_pb = playback;
    playback = NULL;

    /* -p: suppress intermediate display during playback */
    if (opt_p) {
        suppress_display = true;
        playback = opt_p;
    }

    display_screen();

    /* -e: if last file and already at EOF after first screen, exit */
    if (opt_e && at_eof() && file_num == n_files) {
        suppress_display = false;
        playback = saved_pb;
        return CMD_NEXT;
    }

    for (;;) {
        if (got_winch) {
            update_screen_size();
            got_winch = 0;
            suppress_display = false;
            display_screen();
        }

        /* When playback ends, do the deferred display */
        if (suppress_display && (!playback || !*playback)) {
            suppress_display = false;
            playback = NULL;
            display_screen();
        }

        bool eof = at_eof();

        if (!suppress_display)
            show_prompt(filename, file_num, n_files, eof,
                        eof ? next_name : NULL);

        int count;
        bool has_count;
        int cmd = read_cmd(&count, &has_count);
        int n   = has_count ? count : 0; /* 0 = "use default" */

        if (!suppress_display)
            clear_prompt();

        switch (cmd) {

        /* ---- Help ---- */
        case 'h':
            show_help();
            break;

        /* ---- Scroll forward one screenful (f / ^F) ---- */
        case 'f': case CTRL('F'):
            /* Spec: at EOF, advance to next file; if last file, exit more */
            if (eof) {
                playback = saved_pb; suppress_display = false;
                return CMD_NEXT; /* main loop exits when file_idx >= eff_n */
            }
            scroll_fwd(has_count ? n : page_lines);
            display_screen();
            break;

        /* ---- Scroll forward: space ---- */
        case ' ':
            if (eof) {
                playback = saved_pb; suppress_display = false;
                return CMD_NEXT;
            }
            scroll_fwd(has_count ? n : page_lines);
            display_screen();
            break;

        /* ---- Scroll forward: j / newline — default 1 line ---- */
        case 'j': case '\n': case '\r':
            if (eof) {
                playback = saved_pb; suppress_display = false;
                return CMD_NEXT;
            }
            scroll_fwd(has_count ? n : 1);
            display_screen();
            break;

        /* ---- Scroll backward one screenful (b / ^B) ---- */
        case 'b': case CTRL('B'):
            scroll_bwd(has_count ? n : page_lines);
            display_screen();
            break;

        /* ---- Scroll backward one line ---- */
        case 'k':
            scroll_bwd(has_count ? n : 1);
            display_screen();
            break;

        /* ---- Forward half screen (d / ^D) ---- */
        case 'd': case CTRL('D'):
            if (eof) {
                playback = saved_pb; suppress_display = false;
                return CMD_NEXT;
            }
            /* Spec: if count specified, it becomes new default for d/u */
            if (has_count) half_sz = n;
            scroll_fwd(half_sz);
            display_screen();
            break;

        /* ---- Skip forward (s) ---- */
        case 's':
            {
                if (eof) {
                    playback = saved_pb; suppress_display = false;
                    return CMD_NEXT;
                }
                int skip = has_count ? n : 1;
                int new_top = cur_bot + skip;
                /* Spec: if less than one screenful remains, show last screenful */
                if (new_top + page_lines > lbuf_n)
                    new_top = lbuf_n - page_lines;
                if (new_top < 0) new_top = 0;
                goto_line(new_top);
                display_screen();
            }
            break;

        /* ---- Backward half screen (u / ^U) ---- */
        case 'u': case CTRL('U'):
            /* Spec: count becomes new default for d/^D/u/^U */
            if (has_count) half_sz = n;
            scroll_bwd(half_sz);
            display_screen();
            break;

        /* ---- Go to line (g) ---- */
        case 'g':
            /* Spec: "display the screenful beginning with line count" */
            goto_line(has_count ? n - 1 : 0);
            display_screen();
            break;

        /* ---- Go to end (G) ---- */
        case 'G':
            if (has_count)
                goto_line(n - 1);
            else
                goto_line(lbuf_n - page_lines);
            display_screen();
            break;

        /* ---- Refresh (r / ^L) ---- */
        case 'r': case CTRL('L'):
            suppress_display = false;
            display_screen();
            break;

        /* ---- Refresh discarding buffered input (R) ---- */
        case 'R':
            /* For non-seekable files same as r (spec-allowed) */
            suppress_display = false;
            first_screen = true;
            display_screen();
            break;

        /* ---- Mark position (m{a-z}) ---- */
        case 'm':
            {
                int ltr = read_one();
                if (ltr >= 'a' && ltr <= 'z')
                    marks[ltr - 'a'] = cur_top;
            }
            break;

        /* ---- Return to mark (' {a-z} or '') ---- */
        case '\'':
            {
                int ltr = read_one();
                if (ltr == '\'') {
                    /* '' — return to last large-movement position */
                    int saved = prev_pos;
                    goto_line(saved);
                } else if (ltr >= 'a' && ltr <= 'z' && marks[ltr - 'a'] >= 0) {
                    goto_line(marks[ltr - 'a']);
                } else {
                    status_msg("mark not set");
                    (void)read_one();
                    display_screen();
                    break;
                }
                display_screen();
            }
            break;

        /* ---- Search forward (/[!]pattern) ---- */
        case '/':
            {
                char *pat = read_pattern('/');
                bool neg = false;
                if (pat && pat[0] == '!') {
                    neg = true;
                    memmove(pat, pat + 1, strlen(pat));
                }
                if (pat) {
                    if (*pat == '\0' && re_valid) {
                        /* null pattern: repeat last */
                    } else if (*pat != '\0') {
                        if (!compile_re(pat)) { free(pat); display_screen(); break; }
                    }
                    last_fwd = true; last_neg = neg;
                    free(pat);
                }
                int found = search_fwd(cur_top, neg, has_count ? count : 1);
                if (found < 0) {
                    status_msg("Pattern not found");
                    (void)ctl_getchar();
                } else {
                    goto_line(found);
                }
                display_screen();
            }
            break;

        /* ---- Search backward (?[!]pattern) ---- */
        case '?':
            {
                char *pat = read_pattern('?');
                bool neg = false;
                if (pat && pat[0] == '!') {
                    neg = true;
                    memmove(pat, pat + 1, strlen(pat));
                }
                if (pat) {
                    if (*pat == '\0' && re_valid) {
                        /* repeat */
                    } else if (*pat != '\0') {
                        if (!compile_re(pat)) { free(pat); display_screen(); break; }
                    }
                    last_fwd = false; last_neg = neg;
                    free(pat);
                }
                int found = search_bwd(cur_top, neg, has_count ? count : 1);
                if (found < 0) {
                    status_msg("Pattern not found");
                    (void)ctl_getchar();
                } else {
                    goto_line(found);
                }
                display_screen();
            }
            break;

        /* ---- Repeat search (n / N) ---- */
        case 'n':
            {
                int cnt = has_count ? count : 1;
                int found = last_fwd
                    ? search_fwd(cur_top, last_neg, cnt)
                    : search_bwd(cur_top, last_neg, cnt);
                if (found < 0) {
                    status_msg("Pattern not found");
                    (void)ctl_getchar();
                } else {
                    goto_line(found);
                }
                display_screen();
            }
            break;

        case 'N':
            {
                int cnt = has_count ? count : 1;
                int found = last_fwd
                    ? search_bwd(cur_top, last_neg, cnt)
                    : search_fwd(cur_top, last_neg, cnt);
                if (found < 0) {
                    status_msg("Pattern not found");
                    (void)ctl_getchar();
                } else {
                    goto_line(found);
                }
                display_screen();
            }
            break;

        /* ---- Colon commands (:e, :n, :p, :q, :t) ---- */
        case ':':
            {
                int c2 = read_one();
                switch (c2) {

                case 'e':
                    {
                        char *fn = read_pattern(':');
                        if (!fn || *fn == '\0') {
                            /* :e — re-examine current file */
                            free(fn);
                            first_screen = true;
                            cur_top = 0;
                            display_screen();
                        } else if (strcmp(fn, "#") == 0) {
                            /* :e # — re-examine previously examined file */
                            free(fn);
                            if (!prev_filename) {
                                status_msg("No previously examined file");
                                (void)ctl_getchar();
                                display_screen();
                            } else {
                                free(examine_buf);
                                examine_buf = strdup(prev_filename);
                                suppress_display = false;
                                playback = saved_pb;
                                return CMD_JUMP;
                            }
                        } else {
                            /* Spec: "subjected to shell word expansions" */
                            wordexp_t we;
                            char *expanded = fn;
                            if (wordexp(fn, &we, WRDE_NOCMD) == 0
                                    && we.we_wordc >= 1) {
                                expanded = strdup(we.we_wordv[0]);
                                wordfree(&we);
                                free(fn);
                            } else {
                                wordfree(&we);
                                /* fall through with the literal string */
                            }

                            /* Check accessibility before committing */
                            if (strcmp(expanded, "-") != 0) {
                                FILE *chk = fopen(expanded, "r");
                                if (!chk) {
                                    status_msg("cannot open '%s': %s",
                                               expanded, strerror(errno));
                                    (void)ctl_getchar();
                                    display_screen();
                                    free(expanded);
                                    break;
                                }
                                fclose(chk);
                            }
                            free(examine_buf);
                            examine_buf  = expanded;
                            examine_line = -1; /* no line positioning for :e */
                            suppress_display = false;
                            playback = saved_pb;
                            return CMD_JUMP;
                        }
                    }
                    break;

                case 'n':
                    /* :n — examine next file */
                    if (file_num >= n_files) {
                        status_msg("No next file");
                        (void)ctl_getchar();
                        display_screen();
                    } else {
                        int skip = has_count ? count : 1;
                        jump_target = file_num - 1 + skip; /* 0-based */
                        suppress_display = false;
                        playback = saved_pb;
                        return CMD_JUMP;
                    }
                    break;

                case 'p':
                    /* :p — examine previous file */
                    if (file_num <= 1) {
                        status_msg("No previous file");
                        (void)ctl_getchar();
                        display_screen();
                    } else {
                        int skip = has_count ? count : 1;
                        jump_target = file_num - 1 - skip; /* 0-based */
                        if (jump_target < 0) jump_target = 0;
                        suppress_display = false;
                        playback = saved_pb;
                        return CMD_JUMP;
                    }
                    break;

                case 'q':
                    suppress_display = false;
                    playback = saved_pb;
                    return CMD_QUIT;

                case 't':
                    {
                        char *tag = read_pattern(':');
                        if (tag && *tag) {
                            char *tfn = NULL;
                            int   tln = 0;
                            char *tre = find_tag(tag, &tfn, &tln);
                            if (!tfn) {
                                status_msg("Tag not found: %s", tag);
                                (void)ctl_getchar();
                                display_screen();
                            } else {
                                free(examine_buf);
                                examine_buf  = tfn;
                                examine_line = tln; /* 0=regex, >0=line# (1-based) */
                                jump_target  = -1;  /* signals :e-style CMD_JUMP */
                                if (tre) { compile_re(tre); free(tre); }
                                free(tag);
                                suppress_display = false;
                                playback = saved_pb;
                                return CMD_JUMP;
                            }
                        }
                        free(tag);
                    }
                    break;

                default:
                    status_msg("Unknown command: :%c", c2);
                    (void)ctl_getchar();
                    display_screen();
                    break;
                }
            }
            break;

        /* ---- Invoke editor (v) ---- */
        case 'v':
            invoke_editor(filename, cur_top);
            break;

        /* ---- Display position (= / ^G) ---- */
        case '=': case CTRL('G'):
            {
                long byte_off = 0;
                long total    = 0;
                for (int i = 0; i < lbuf_n; i++) {
                    if (i < cur_bot + 1) byte_off += (long)(lbuf[i].len + 1);
                    total += (long)(lbuf[i].len + 1);
                }
                int pct = (total > 0)
                        ? (int)((byte_off * 100) / total)
                        : 100;
                /* Spec: "information references the first byte of the line
                 * after the last line of the file on the screen" → line cur_bot+2 */
                status_msg("%s [%d/%d] line %d/%d byte %ld/%ld %d%%",
                           filename, file_num, n_files,
                           cur_bot + 2, lbuf_n,
                           byte_off, total, pct);
                (void)ctl_getchar();
                display_screen();
            }
            break;

        /* ---- Quit ---- */
        case 'q': case 'Q':
            suppress_display = false;
            playback = saved_pb;
            return CMD_QUIT;

        /* ---- ZZ quit ---- */
        case 'Z':
            {
                int c2 = read_one();
                if (c2 == 'Z') {
                    suppress_display = false;
                    playback = saved_pb;
                    return CMD_QUIT;
                }
            }
            break;

        case CTRL('C'):
            suppress_display = false;
            playback = saved_pb;
            return CMD_QUIT;

        case 0:
            /* Null command from read_cmd (SIGWINCH handled) */
            break;

        default:
            fputc('\a', ctl);
            fflush(ctl);
            break;
        }

        /* -e: exit immediately after writing the last line of the last file */
        if (opt_e && at_eof() && file_num == n_files && !suppress_display) {
            suppress_display = false;
            playback = saved_pb;
            return CMD_NEXT;
        }
    }
}

/* ================================================================ */
/*  File loading                                                      */
/* ================================================================ */

static int load_file(const char *path, FILE *fp_override)
{
    FILE *fp;
    bool  close_fp = false;

    if (fp_override) {
        fp = fp_override;
    } else if (strcmp(path, "-") == 0) {
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp) {
            fprintf(stderr, "more: cannot open '%s': %s\n",
                    path, strerror(errno));
            return -1;
        }
        close_fp = true;
    }

    lbuf_clear();

    char   *line = NULL;
    size_t  cap  = 0;
    ssize_t n;
    bool    prev_blank = false;

    while ((n = getline(&line, &cap, fp)) >= 0) {
        size_t len = (size_t)n;
        if (len > 0 && line[len - 1] == '\n') len--;
        /* -u: keep \r so write_line can display it as ^M (spec requirement).
         * Without -u, process_bs() strips \r internally. */
        if (!opt_u && len > 0 && line[len - 1] == '\r') len--;

        bool blank = (len == 0);
        if (opt_s && blank && prev_blank) continue;
        prev_blank = blank;

        if (lbuf_add(line, len) < 0) {
            free(line);
            if (close_fp) fclose(fp);
            return -1;
        }
    }

    free(line);
    if (ferror(fp))
        fprintf(stderr, "more: error reading '%s': %s\n", path, strerror(errno));
    if (close_fp) fclose(fp);
    return 0;
}

/* ================================================================ */
/*  Filter mode (non-terminal stdout)                                 */
/* ================================================================ */

static void filter_file(const char *path, FILE *fp_override)
{
    FILE *fp;
    bool  close_fp = false;

    if (fp_override) {
        fp = fp_override;
    } else if (!path || strcmp(path, "-") == 0) {
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp) {
            fprintf(stderr, "more: cannot open '%s': %s\n",
                    path, strerror(errno));
            return;
        }
        close_fp = true;
    }

    char   *line = NULL;
    size_t  cap  = 0;
    ssize_t n;
    bool    prev_blank = false;

    while ((n = getline(&line, &cap, fp)) >= 0) {
        bool blank = (n <= 1);
        if (opt_s && blank && prev_blank) continue;
        prev_blank = blank;
        fwrite(line, 1, (size_t)n, stdout);
    }

    free(line);
    if (close_fp) fclose(fp);
}

/* ================================================================ */
/*  Option parsing (command-line and MORE env variable)               */
/* ================================================================ */

static int parse_one_opt(int opt, const char *optarg_val)
{
    switch (opt) {
    case 'c': opt_c = true; break;
    case 'e': opt_e = true; break;
    case 'i': opt_i = true; break;
    case 'n':
        {
            if (!optarg_val) return -1;
            char *end;
            long v = strtol(optarg_val, &end, 10);
            if (*end || v <= 0) {
                fprintf(stderr, "more: invalid line count: %s\n", optarg_val);
                return -1;
            }
            opt_n = (int)v;
        }
        break;
    case 'p': opt_p = (char *)optarg_val; break;
    case 's': opt_s = true; break;
    case 't': opt_t = (char *)optarg_val; break;
    case 'u': opt_u = true; break;
    default:
        fprintf(stderr, "more: unknown option -%c\n", opt);
        return -1;
    }
    return 0;
}

/* ================================================================ */
/*  main                                                              */
/* ================================================================ */

static void usage(void)
{
    fprintf(stderr,
        "usage: more [-ceisu] [-n number] [-p command] [-t tagstring]"
        " [file...]\n");
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    /* Process MORE environment variable first */
    {
        const char *more_env = getenv("MORE");
        if (more_env) {
            char *env_copy = strdup(more_env);
            char *tok      = strtok(env_copy, " \t");
            while (tok) {
                if (tok[0] == '-') {
                    for (int i = 1; tok[i]; i++) {
                        char o = tok[i];
                        if (o == 'n' || o == 'p' || o == 't') {
                            char *arg = tok[i + 1]
                                      ? tok + i + 1
                                      : strtok(NULL, " \t");
                            parse_one_opt(o, arg);
                            break;
                        }
                        parse_one_opt(o, NULL);
                    }
                }
                tok = strtok(NULL, " \t");
            }
            free(env_copy);
        }
    }

    /* Command-line options override MORE env */
    int opt;
    while ((opt = getopt(argc, argv, "ceisun:p:t:")) != -1) {
        if (opt == '?') { usage(); return 1; }
        if (parse_one_opt(opt, optarg) < 0) { usage(); return 1; }
    }

    int     n_files = argc - optind;
    char  **files   = argv + optind;

    /* If stdout is not a terminal: filter mode.
     * Only -s has effect in filter mode; all other options are ignored. */
    if (!isatty(STDOUT_FILENO)) {
        if (n_files == 0) {
            filter_file(NULL, stdin);
        } else {
            for (int i = 0; i < n_files; i++) {
                if (strcmp(files[i], "-") == 0)
                    filter_file("-", stdin);
                else
                    filter_file(files[i], NULL);
            }
        }
        return 0;
    }

    /* Terminal mode.
     * out = stdout for file content.
     * ctl = stderr (per spec) if readable+writable terminal,
     *       else fall back to /dev/tty (spec allows this as fallback). */
    out = stdout;

    if (isatty(STDERR_FILENO)) {
        ctl = stderr;
    } else {
        ctl = fopen("/dev/tty", "r+");
        if (!ctl) {
            fprintf(stderr,
                "more: cannot open terminal for user commands\n");
            return 1;
        }
    }
    ctl_fd = fileno(ctl);

    /* Set up signals */
    {
        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sa.sa_handler = handle_sigwinch;
        sigaction(SIGWINCH, &sa, NULL);
        sa.sa_handler = handle_sigcont;
        sigaction(SIGCONT, &sa, NULL);
    }

    update_screen_size();
    enter_raw();
    atexit(cleanup_exit);

    fputs("\033[?25l", out); /* hide cursor during paging */
    fflush(out);

    /* Handle -t tagstring: prepend the tag's file to the file list */
    char *tag_file = NULL;
    int   tag_line = 0;
    if (opt_t) {
        char *tre = find_tag(opt_t, &tag_file, &tag_line);
        if (!tag_file) {
            fprintf(stderr, "more: tag not found: %s\n", opt_t);
            cleanup_exit();
            return 1;
        }
        if (tre) { compile_re(tre); free(tre); }
    }

    /* Build effective file list: [tag_file] + files[] */
    bool had_error = false;
    int  eff_n     = n_files + (tag_file ? 1 : 0);
    int  file_idx  = 0; /* 0-based into effective list */

    if (eff_n == 0) {
        /* No files: read standard input */
        if (load_file("-", NULL) < 0) { cleanup_exit(); return 1; }
        interact("(standard input)", 1, 1, NULL);
        cleanup_exit();
        return had_error ? 1 : 0;
    }

    examine_buf   = NULL;
    prev_filename = NULL;
    examine_line  = -1;

    /*
     * cur_fn_alloc: when set, use this filename for the current iteration
     * instead of the list-derived name (set by :e command).
     */
    char *cur_fn_alloc = NULL;

    while (file_idx >= 0 && file_idx < eff_n) {
        /* Canonical filename for this slot */
        const char *list_fn;
        if (tag_file && file_idx == 0)
            list_fn = tag_file;
        else
            list_fn = files[tag_file ? file_idx - 1 : file_idx];

        const char *fn = cur_fn_alloc ? cur_fn_alloc : list_fn;

        const char *next_fn = NULL;
        if (file_idx + 1 < eff_n) {
            if (tag_file && file_idx + 1 == 0)
                next_fn = tag_file;
            else
                next_fn = files[tag_file ? file_idx : file_idx + 1];
        }

        if (load_file(fn, NULL) < 0) {
            had_error = true;
            free(cur_fn_alloc); cur_fn_alloc = NULL;
            file_idx++;
            continue;
        }

        /* Determine starting position for this file. */
        if (cur_fn_alloc && examine_line >= 0) {
            /* :t — position to the tag line or search for regex */
            if (examine_line > 0) {
                cur_top = examine_line - 1; /* line-number tag (1-based → 0-based) */
            } else {
                /* regex tag: search from the start of the file */
                int tf = search_fwd(-1, false, 1);
                cur_top = (tf >= 0) ? tf : 0;
            }
            examine_line = -1;
        } else if (tag_file && file_idx == 0 && !cur_fn_alloc) {
            /* -t option: initial tag file */
            if (tag_line > 0) {
                cur_top = tag_line - 1;
            } else if (re_valid) {
                /* regex tag: search from the start */
                int tf = search_fwd(-1, false, 1);
                cur_top = (tf >= 0) ? tf : 0;
            } else {
                cur_top = 0;
            }
        } else {
            cur_top = 0;
        }

        jump_target = -1;
        int r = interact(fn, file_idx + 1, eff_n, next_fn);

        /* Record fn as the previously examined file for ':e #' support */
        free(prev_filename);
        prev_filename = strdup(fn);

        free(cur_fn_alloc); cur_fn_alloc = NULL;

        if (r == CMD_QUIT) break;

        if (r == CMD_NEXT) {
            file_idx++;
        } else if (r == CMD_PREV) {
            if (file_idx > 0) file_idx--;
        } else if (r == CMD_JUMP) {
            if (jump_target < 0 && examine_buf) {
                /* :e filename — stay at same file_idx slot, load new file */
                cur_fn_alloc = examine_buf;
                examine_buf  = NULL;
                /* loop back: cur_fn_alloc overrides list_fn next iteration */
            } else {
                /* :n / :p with count, or :t */
                if (jump_target < 0)      jump_target = 0;
                if (jump_target >= eff_n) jump_target = eff_n - 1;
                file_idx = jump_target;
            }
        }

        if (opt_e && at_eof() && file_idx >= eff_n) break;
    }

    free(cur_fn_alloc);
    free(examine_buf);
    free(prev_filename);
    free(tag_file);
    lbuf_clear();
    free(lbuf);
    if (re_valid) regfree(&last_re);

    fputs("\033[?25h", out);
    fflush(out);

    return had_error ? 1 : 0;
}
