/*
 * vinput.c - text input mode in open and visual mode ("Input Mode
 * Commands in vi"): autoindent, erase-columns, ^D ^T ^H ^U ^W ^V NUL,
 * abbreviations, showmatch and wrapmargin.
 */
#include "ex.h"
#include "vi.h"

Str last_insert;

typedef struct IState {
    InputSpec *sp;
    int ai;              /* autoindent in effect */
    int ai_end;          /* end of the autoindent characters on the current line */
    int ins_start;       /* first text input position on the current line */
    int typed_on_line;   /* text input characters typed on the current line */
    Str er;              /* erase-columns (display only) */
    Str *rec;            /* the net input text */
    int repl_avail;      /* R: original characters after the cursor */
    int next_ai;         /* indentation forced for the next line (^ ^D), -1 none */
    int discard_space;   /* wrapmargin: discard the next <space> */
    unsigned long *created;  /* ids of lines created by this command */
    int ncreated;
    int last_char;       /* the previous text input character */
    int literal_mode;    /* replaying text literally */
} IState;

static const char *lt(int l) { return LT(l); }
static int ll(int l) { return LL(l); }

static int indent_cols(const char *s, int len, int upto)
{
    int col = 0, ts = (int)OV(OP_TABSTOP);
    for (int k = 0; k < len && k < upto && (s[k] == ' ' || s[k] == '\t'); k++)
        col = s[k] == '\t' ? col + ts - col % ts : col + 1;
    return col;
}

static int lead_blanks(const char *s, int len)
{
    int k = 0;
    while (k < len && (s[k] == ' ' || s[k] == '\t'))
        k++;
    return k;
}

static void set_line(int l, const char *s, int len) { buf_change(l, s, len); }

static void update_overlay(IState *st, int caret)
{
    static Str ov;
    if (st->er.len == 0 && !caret) {
        ovl_text = NULL;
        return;
    }
    str_clear(&ov);
    str_addn(&ov, lt(cur), (size_t)cur_off);
    if (caret)
        str_addc(&ov, '^');
    str_addn(&ov, st->er.s ? st->er.s : "", st->er.len);
    str_addn(&ov, lt(cur) + cur_off, (size_t)(ll(cur) - cur_off));
    ovl_text = ov.s ? ov.s : "";
    ovl_len = (int)ov.len;
    ovl_line = cur;
}

/* insert raw bytes at the cursor */
static void put_bytes(IState *st, const char *b, int n)
{
    Str s;
    const char *t = lt(cur);
    int len = ll(cur);
    str_init(&s);
    str_addn(&s, t, (size_t)cur_off);
    str_addn(&s, b, (size_t)n);
    str_addn(&s, t + cur_off, (size_t)(len - cur_off));
    set_line(cur, s.s, (int)s.len);
    str_free(&s);
    cur_off += n;
    (void)st;
}

static void del_bytes_before(int n)
{
    Str s;
    const char *t = lt(cur);
    int len = ll(cur);
    str_init(&s);
    str_addn(&s, t, (size_t)(cur_off - n));
    str_addn(&s, t + cur_off, (size_t)(len - cur_off));
    set_line(cur, s.s ? s.s : "", (int)s.len);
    str_free(&s);
    cur_off -= n;
}

static void del_bytes_after(int n)
{
    Str s;
    const char *t = lt(cur);
    int len = ll(cur);
    str_init(&s);
    str_addn(&s, t, (size_t)cur_off);
    str_addn(&s, t + cur_off + n, (size_t)(len - cur_off - n));
    set_line(cur, s.s ? s.s : "", (int)s.len);
    str_free(&s);
}

/* consume erase-columns for a character of width w */
static void eat_erase(IState *st, int w)
{
    int used = 0;
    while (st->er.len > 0 && used < w) {
        int n = ch_len(st->er.s, (int)st->er.len, 0);
        used += ch_width(st->er.s, (int)st->er.len, 0, 0);
        memmove(st->er.s, st->er.s + n, st->er.len - (size_t)n);
        st->er.len -= (size_t)n;
        st->er.s[st->er.len] = 0;
    }
}

static void rec_add(IState *st, const char *b, int n) { str_addn(st->rec, b, (size_t)n); }

static void rec_drop_last(IState *st)
{
    if (st->rec->len > 0 && st->rec->s[st->rec->len - 1] != '\n') {
        int p = ch_prev(st->rec->s, (int)st->rec->len);
        st->rec->len = (size_t)p;
        st->rec->s[p] = 0;
    }
}

static void insert_char(IState *st, const char *b, int n)
{
    int w = ch_width(b, n, 0, col_of(lt(cur), ll(cur), cur_off));
    if (st->er.len)
        eat_erase(st, w);
    else if (st->sp->kind == IN_REPLACE && st->repl_avail > 0 && cur_off < ll(cur)) {
        del_bytes_after(ch_len(lt(cur), ll(cur), cur_off));
        st->repl_avail--;
    }
    put_bytes(st, b, n);
    st->typed_on_line = 1;
    rec_add(st, b, n);
}

static void set_indent(IState *st, int width)
{
    /* replace the autoindent characters with ones of the given width */
    Str s;
    const char *t = lt(cur);
    int len = ll(cur), k = st->ai_end;
    int newlen;
    str_init(&s);
    make_indent(&s, width);
    newlen = (int)s.len;
    str_addn(&s, t + k, (size_t)(len - k));
    set_line(cur, s.s ? s.s : "", (int)s.len);
    str_free(&s);
    cur_off = cur_off - k + newlen;
    st->ai_end = newlen;
    if (st->ins_start <= k)
        st->ins_start = newlen;
}

static void new_line(IState *st)
{
    const char *t = lt(cur);
    int len = ll(cur), o = cur_off, r = cur_off, width = 0;
    Str a, b;
    /* erase-columns after the cursor are discarded */
    str_clear(&st->er);
    if (st->ai) {
        width = indent_cols(t, len, o);
        while (r < len && is_blankc((unsigned char)t[r]))
            r++;
        if (st->next_ai >= 0) {
            width = st->next_ai;
            st->next_ai = -1;
        }
    }
    str_init(&a);
    str_init(&b);
    if (st->ai && o == st->ai_end && !st->typed_on_line && lead_blanks(t, o) == o)
        ; /* only autoindent characters: the line becomes empty */
    else
        str_addn(&a, t, (size_t)o);
    if (st->ai)
        make_indent(&b, width);
    {
        int ind = (int)b.len;
        str_addn(&b, t + r, (size_t)(len - r));
        set_line(cur, a.s ? a.s : "", (int)a.len);
        buf_insert1(cur, b.s ? b.s : "", (int)b.len);
        cur++;
        cur_off = ind;
        st->ai_end = ind;
        st->ins_start = ind;
    }
    st->typed_on_line = 0;
    if (st->sp->kind == IN_REPLACE)
        st->repl_avail = ll(cur) - cur_off;
    st->created = xrealloc(st->created, (size_t)(st->ncreated + 1) * sizeof(unsigned long));
    st->created[st->ncreated++] = lp(cur)->id;
    str_free(&a);
    str_free(&b);
    rec_add(st, "\n", 1);
}

static void ctrl_t(IState *st)
{
    int sw = (int)OV(OP_SHIFTWIDTH);
    int col = col_of(lt(cur), ll(cur), cur_off);
    int target = col + sw - col % sw;
    if (lead_blanks(lt(cur), cur_off) == cur_off) {
        /* only blanks before the cursor: rebuild them */
        Str s;
        const char *t = lt(cur);
        int len = ll(cur), k = cur_off;
        str_init(&s);
        make_indent(&s, target);
        int nl = (int)s.len;
        str_addn(&s, t + k, (size_t)(len - k));
        set_line(cur, s.s ? s.s : "", (int)s.len);
        str_free(&s);
        if (st->ai_end == k || st->ai_end == 0) {
            st->ai_end = nl;
            if (st->ins_start <= k)
                st->ins_start = nl;
        }
        cur_off = nl;
    } else {
        while (col < target) {
            insert_char(st, " ", 1);
            st->rec->len--;   /* recorded as ^T below */
            st->rec->s[st->rec->len] = 0;
            col++;
        }
    }
    rec_add(st, "\024", 1);
}

/* ^D; returns 1 if it was handled specially */
static int ctrl_d(IState *st)
{
    int sw = (int)OV(OP_SHIFTWIDTH);
    const char *t = lt(cur);
    if (st->ai_end > 0 && cur_off == st->ai_end + 1 && (t[st->ai_end] == '0' || t[st->ai_end] == '^') &&
        (st->last_char == '0' || st->last_char == '^')) {
        int keep = st->last_char == '^';
        int width = indent_cols(t, ll(cur), st->ai_end);
        /* remove the 0 or ^ and the autoindent */
        del_bytes_before(1);
        rec_drop_last(st);
        set_indent(st, 0);
        if (keep)
            st->next_ai = width;
        rec_add(st, st->last_char == '^' ? "^\004" : "0\004", 2);
        st->typed_on_line = 0;
        return 1;
    }
    if (st->ai_end > 0 && cur_off == st->ai_end) {
        int width = indent_cols(t, ll(cur), st->ai_end);
        width = width > 0 ? ((width - 1) / sw) * sw : 0;
        set_indent(st, width);
        rec_add(st, "\004", 1);
        return 1;
    }
    if (cur_off == 0)
        return 1;   /* discarded */
    return 0;
}

static void erase_back(IState *st, int to_off)
{
    /* the erased characters become erase-columns */
    Str g;
    if (to_off >= cur_off)
        return;
    str_init(&g);
    str_addn(&g, lt(cur) + to_off, (size_t)(cur_off - to_off));
    str_addn(&g, st->er.s ? st->er.s : "", st->er.len);
    str_free(&st->er);
    st->er = g;
    {
        int n = cur_off - to_off;
        /* drop the same text from the record */
        for (int p = cur_off; p > to_off; p = ch_prev(lt(cur), p))
            rec_drop_last(st);
        del_bytes_before(n);
    }
}

static int abbrev_check(IState *st)
{
    const char *t;
    int start, wstart, o = cur_off;
    if (!abbrevs || o <= st->ins_start)
        return 0;
    t = lt(cur);
    wstart = o;
    while (wstart > st->ins_start) {
        int p = ch_prev(t, wstart);
        if (!is_wordc(ch_wc(t, ll(cur), p)))
            break;
        wstart = p;
    }
    if (wstart == o)
        return 0;
    start = wstart;
    if (o - wstart == ch_len(t, ll(cur), wstart) && wstart > st->ins_start) {
        int p = ch_prev(t, wstart);
        wint_t c = ch_wc(t, ll(cur), p);
        if (!is_wordc(c) && !is_blankc((int)c)) {
            while (start > st->ins_start) {
                int q = ch_prev(t, start);
                wint_t d = ch_wc(t, ll(cur), q);
                if (is_wordc(d) || is_blankc((int)d))
                    break;
                start = q;
            }
        }
    }
    for (Map *m = abbrevs; m; m = m->next) {
        if ((int)strlen(m->lhs) == o - start && strncmp(t + start, m->lhs, (size_t)(o - start)) == 0) {
            int n = o - start;
            char *rhs = xstrdup(m->rhs);
            del_bytes_before(n);
            for (int k = 0; k < n; k++)
                rec_drop_last(st);
            for (const char *r = rhs; *r;) {
                int cl = ch_len(r, (int)strlen(r), 0);
                put_bytes(st, r, cl);
                rec_add(st, r, cl);
                r += cl;
            }
            free(rhs);
            return 1;
        }
    }
    return 0;
}

static void show_match(void)
{
    Pos p = { cur, cur_off > 0 ? ch_prev(lt(cur), cur_off) : 0 };
    char close = lt(cur)[p.o], open = close == ')' ? '(' : close == '}' ? '{' : '[';
    int depth = 1, so = cur_off, sl = cur;
    for (;;) {
        if (p.o > 0)
            p.o = ch_prev(lt(p.l), p.o);
        else {
            if (p.l <= 1 || p.l <= top_line)
                return;
            p.l--;
            p.o = ll(p.l);
            continue;
        }
        if (lt(p.l)[p.o] == close)
            depth++;
        else if (lt(p.l)[p.o] == open && --depth == 0)
            break;
    }
    if (p.l < top_line || p.l > bot_line)
        return;
    {
        int saved_ia = input_mode_active;
        cur = p.l;
        cur_off = p.o;
        input_mode_active = 0;
        vs_refresh();
        input_mode_active = saved_ia;
        key_peek_timeout(1000);
        cur = sl;
        cur_off = so;
    }
}

static void wrap_margin(IState *st, int trigger_blank)
{
    int wm = (int)OV(OP_WRAPMARGIN), col;
    const char *t;
    if (wm <= 0 || mode == MODE_OPEN)
        return;
    t = lt(cur);
    col = col_of(t, ll(cur), cur_off) + prefix_width();
    if (col <= cols - wm)
        return;
    if (trigger_blank) {
        /* discard the blank and the blanks before it; act as a newline */
        int p = cur_off;
        while (p > st->ins_start && is_blankc((unsigned char)t[p - 1]))
            p--;
        while (cur_off > p) {
            del_bytes_before(1);
            rec_drop_last(st);
        }
        new_line(st);
        st->discard_space = 1;
        return;
    }
    {
        /* break at the blanks before the last group of inserted non-blanks */
        int p = cur_off, q;
        while (p > st->ins_start && !is_blankc((unsigned char)t[p - 1]))
            p--;
        if (p <= st->ins_start)
            return;
        q = p;
        while (q > st->ins_start && is_blankc((unsigned char)t[q - 1]))
            q--;
        if (q == p)
            return;
        {
            int after = cur_off - p;   /* characters to move to the new line */
            int save = cur_off;
            /* delete the blanks and split */
            cur_off = p;
            del_bytes_before(p - q);
            (void)save;
            {
                /* the record: replace the blanks with a newline */
                size_t rl = st->rec->len;
                size_t keep = rl >= (size_t)(after + (p - q)) ? rl - (size_t)(after + (p - q)) : 0;
                char *tail = xstrndup(st->rec->s + rl - after, (size_t)after);
                st->rec->len = keep;
                st->rec->s[keep] = 0;
                new_line(st);
                rec_add(st, tail, after);
                free(tail);
            }
            cur_off += after;
            st->typed_on_line = 1;
        }
    }
}

static void literal_text(IState *st, const char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') {
            new_line(st);
            i++;
            continue;
        }
        if (c == 024) {
            ctrl_t(st);
            i++;
            continue;
        }
        if (c == 004) {
            if (!ctrl_d(st))
                insert_char(st, "\004", 1);
            i++;
            continue;
        }
        {
            int cl = ch_len(s, (int)n, (int)i);
            insert_char(st, s + i, cl);
            st->last_char = c;
            i += (size_t)cl;
        }
    }
}

static void finish(IState *st)
{
    /* lines created during input that contain only autoindent become empty,
     * and leading blanks are rewritten minimally */
    for (int i = 0; i < st->ncreated; i++) {
        int l = line_of_id(st->created[i]);
        const char *t;
        int len, k;
        if (!l)
            continue;
        t = lt(l);
        len = ll(l);
        k = lead_blanks(t, len);
        if (k == len && len > 0) {
            if (l == cur)
                cur_off = 0;
            set_line(l, "", 0);
        } else if (k > 0) {
            Str s;
            int w = indent_cols(t, len, k);
            str_init(&s);
            make_indent(&s, w);
            if ((int)s.len != k || memcmp(s.s, t, (size_t)k) != 0) {
                int delta = (int)s.len - k;
                str_addn(&s, t + k, (size_t)(len - k));
                set_line(l, s.s, (int)s.len);
                if (l == cur && cur_off >= k)
                    cur_off += delta;
            }
            str_free(&s);
        }
    }
    /* the first line too, if it was created by o/O and holds only autoindent */
    if (st->sp->kind == IN_OPEN || st->sp->kind == IN_CHANGE) {
        /* handled through created[] */
    }
}

void vi_input(InputSpec *sp, Str *record)
{
    IState st;
    const char *rp = sp->replay;
    int first = 1, done = 0, start_line = cur;
    long start_off = cur_off;
    memset(&st, 0, sizeof st);
    st.sp = sp;
    st.ai = OV(OP_AUTOINDENT) && !silent;
    st.rec = record;
    st.next_ai = -1;
    str_init(&st.er);
    if (sp->erase && sp->erase_len)
        str_addn(&st.er, sp->erase, (size_t)sp->erase_len);
    st.ins_start = cur_off;
    st.ai_end = 0;
    if (sp->kind == IN_CHANGE && st.ai)
        st.ai_end = cur_off;
    if (sp->kind == IN_CHANGE || sp->kind == IN_OPEN) {
        st.created = xmalloc(sizeof(unsigned long));
        st.created[0] = lp(cur)->id;
        st.ncreated = 1;
    }
    if (sp->kind == IN_OPEN && st.ai && sp->ai_line >= 1 && sp->ai_line <= nlines) {
        int w = indent_cols(lt(sp->ai_line), ll(sp->ai_line), ll(sp->ai_line));
        Str s;
        str_init(&s);
        make_indent(&s, w);
        set_line(cur, s.s ? s.s : "", (int)s.len);
        cur_off = (int)s.len;
        st.ai_end = cur_off;
        st.ins_start = cur_off;
        str_free(&s);
    }
    if (sp->kind == IN_REPLACE)
        st.repl_avail = ll(cur) - cur_off;
    input_mode_active = 1;
    mode_indicator = sp->kind == IN_REPLACE ? "REPLACE MODE" : "INPUT MODE";
    while (!done) {
        int k, flags = 0;
        if (rp) {
            if (*rp) {
                const char *nl = rp + strlen(rp);
                literal_text(&st, rp, (size_t)(nl - rp));
                rp = nl;
            }
            k = 033;
        } else {
            update_overlay(&st, 0);
            if (!key_pending())
                vs_refresh();
            k = key_get(2, &flags);
        }
        if (k == K_RESIZE) {
            if (got_winch || got_cont) {
                got_winch = got_cont = 0;
                term_get_size();
                opts_screen_changed();
                if (!term_raw)
                    term_raw_on();
                vs_redraw_all();
            }
            continue;
        }
        if (k == K_EOF) {
            got_hup = 1;
            break;
        }
        if (k == K_INTR || k == 033) {
            if (k == 033)
                abbrev_check(&st);
            break;
        }
        if (k == 0 && first) {
            /* NUL: the previous input text again */
            literal_text(&st, last_insert.s ? last_insert.s : "", last_insert.len);
            first = 0;
            break;
        }
        first = 0;
        if (k == 026 || k == 021) {
            int k2;
            update_overlay(&st, 1);
            vs_refresh();
            k2 = key_get(0, NULL);
            update_overlay(&st, 0);
            if (k2 < 0)
                continue;
            if (k2 == '\n') {
                new_line(&st);
                continue;
            }
            {
                char ch = (char)k2;
                insert_char(&st, &ch, 1);
                st.last_char = k2;
            }
            continue;
        }
        if (k == '\r' || k == '\n') {
            new_line(&st);
            st.last_char = '\n';
            continue;
        }
        if (k == 010 || k == erase_char()) {
            if (cur_off > st.ins_start && cur_off > st.ai_end)
                erase_back(&st, ch_prev(lt(cur), cur_off));
            else if (cur_off > 0 && cur_off <= st.ai_end)
                erase_back(&st, ch_prev(lt(cur), cur_off)), st.ai_end = cur_off,
                    st.ins_start = st.ins_start > cur_off ? cur_off : st.ins_start;
            else
                vi_beep();
            continue;
        }
        if (k == 025 || k == kill_char()) {
            int to = st.ins_start > st.ai_end ? st.ins_start : st.ai_end;
            if (cur_off > to)
                erase_back(&st, to);
            else if (cur_off > 0 && cur_off <= st.ai_end) {
                erase_back(&st, 0);
                st.ai_end = 0;
                st.ins_start = 0;
            } else
                vi_beep();
            continue;
        }
        if (k == 027 || k == werase_char()) {
            int lim = st.ins_start > st.ai_end ? st.ins_start : st.ai_end, p = cur_off;
            const char *t = lt(cur);
            if (p <= lim) {
                vi_beep();
                continue;
            }
            while (p > lim && is_blankc((unsigned char)t[p - 1]))
                p--;
            if (p > lim) {
                int q = ch_prev(t, p);
                int w = is_wordc(ch_wc(t, ll(cur), q));
                while (p > lim) {
                    q = ch_prev(t, p);
                    wint_t c = ch_wc(t, ll(cur), q);
                    if (is_blankc((int)c) || (is_wordc(c) != w))
                        break;
                    p = q;
                }
            }
            erase_back(&st, p);
            continue;
        }
        if (k == 024) {
            ctrl_t(&st);
            continue;
        }
        if (k == 004) {
            if (ctrl_d(&st))
                continue;
        }
        if (k == ' ' && st.discard_space) {
            st.discard_space = 0;
            continue;
        }
        st.discard_space = 0;
        {
            char b[MB_LEN_MAX + 1];
            int n = 1;
            b[0] = (char)k;
            if (k > 0x7f && mb) {
                unsigned char u = (unsigned char)k;
                int need = (u & 0xE0) == 0xC0 ? 1 : (u & 0xF0) == 0xE0 ? 2 : (u & 0xF8) == 0xF0 ? 3 : 0;
                for (int i = 0; i < need; i++) {
                    int k2 = key_get(0, NULL);
                    if (k2 < 0)
                        break;
                    b[n++] = (char)k2;
                }
            }
            if (!is_wordc(ch_wc(b, n, 0)) && !(flags & 0))
                abbrev_check(&st);
            insert_char(&st, b, n);
            st.last_char = k;
            if (OV(OP_SHOWMATCH) && (k == ')' || k == '}' || k == ']'))
                show_match();
            wrap_margin(&st, is_blankc(k));
        }
    }
    /* repeat the input count-1 more times */
    if (sp->count > 1 && st.rec->len > 0) {
        char *t = xstrndup(st.rec->s, st.rec->len);
        size_t n = st.rec->len;
        for (int i = 1; i < sp->count; i++) {
            if (sp->kind == IN_OPEN) {
                if (sp->open_below || 1) {
                    /* start on a new line after the text added so far */
                    new_line(&st);
                    st.rec->len--;
                    st.rec->s[st.rec->len] = 0;
                }
            }
            literal_text(&st, t, n);
        }
        free(t);
    } else if (sp->count > 1 && sp->kind == IN_OPEN) {
        for (int i = 1; i < sp->count; i++) {
            new_line(&st);
            st.rec->len--;
            st.rec->s[st.rec->len] = 0;
        }
    }
    finish(&st);
    str_free(&st.er);
    ovl_text = NULL;
    input_mode_active = 0;
    mode_indicator = NULL;
    /* the cursor rests on the last input character */
    if (cur >= 1 && cur <= nlines) {
        if (st.typed_on_line && cur_off > 0)
            cur_off = ch_prev(lt(cur), cur_off);
        else if (cur_off >= ll(cur))
            cur_off = ll(cur) ? ch_prev(lt(cur), ll(cur)) : 0;
        want_col = col_of(lt(cur), ll(cur), cur_off);
    }
    if (st.rec->len) {
        str_clear(&last_insert);
        str_addn(&last_insert, st.rec->s, st.rec->len);
    }
    free(st.created);
    (void)start_line;
    (void)start_off;
}
