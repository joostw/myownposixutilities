/*
 * screen.c - the visual and open mode display: rendering edit buffer
 * lines (with folding, '@' and '~' lines), the status line, ex output
 * while in visual mode, and the line editor for line-oriented commands.
 */
#include "ex.h"
#include "vi.h"

int vi_active;
int top_line = 1;
int vs_text_rows;
int vs_ex_lines;
int bot_line;             /* last buffer line completely displayed */
static char **phys;       /* what the terminal shows, row by row */
static int phys_rows;
static int full_redraw = 1;
static char *status_text;
static int status_err;
int status_sticky;
static int ex_out_active;     /* ex output is being written over the screen */
static int ex_col;            /* column on the bottom line */
static int ex_pending_nl;
static Str ex_line;        /* the text of the current ex output line */
static int ex_line_err;
int vs_wait_needed;

int prefix_width(void) { return OV(OP_NUMBER) ? 8 : 0; }

void vs_redraw_all(void)
{
    full_redraw = 1;
}

static void phys_alloc(void)
{
    if (phys) {
        for (int i = 0; i < phys_rows; i++)
            free(phys[i]);
        free(phys);
    }
    phys_rows = rows;
    phys = xcalloc((size_t)rows, sizeof(char *));
}

void vs_text_rows_update(void)
{
    if (mode == MODE_OPEN) {
        vs_text_rows = rows - 1;
        return;
    }
    vs_text_rows = (int)OV(OP_WINDOW);
    if (vs_text_rows > rows - 1 || vs_text_rows < 1)
        vs_text_rows = rows - 1;
}

/*
 * Lay out a buffer line.  For every character position the screen row
 * (relative to the line's first row), column and width are stored in
 * lay (if not NULL; indexed by byte offset).  Returns the number of rows.
 * If out is not NULL, the rendered rows are appended to out[] (each a
 * malloc'd string).
 */
const char *ovl_text;
int ovl_len, ovl_line;

int layout_line(int lnum, Lay *lay, char ***out)
{
    const char *s = lnum >= 1 && lnum <= nlines ? LT(lnum) : "";
    int len = lnum >= 1 && lnum <= nlines ? LL(lnum) : 0;
    int list = (int)OV(OP_LIST), row = 0, x = 0, col = 0;
    Str cur_row;
    if (ovl_text && lnum == ovl_line) {
        s = ovl_text;
        len = ovl_len;
    }
    int nrows_alloc = 0;
    char **rowsv = NULL;
    str_init(&cur_row);
    if (OV(OP_NUMBER)) {
        str_fmt(&cur_row, "%6d  ", lnum);
        x = 8;
    }
#define EMIT_ROW()                                                                 \
    do {                                                                           \
        if (out) {                                                                 \
            rowsv = xrealloc(rowsv, (size_t)(nrows_alloc + 1) * sizeof(char *)); \
            rowsv[nrows_alloc++] = str_detach(&cur_row);                           \
        } else                                                                     \
            str_clear(&cur_row);                                                   \
        str_init(&cur_row);                                                        \
    } while (0)
    for (int p = 0; p < len;) {
        int n = ch_len(s, len, p);
        int w;
        Str piece;
        str_init(&piece);
        w = char_disp(&piece, s, len, p, col, list);
        if (!list && s[p] == '\t') {
            int rem = cols - x;
            int dw = w < rem ? w : rem;
            if (lay) {
                lay[p].row = row;
                lay[p].x = x;
                lay[p].w = dw;
            }
            for (int k = 0; k < dw; k++)
                str_addc(&cur_row, ' ');
            x += dw;
            col += w;
        } else {
            if (x + w > cols && x > 0) {
                EMIT_ROW();
                row++;
                x = 0;
            }
            if (lay) {
                lay[p].row = row;
                lay[p].x = x;
                lay[p].w = w;
            }
            if (w <= cols)
                str_addn(&cur_row, piece.s ? piece.s : "", piece.len);
            x += w;
            col += w;
        }
        str_free(&piece);
        for (int k = 1; k < n && lay; k++)
            lay[p + k] = lay[p];
        p += n;
        if (x >= cols && p < len) {
            EMIT_ROW();
            row++;
            x = 0;
        }
    }
    if (list) {
        if (x + 1 > cols) {
            EMIT_ROW();
            row++;
            x = 0;
        }
        str_addc(&cur_row, '$');
        x++;
    }
    if (lay) {
        /* position just past the end of the line */
        if (x >= cols) {
            lay[len].row = row + 1;
            lay[len].x = 0;
        } else {
            lay[len].row = row;
            lay[len].x = x;
        }
        lay[len].w = 1;
    }
    EMIT_ROW();
    str_free(&cur_row);
    if (out)
        *out = rowsv;
    return row + 1;
#undef EMIT_ROW
}

int line_rows(int lnum)
{
    return layout_line(lnum, NULL, NULL);
}

/* make sure the current line is displayed */
static void fix_top(void)
{
    int tr = vs_text_rows, r, l;
    if (nlines == 0) {
        top_line = 1;
        return;
    }
    if (cur < 1)
        cur = 1;
    if (mode == MODE_OPEN) {
        top_line = cur;
        return;
    }
    if (top_line < 1)
        top_line = 1;
    if (top_line > nlines)
        top_line = nlines;
    if (cur < top_line) {
        if (top_line - cur <= tr / 2) {
            top_line = cur;
            return;
        }
        goto center;
    }
    /* rows from top_line through cur */
    r = 0;
    for (l = top_line; l <= cur && r <= tr; l++)
        r += line_rows(l);
    if (r <= tr)
        return;
    /* below the screen: scroll if close, otherwise center */
    {
        int dist = cur - bot_line;
        if (dist > 0 && dist <= tr / 2) {
            /* put cur on the last rows */
            int need = line_rows(cur);
            l = cur;
            while (l > 1 && need + line_rows(l - 1) <= tr) {
                l--;
                need += line_rows(l);
            }
            top_line = l;
            return;
        }
    }
center:
    vs_center(cur);
}

/* place line l in the middle of the display */
void vs_center(int l)
{
    int tr = vs_text_rows, need, t;
    if (l < 1)
        l = 1;
    need = line_rows(l);
    t = l;
    while (t > 1) {
        int r = line_rows(t - 1);
        if (need + r > (tr + 1) / 2 + (line_rows(l) - 1))
            break;
        need += r;
        t--;
    }
    top_line = t;
}

/* place line l at the bottom of the display */
void vs_bottom(int l)
{
    int tr = vs_text_rows, need, t;
    if (l < 1)
        l = 1;
    need = line_rows(l);
    t = l;
    while (t > 1 && need + line_rows(t - 1) <= tr) {
        t--;
        need += line_rows(t);
    }
    top_line = t;
}

/* the line displayed at the bottom of a screen starting at t */
int vs_last_line_from(int t)
{
    int r = 0, l = t, last = t;
    for (; l <= nlines; l++) {
        int n = line_rows(l);
        if (r + n > vs_text_rows)
            break;
        r += n;
        last = l;
    }
    return last;
}

static void draw_row(int r, const char *text, int standout)
{
    if (!full_redraw && phys[r] && strcmp(phys[r], text) == 0 && !standout)
        return;
    tmove(r, 0);
    if (standout)
        tstandout(1);
    tputs_(text);
    if (standout)
        tstandout(0);
    tclear_eol();
    free(phys[r]);
    phys[r] = standout ? xstrdup("\001") : xstrdup(text);
}

void vs_message(const char *text, int error)
{
    if (ex_out_active) {
        out_write(text, strlen(text));
        out_write("\n", 1);
        ex_line_err = error;
        return;
    }
    free(status_text);
    status_text = xstrdup(text);
    status_err = error;
    status_sticky = 1;
}

void vs_clear_message(void)
{
    free(status_text);
    status_text = NULL;
    status_err = 0;
}

int cursor_row, cursor_col;

/* compute the screen position of the cursor */
static void place_cursor(int text_start_row)
{
    Lay *lay;
    int len, off, row, x;
    if (nlines == 0 || cur < 1) {
        cursor_row = text_start_row;
        cursor_col = 0;
        return;
    }
    len = (ovl_text && ovl_line == cur) ? ovl_len : LL(cur);
    lay = xmalloc((size_t)(len + 1) * sizeof(Lay));
    layout_line(cur, lay, NULL);
    off = cur_off;
    if (off > len)
        off = len;
    if (input_mode_active) {
        row = lay[off].row;
        x = lay[off].x;
        if (off == len && OV(OP_LIST) && len >= 0) {
            /* rest on the '$' */
            row = lay[len].row;
            x = lay[len].x;
            if (x > 0)
                x--;
            if (len == 0)
                x = prefix_width();
        }
    } else {
        if (len == 0) {
            row = 0;
            x = prefix_width();
        } else {
            if (off >= len)
                off = ch_prev((ovl_text && ovl_line == cur) ? ovl_text : LT(cur), len);
            row = lay[off].row;
            x = lay[off].x + (lay[off].w > 0 ? lay[off].w - 1 : 0);
        }
    }
    free(lay);
    if (x >= cols)
        x = cols - 1;
    cursor_row = text_start_row + row;
    cursor_col = x;
}

const char *mode_indicator;

void vs_refresh(void)
{
    int r = 0, l, cur_row_start = 0, base_row = 0;
    if (!vi_active)
        return;
    if (!phys || phys_rows != rows)
        phys_alloc(), full_redraw = 1;
    vs_text_rows_update();
    if (full_redraw)
        tclear();
    fix_top();
    if (mode == MODE_OPEN) {
        /* the current line on the rows above the status line */
        int n = nlines ? line_rows(cur) : 1;
        base_row = rows - 1 - n;
        if (base_row < 0)
            base_row = 0;
        for (r = 0; r < base_row; r++)
            draw_row(r, "", 0);
        r = base_row;
    }
    bot_line = top_line - 1;
    for (l = top_line; l <= nlines && r < (mode == MODE_OPEN ? rows - 1 : vs_text_rows); l++) {
        char **rv;
        int n = layout_line(l, NULL, &rv);
        if (l == cur)
            cur_row_start = r;
        if (r + n > vs_text_rows + (mode == MODE_OPEN ? rows : 0) && l != cur) {
            for (; r < vs_text_rows; r++)
                draw_row(r, "@", 0);
            for (int k = 0; k < n; k++)
                free(rv[k]);
            free(rv);
            break;
        }
        for (int k = 0; k < n; k++) {
            if (r < rows - 1)
                draw_row(r, rv[k], 0);
            r++;
            free(rv[k]);
        }
        free(rv);
        bot_line = l;
        if (mode == MODE_OPEN)
            break;
    }
    if (nlines == 0) {
        draw_row(r, OV(OP_NUMBER) ? "" : "", 0);
        cur_row_start = r;
        r++;
    }
    for (; r < vs_text_rows; r++)
        draw_row(r, "~", 0);
    for (; r < rows - 1; r++)
        draw_row(r, "", 0);
    /* status line */
    if (status_text) {
        /* keep it on one line */
        Str t;
        int w = 0;
        str_init(&t);
        for (const char *p = status_text; *p && w < cols - 1;) {
            int n = ch_len(p, (int)strlen(p), 0);
            str_addn(&t, p, (size_t)n);
            w++;
            p += n;
        }
        draw_row(rows - 1, t.s ? t.s : "", status_err);
        str_free(&t);
    }
    else if (OV(OP_SHOWMODE) && mode_indicator) {
        Str b;
        str_init(&b);
        for (int k = (int)strlen(mode_indicator); k < cols - 1; k++)
            str_addc(&b, ' ');
        str_adds(&b, mode_indicator);
        draw_row(rows - 1, b.s, 0);
        str_free(&b);
    } else
        draw_row(rows - 1, "", 0);
    full_redraw = 0;
    place_cursor(cur_row_start);
    tmove(cursor_row, cursor_col);
    tflush();
}

/* ---------- ex output written over the screen ---------- */



void vs_ex_begin(void)
{
    str_clear(&ex_line);
    ex_line_err = 0;
    ex_out_active = 1;
    vs_ex_lines = 0;
    ex_col = 0;
    ex_pending_nl = 0;
    vs_wait_needed = 0;
}

static void ex_newline(void)
{
    str_clear(&ex_line);
    ex_line_err = 0;
    tputs_("\r\n");
    vs_ex_lines++;
    ex_col = 0;
    full_redraw = 1;
    vs_wait_needed = 1;
}

void vs_ex_output(const char *s, size_t n)
{
    if (!ex_out_active) {
        /* output outside an ex command (e.g. from a vi command) */
        Str b;
        str_init(&b);
        str_addn(&b, s, n);
        while (b.len && b.s[b.len - 1] == '\n')
            b.s[--b.len] = 0;
        if (b.len)
            vs_message(b.s, 0);
        str_free(&b);
        return;
    }
    if (vs_ex_lines == 0 && ex_col == 0 && !ex_pending_nl) {
        /* first output: on the status line */
        vs_clear_message();
        tmove(rows - 1, 0);
        tclear_eol();
        free(phys[rows - 1]);
        phys[rows - 1] = NULL;
        vs_ex_lines = 1;
    }
    for (size_t i = 0; i < n; i++) {
        if (ex_pending_nl) {
            ex_newline();
            ex_pending_nl = 0;
        }
        if (s[i] == '\n') {
            ex_pending_nl = 1;
            continue;
        }
        str_addc(&ex_line, s[i]);
        tputn(s + i, 1);
        ex_col++;
        if (ex_col >= cols) {
            ex_col = 0;
            vs_wait_needed = 1;
            full_redraw = 1;
        }
    }
    tflush();
}

void vs_ex_end(int wait_key)
{
    ex_out_active = 0;
    (void)wait_key;
    if (!vs_wait_needed && vs_ex_lines <= 1 && ex_line.len) {
        /* a single line of output stays on the status line */
        free(status_text);
        status_text = xstrdup(ex_line.s);
        status_err = ex_line_err;
        status_sticky = 1;
    }
    str_clear(&ex_line);
}

/* ---------- the bottom-line editor ---------- */

static void show_input_line(const char *prompt, Str *b)
{
    Str d;
    int start = 0, w;
    str_init(&d);
    str_adds(&d, prompt);
    for (size_t i = 0; i < b->len;) {
        int n = ch_len(b->s, (int)b->len, (int)i);
        char_disp(&d, b->s, (int)b->len, (int)i, 0, 0);
        i += (size_t)n;
    }
    w = (int)d.len;   /* approximate width */
    if (w >= cols)
        start = w - cols + 1;
    if (ex_out_active && vs_ex_lines > 0 && ex_col > 0) {
        ex_newline();
    }
    tmove(rows - 1, 0);
    tputs_(d.s + start);
    tclear_eol();
    tflush();
    free(phys[rows - 1]);
    phys[rows - 1] = NULL;
    str_free(&d);
}

/*
 * Read a line on the last line of the screen.  Returns NULL if the input
 * was cancelled (interrupt, or erasing past the prompt).  textmode: an ex
 * text input line (no cancellation by erase).
 */
char *vs_getline(const char *prompt, int textmode)
{
    Str b;
    int k;
    str_init(&b);
    if (ex_out_active && ex_pending_nl) {
        ex_newline();
        ex_pending_nl = 0;
    }
    for (;;) {
        show_input_line(prompt, &b);
        k = key_get(0, NULL);
        if (k == K_RESIZE) {
            got_winch = got_cont = 0;
            continue;
        }
        if (k == K_INTR || k == K_EOF) {
            str_free(&b);
            if (k == K_EOF)
                got_hup = 1;
            if (textmode)
                return xstrdup("\003");
            return NULL;
        }
        if (k == '\r' || k == '\n' || k == 033) {
            if (ex_out_active)
                ex_pending_nl = 1, vs_ex_lines++;
            return str_detach(&b);
        }
        if (k == 026 || k == 021) {   /* ^V ^Q: literal next */
            int k2;
            show_input_line(prompt, &b);
            tputs_("^");
            tflush();
            k2 = key_get(0, NULL);
            if (k2 >= 0)
                str_addc(&b, (char)k2);
            continue;
        }
        if (k == 010 || k == erase_char()) {
            if (b.len == 0) {
                if (textmode)
                    continue;
                str_free(&b);
                return NULL;
            }
            b.len = (size_t)ch_prev(b.s, (int)b.len);
            b.s[b.len] = 0;
            continue;
        }
        if (k == 025 || k == kill_char()) {
            str_clear(&b);
            continue;
        }
        if (k == 027 || k == werase_char()) {
            while (b.len && is_blankc((unsigned char)b.s[b.len - 1]))
                b.len--;
            if (b.len && is_wordc((unsigned char)b.s[b.len - 1]))
                while (b.len && is_wordc((unsigned char)b.s[b.len - 1]))
                    b.len--;
            else
                while (b.len && !is_wordc((unsigned char)b.s[b.len - 1]) &&
                       !is_blankc((unsigned char)b.s[b.len - 1]))
                    b.len--;
            if (b.s)
                b.s[b.len] = 0;
            continue;
        }
        str_addc(&b, (char)k);
    }
}

/* confirmation for s///c while in visual mode */
int vs_confirm(const char *line1, const char *line2)
{
    char *ans;
    int r;
    out_write(line1, strlen(line1));
    out_write("\n", 1);
    out_write(line2, strlen(line2));
    out_write("\n", 1);
    ans = vs_getline("", 1);
    r = ans && (ans[0] == 'y' || ans[0] == 'Y');
    if (ans && ans[0] == '\003')
        intr = 1;
    free(ans);
    return r;
}

/* ---------- suspending the screen ---------- */

void vs_suspend(void)
{
    if (!vi_active)
        return;
    tmove(rows - 1, 0);
    tclear_eol();
    tflush();
    term_raw_off();
}

void vs_resume(void)
{
    if (!vi_active)
        return;
    term_raw_on();
    full_redraw = 1;
    vs_wait_needed = 1;
}

void vs_wait_key(int allow_colon, int *colon)
{
    int k;
    if (colon)
        *colon = 0;
    if (ex_pending_nl || ex_col > 0) {
        tputs_("\r\n");
        ex_pending_nl = 0;
        ex_col = 0;
    }
    tmove(rows - 1, 0);
    tstandout(1);
    tputs_(allow_colon && cols >= 60 ? "Press any key to continue [: to enter more ex commands]: "
                                     : cols >= 30 ? "Press any key to continue: " : "[Hit any key]");
    tstandout(0);
    tclear_eol();
    tflush();
    for (;;) {
        k = key_get(0, NULL);
        if (k == K_RESIZE) {
            got_winch = 0;
            continue;
        }
        break;
    }
    if (k == K_EOF)
        got_hup = 1;
    if (allow_colon && k == ':' && colon)
        *colon = 1;
    full_redraw = 1;
    vs_wait_needed = 0;
}
