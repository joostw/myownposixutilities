/*
 * vi.c - visual and open mode commands (XCU vi "Command Descriptions in vi").
 */
#include "ex.h"
#include "vi.h"

static int vi_quit;
static int last_find_cmd, last_find_char;
static int scroll_count;
static unsigned long u_id;
static char *u_text;
static int u_len;
static int last_at_buf;
static int replaying;            /* executing '.' */
int input_mode_active;

typedef struct VCmd {
    int count, has_count;
    int reg;
    int cmd, arg;            /* command character and argument (f c, r c, m c ...) */
    int op;                  /* operator when reading a motion */
    int mcount, has_mcount;
    int mcmd, marg;
    char *mtext;             /* search text for / and ? motions */
    char *bangcmd;           /* shell command for ! */
} VCmd;

/* the last repeatable command, for '.' */
static VCmd rep;
static int rep_valid;
static Str rep_text;
static int rep_has_text;

typedef struct MRes {
    Pos to;
    int linemode;
    Pos ra, rb;              /* region [ra, rb) in character mode, lines ra.l..rb.l in line mode */
    int colrule;             /* how the column is set when not used as a motion */
    int ctx;                 /* set the context mark */
    int forward;             /* motion toward the end of the buffer */
} MRes;

enum { COL_KEEP, COL_NONBLANK, COL_TO, COL_EOL };

void vi_beep(void)
{
    tputs_("\a");
    tflush();
}

void verr(const char *m)
{
    if (m)
        vs_message(m, 1);
    vi_beep();
    ex_err_silent();
}

/* ---------- position helpers ---------- */

static const char *lt(int l) { return LT(l); }
static int ll(int l) { return LL(l); }

static int nextoff(int l, int o) { return o + ch_len(lt(l), ll(l), o); }
static int prevoff(int l, int o) { return ch_prev(lt(l), o); }

static int lastoff(int l)
{
    int len = ll(l);
    return len ? ch_prev(lt(l), len) : 0;
}

static int poscmp(Pos a, Pos b)
{
    if (a.l != b.l)
        return a.l < b.l ? -1 : 1;
    return a.o < b.o ? -1 : a.o > b.o;
}

static void set_cursor(int l, int o)
{
    cur = l;
    cur_off = o;
    if (l >= 1 && l <= nlines)
        want_col = col_of(lt(l), ll(l), o);
    else
        want_col = 0;
}

/* put the cursor on the line at the remembered column */
static void cursor_to_want(void)
{
    if (cur < 1 || cur > nlines) {
        cur_off = 0;
        return;
    }
    if (want_col < 0)
        cur_off = lastoff(cur);
    else
        cur_off = off_of_col(lt(cur), ll(cur), want_col);
}

static int is_blank_line(int l)
{
    const char *s = lt(l);
    for (int i = 0; i < ll(l); i++)
        if (s[i] != ' ' && s[i] != '\t')
            return 0;
    return 1;
}

enum { C_EOL, C_BLANK, C_WORD, C_PUNCT, C_BLANKLINE };

static int wclass(Pos p, int big)
{
    const char *s;
    int len;
    if (is_blank_line(p.l))
        return C_BLANKLINE;
    s = lt(p.l);
    len = ll(p.l);
    if (p.o >= len)
        return C_EOL;
    if (s[p.o] == ' ' || s[p.o] == '\t')
        return C_BLANK;
    if (big)
        return C_WORD;
    return is_wordc(ch_wc(s, len, p.o)) ? C_WORD : C_PUNCT;
}

/* move one character position forward (EOL positions included) */
static int pos_next(Pos *p)
{
    if (p->o < ll(p->l)) {
        p->o = nextoff(p->l, p->o);
        return 1;
    }
    if (p->l >= nlines)
        return 0;
    p->l++;
    p->o = 0;
    return 1;
}

static int pos_prev(Pos *p)
{
    if (p->o > 0) {
        p->o = prevoff(p->l, p->o);
        return 1;
    }
    if (p->l <= 1)
        return 0;
    p->l--;
    p->o = ll(p->l);
    return 1;
}

static Pos buf_last_char(void)
{
    Pos p;
    p.l = nlines;
    p.o = lastoff(nlines);
    return p;
}

/* start of the next word; *end set if there is none */
static Pos word_fwd(Pos p, int big, int *end)
{
    int c = wclass(p, big), len;
    *end = 0;
    if (c == C_BLANKLINE) {
        while (p.l <= nlines && is_blank_line(p.l))
            p.l++;
        if (p.l > nlines) {
            *end = 1;
            return buf_last_char();
        }
        p.o = first_nonblank(p.l);
        return p;
    }
    len = ll(p.l);
    if (c == C_WORD || c == C_PUNCT)
        while (p.o < len && wclass(p, big) == c)
            p.o = nextoff(p.l, p.o);
    while (p.o < len && wclass(p, big) == C_BLANK)
        p.o = nextoff(p.l, p.o);
    if (p.o < len)
        return p;
    if (p.l >= nlines) {
        *end = 1;
        return buf_last_char();
    }
    p.l++;
    p.o = 0;
    if (is_blank_line(p.l))
        return p;
    p.o = first_nonblank(p.l);
    return p;
}

/* end of the current or next word */
static Pos word_end(Pos p, int big, int *end)
{
    int c;
    *end = 0;
    if (!pos_next(&p)) {
        *end = 1;
        return p;
    }
    while ((c = wclass(p, big)) == C_BLANK || c == C_EOL || c == C_BLANKLINE) {
        if (!pos_next(&p)) {
            *end = 1;
            return buf_last_char();
        }
    }
    for (;;) {
        Pos q = p;
        if (!pos_next(&q) || q.l != p.l || wclass(q, big) != c)
            break;
        p = q;
    }
    return p;
}

static Pos word_back(Pos p, int big, int *begin)
{
    int c;
    *begin = 0;
    if (!pos_prev(&p)) {
        *begin = 1;
        return p;
    }
    for (;;) {
        c = wclass(p, big);
        if (c == C_BLANKLINE) {
            while (p.l > 1 && is_blank_line(p.l - 1))
                p.l--;
            p.o = 0;
            return p;
        }
        if (c == C_BLANK || c == C_EOL) {
            if (!pos_prev(&p)) {
                *begin = 1;
                return p;
            }
            continue;
        }
        break;
    }
    while (p.o > 0) {
        Pos q = p;
        q.o = prevoff(p.l, p.o);
        if (wclass(q, big) != c)
            break;
        p = q;
    }
    return p;
}

/* ---------- section, paragraph and sentence boundaries ---------- */

static int macro_match(const char *s, int len, const char *pairs)
{
    size_t n = strlen(pairs);
    if (len < 2 || s[0] != '.')
        return 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        if (s[1] != pairs[i])
            continue;
        if (pairs[i + 1] == ' ') {
            if (len == 2 || s[2] == ' ' || s[2] == '\t')
                return 1;
        } else if (len >= 3 && s[2] == pairs[i + 1])
            return 1;
    }
    return 0;
}

static int section_line(int l)
{
    const char *s = lt(l);
    int len = ll(l);
    if (len > 0 && (s[0] == '\f' || s[0] == '{'))
        return 1;
    return macro_match(s, len, OS(OP_SECTIONS) ? OS(OP_SECTIONS) : "");
}

static int para_line(int l, int *in_blank_group)
{
    if (section_line(l))
        return 1;
    if (macro_match(lt(l), ll(l), OS(OP_PARAGRAPHS) ? OS(OP_PARAGRAPHS) : ""))
        return 1;
    if (is_blank_line(l)) {
        int first = l == 1 || !is_blank_line(l - 1);
        if (in_blank_group)
            *in_blank_group = 1;
        return first;
    }
    return 0;
}

/* kind: 0 section, 1 paragraph */
static int boundary_line(int l, int kind)
{
    if (l == 1 || l == nlines)
        return 1;
    return kind == 0 ? section_line(l) : para_line(l, NULL);
}

/* is position p the start of a sentence? */
static int sentence_start(Pos p)
{
    const char *s = lt(p.l);
    int len = ll(p.l);
    Pos q;
    int crossed_para = 0, two_spaces = 0, nsp = 0, eol = 0;
    if (p.o == 0 && (p.l == 1 || para_line(p.l, NULL)))
        return 1;
    if (p.o >= len || is_blankc((unsigned char)s[p.o]))
        return 0;
    /* look back over blanks and line ends */
    q = p;
    for (;;) {
        Pos r = q;
        if (!pos_prev(&r))
            return 1;     /* first non-blank of the buffer */
        if (r.o >= ll(r.l)) {
            /* crossed a line end */
            eol = 1;
            if (para_line(r.l, NULL) && is_blank_line(r.l))
                crossed_para = 1;
            if (para_line(q.l, NULL) && q.o == 0 && q.l != p.l)
                crossed_para = 1;
            q = r;
            if (ll(r.l) == 0 || is_blank_line(r.l)) {
                crossed_para = 1;
                continue;
            }
            continue;
        }
        if (is_blankc((unsigned char)lt(r.l)[r.o])) {
            if (!eol)
                nsp++;
            q = r;
            continue;
        }
        q = r;
        break;
    }
    if (crossed_para)
        return 1;
    if (para_line(q.l, NULL) && !is_blank_line(q.l) && q.l != p.l) {
        /* the previous non-blank is on a paragraph boundary (macro) line */
        return 1;
    }
    two_spaces = eol || nsp >= 2;
    if (!two_spaces)
        return 0;
    /* skip closing characters */
    {
        const char *t = lt(q.l);
        int o = q.o;
        while (o >= 0 && strchr(")]\"'", t[o]) && o > 0)
            o--;
        if (strchr(".!?", t[o]))
            return 1;
    }
    return 0;
}

/* ---------- searching ---------- */

/* the last match on line l starting in [lo, hi); -1 if none */
static int last_match(Regex *re, int l, int lo, int hi, int *mlen)
{
    Match m;
    int pos = lo, best = -1;
    while (pos <= ll(l) && re_exec(re, lt(l), ll(l), pos, 0, &m)) {
        if (m.so[0] >= hi)
            break;
        best = m.so[0];
        *mlen = m.eo[0] - m.so[0];
        if (m.so[0] >= ll(l))
            break;
        pos = nextoff(l, m.so[0]);
    }
    return best;
}

/* the first match on line l starting at or after lo and before hi */
static int first_match(Regex *re, int l, int lo, int hi, int *mlen)
{
    Match m;
    if (lo > ll(l))
        return -1;
    if (!re_exec(re, lt(l), ll(l), lo, 0, &m) || m.so[0] >= hi)
        return -1;
    *mlen = m.eo[0] - m.so[0];
    return m.so[0];
}

/* find the next match of re after (or before) position p */
static int search_from(Regex *re, Pos p, int dir, int wrap, Pos *found, int *matchlen)
{
    int l, o;
    if (nlines == 0)
        return 0;
    if (dir > 0) {
        int start = p.o < ll(p.l) ? nextoff(p.l, p.o) : ll(p.l) + 1;
        if ((o = first_match(re, p.l, start, INT_MAX, matchlen)) >= 0)
            goto hit_cur;
        for (l = p.l + 1; l <= nlines; l++) {
            if (intr) {
                intr = 0;
                verr("Interrupted");
            }
            if ((o = first_match(re, l, 0, INT_MAX, matchlen)) >= 0)
                goto hit;
        }
        if (!wrap)
            return 0;
        for (l = 1; l < p.l; l++) {
            if (intr) {
                intr = 0;
                verr("Interrupted");
            }
            if ((o = first_match(re, l, 0, INT_MAX, matchlen)) >= 0)
                goto hit;
        }
        if ((o = first_match(re, p.l, 0, p.o + 1, matchlen)) >= 0)
            goto hit_cur;
        return 0;
    }
    if ((o = last_match(re, p.l, 0, p.o, matchlen)) >= 0)
        goto hit_cur;
    for (l = p.l - 1; l >= 1; l--) {
        if (intr) {
            intr = 0;
            verr("Interrupted");
        }
        if ((o = last_match(re, l, 0, INT_MAX, matchlen)) >= 0)
            goto hit;
    }
    if (!wrap)
        return 0;
    for (l = nlines; l > p.l; l--) {
        if (intr) {
            intr = 0;
            verr("Interrupted");
        }
        if ((o = last_match(re, l, 0, INT_MAX, matchlen)) >= 0)
            goto hit;
    }
    if ((o = last_match(re, p.l, p.o, INT_MAX, matchlen)) >= 0)
        goto hit_cur;
    return 0;
hit_cur:
    l = p.l;
hit:
    found->l = l;
    found->o = o;
    return 1;
}

/* ---------- motions ---------- */

static int getkey_arg(void)
{
    int k = key_get(0, NULL);
    if (k == K_INTR || k == K_EOF)
        verr(NULL);
    if (k == K_RESIZE)
        return getkey_arg();
    return k;
}

static void find_char(int cmd, int ch, int cnt, Pos from, Pos *to)
{
    const char *s = lt(from.l);
    int len = ll(from.l), o = from.o, n = 0;
    if (cmd == 'f' || cmd == 't') {
        int p = o;
        while (n < cnt) {
            if (p >= len)
                verr(NULL);
            p = nextoff(from.l, p);
            if (p >= len)
                verr(NULL);
            if ((unsigned char)s[p] == ch && ch_len(s, len, p) == 1)
                n++;
            else if (ch > 0x7f && ch_wc(s, len, p) == (wint_t)ch)
                n++;
        }
        to->l = from.l;
        to->o = cmd == 't' ? prevoff(from.l, p) : p;
        if (cmd == 't' && to->o < o)
            to->o = o;
    } else {
        int p = o;
        while (n < cnt) {
            if (p <= 0)
                verr(NULL);
            p = prevoff(from.l, p);
            if ((unsigned char)s[p] == ch)
                n++;
        }
        to->l = from.l;
        to->o = cmd == 'T' ? nextoff(from.l, p) : p;
    }
}

static int match_bracket(Pos from, Pos *to)
{
    const char *s = lt(from.l);
    int len = ll(from.l), o = from.o, dir, depth = 1;
    char open, close, c;
    static const char pairs[] = "()[]{}";
    while (o < len && !strchr(pairs, s[o]))
        o++;
    if (o >= len)
        return 0;
    c = s[o];
    {
        const char *q = strchr(pairs, c);
        int i = (int)(q - pairs);
        open = pairs[i & ~1];
        close = pairs[i | 1];
        dir = (i & 1) ? -1 : 1;
    }
    {
        Pos p = { from.l, o };
        for (;;) {
            if (dir > 0) {
                if (!pos_next(&p))
                    return 0;
            } else if (!pos_prev(&p))
                return 0;
            if (p.o >= ll(p.l))
                continue;
            c = lt(p.l)[p.o];
            if (c == open)
                depth += dir > 0 ? 1 : -1;
            else if (c == close)
                depth += dir > 0 ? -1 : 1;
            if (depth == 0) {
                *to = p;
                return 1;
            }
        }
    }
}

static int fnb(int l) { return first_nonblank(l); }

/* region helpers: characters from a (inclusive) to b (exclusive) */
static void region_chars(MRes *m, Pos a, Pos b)
{
    if (poscmp(a, b) > 0) {
        Pos t = a;
        a = b;
        b = t;
    }
    m->ra = a;
    m->rb = b;
    m->linemode = 0;
}

static Pos pos_after(Pos p)
{
    if (p.o < ll(p.l))
        p.o = nextoff(p.l, p.o);
    return p;
}

static void region_lines(MRes *m, int a, int b)
{
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    m->ra.l = a;
    m->ra.o = 0;
    m->rb.l = b;
    m->rb.o = ll(b);
    m->linemode = 1;
}

static char *read_search_line(int prompt)
{
    char p[2] = { (char)prompt, 0 };
    char *s = vs_getline(p, 0);
    return s;
}

/*
 * Parse and execute a / or ? search line.  Returns the match position
 * (after any offset) and whether an offset was given.
 */
static int do_search(int dir, const char *text, Pos from, Pos *res, int *has_offset, int *zcmd,
                     int as_motion)
{
    const char *p = text;
    Pos at = from, found;
    int mlen = 0, first = 1, delim = dir > 0 ? '/' : '?';
    *has_offset = 0;
    if (zcmd)
        *zcmd = 0;
    if (!*p) {
        if (!last_re)
            verr("No previous regular expression");
        if (!search_from(last_re, at, dir, (int)OV(OP_WRAPSCAN), &found, &mlen)) {
            verr("Pattern not found");
        }
        *res = found;
        search_dir = dir;
        return 1;
    }
    for (;;) {
        Regex *re;
        if (!first) {
            p = skipbl(p);
            if (*p != ';')
                break;
            p = skipbl(p + 1);
            if (*p != '/' && *p != '?')
                verr("Invalid search command");
            delim = *p;
            dir = delim == '/' ? 1 : -1;
            p++;
        }
        {
            const char *e = re_skip(p, delim);
            if (e == p) {
                if (!last_re)
                    verr("No previous regular expression");
                re = re_ref(last_re);
            } else {
                char *err = NULL;
                re = re_compile(p, delim, NULL, (int)OV(OP_MAGIC), &err);
                if (!re)
                    verr(err ? err : "Invalid regular expression");
                set_last_re(re);
            }
            p = e;
            if ((unsigned char)*p == delim)
                p++;
            else if (*p) {
                re_free(re);
                verr("Invalid search");
            }
        }
        if (!search_from(re, at, dir, (int)OV(OP_WRAPSCAN), &found, &mlen)) {
            re_free(re);
            verr("Pattern not found");
        }
        re_free(re);
        at = found;
        first = 0;
        search_dir = dir;
        if (!*p)
            break;
        if (*skipbl(p) == ';')
            continue;
        break;
    }
    *res = at;
    p = skipbl(p);
    if (*p == 'z' && !as_motion) {
        if (zcmd)
            *zcmd = 1;
        return 1;
    }
    if (*p) {
        /* address offset */
        long off = 0;
        int got = 0;
        while (*p) {
            p = skipbl(p);
            if (*p == '+' || *p == '-') {
                int sign = *p == '+' ? 1 : -1;
                long n = 1;
                p++;
                if (isdigit((unsigned char)*p)) {
                    n = strtol(p, (char **)&p, 10);
                }
                off += sign * n;
                got = 1;
            } else if (isdigit((unsigned char)*p)) {
                off += strtol(p, (char **)&p, 10);
                got = 1;
            } else if (*p)
                verr("Invalid search offset");
        }
        if (got) {
            long nl = at.l + off;
            if (nl < 1 || nl > nlines)
                verr("Search offset past the end of the file");
            res->l = (int)nl;
            res->o = fnb((int)nl);
            *has_offset = 1;
        }
    }
    return 1;
}

static void motion(VCmd *c, int key, int cnt, int has_cnt, MRes *m);

static void search_motion(VCmd *c, int dir, const char *text, MRes *m)
{
    Pos from = { cur, cur_off }, to;
    int off, z;
    do_search(dir, text, from, &to, &off, &z, c->op != 0);
    m->to = to;
    m->colrule = off ? COL_NONBLANK : COL_TO;
    m->ctx = 1;
    if (c->op) {
        if (poscmp(to, from) == 0)
            verr("Search wrapped to the starting position");
        if (off) {
            region_lines(m, from.l, to.l);
            return;
        }
        {
            Pos a = from, b = to;
            if (poscmp(a, b) > 0) {
                a = to;
                b = from;
            }
            m->forward = poscmp(to, from) > 0;
            if ((ll(a.l) == 0 || a.o <= fnb(a.l)) && (ll(b.l) == 0 || b.o == 0) && b.l > a.l) {
                region_lines(m, a.l, b.l - 1);
            } else if (b.l > a.l && (ll(b.l) == 0 || b.o <= fnb(b.l))) {
                Pos e = { b.l - 1, ll(b.l - 1) };
                region_chars(m, a, e);
            } else
                region_chars(m, a, b);
        }
    }
}

static void motion(VCmd *c, int key, int cnt, int has_cnt, MRes *m)
{
    Pos from = { cur, cur_off }, to = from;
    int len = cur >= 1 ? ll(cur) : 0, op = c->op;
    memset(m, 0, sizeof *m);
    m->colrule = COL_TO;
    if (nlines == 0 && key != '/' && key != '?')
        verr(NULL);
    if (cnt < 1)
        cnt = 1;
    switch (key) {
    case 'h':
    case 010:
    case 0x7f: {
        int o = cur_off;
        if (o <= 0)
            verr(NULL);
        for (int i = 0; i < cnt && o > 0; i++)
            o = prevoff(cur, o);
        to.o = o;
        if (op)
            region_chars(m, to, from);
        break;
    }
    case 'l':
    case ' ': {
        int o = cur_off;
        if (op) {
            if (len == 0)
                verr(NULL);
            for (int i = 0; i < cnt && o < len; i++)
                o = nextoff(cur, o);
            to.o = o;
            region_chars(m, from, to);
            m->forward = 1;
            break;
        }
        if (nextoff(cur, o) >= len)
            verr(NULL);
        for (int i = 0; i < cnt && nextoff(cur, o) < len; i++)
            o = nextoff(cur, o);
        to.o = o;
        break;
    }
    case 'j':
    case 016:
    case 012:
    case '\r':
    case '+':
        if (cur + cnt > nlines)
            verr(NULL);
        to.l = cur + cnt;
        m->colrule = (key == '\r' || key == '+') ? COL_NONBLANK : COL_KEEP;
        m->forward = 1;
        if (op)
            region_lines(m, cur, to.l);
        break;
    case 'k':
    case 020:
    case '-':
        if (cur - cnt < 1)
            verr(NULL);
        to.l = cur - cnt;
        m->colrule = key == '-' ? COL_NONBLANK : COL_KEEP;
        if (op)
            region_lines(m, to.l, cur);
        break;
    case '_':
        if (cur + cnt - 1 > nlines)
            verr(NULL);
        to.l = cur + cnt - 1;
        m->colrule = COL_NONBLANK;
        m->forward = 1;
        if (op)
            region_lines(m, cur, to.l);
        break;
    case '$':
        if (cur + cnt - 1 > nlines)
            verr(NULL);
        to.l = cur + cnt - 1;
        to.o = lastoff(to.l);
        m->colrule = COL_EOL;
        m->forward = 1;
        if (op) {
            if (cnt == 1) {
                if (len == 0)
                    verr(NULL);
                region_chars(m, from, pos_after(to));
            } else if (cur_off <= fnb(cur))
                region_lines(m, cur, to.l);
            else {
                Pos e = { to.l, ll(to.l) };
                region_chars(m, from, e);
            }
        }
        break;
    case '0':
        to.o = 0;
        if (op) {
            if (cur_off == 0)
                verr(NULL);
            region_chars(m, to, from);
        } else if (len == 0)
            m->colrule = COL_KEEP;
        break;
    case '^': {
        int f = fnb(cur);
        to.o = f;
        m->colrule = COL_NONBLANK;
        if (op) {
            if (len == 0 || is_blank_line(cur) || cur_off == f)
                verr(NULL);
            region_chars(m, from, to);
        }
        break;
    }
    case '|': {
        int o = off_of_col(lt(cur), len, cnt - 1);
        to.o = o;
        if (op) {
            if (len == 0 || o == cur_off)
                verr(NULL);
            region_chars(m, from, to);
            m->forward = o > cur_off;
        }
        break;
    }
    case 'f':
    case 'F':
    case 't':
    case 'T':
    case ';':
    case ',': {
        int fc = key, ch = c->marg;
        if (key == ';' || key == ',') {
            if (!last_find_cmd)
                verr(NULL);
            fc = last_find_cmd;
            ch = last_find_char;
            if (key == ',')
                fc = fc == 'f' ? 'F' : fc == 'F' ? 'f' : fc == 't' ? 'T' : 't';
        } else if (!replaying) {
            last_find_cmd = key;
            last_find_char = ch;
        }
        if (fc == 't' && (key == ';' || key == ',')) {
            /* repeating t: skip the adjacent match */
            Pos f2;
            find_char('t', ch, cnt, from, &f2);
            if (f2.o == from.o && cnt == 1) {
                Pos t2 = from;
                t2.o = nextoff(cur, from.o);
                find_char('t', ch, 1, t2, &f2);
            }
            to = f2;
        } else if (fc == 'T' && (key == ';' || key == ',')) {
            Pos f2;
            find_char('T', ch, cnt, from, &f2);
            if (f2.o == from.o && cnt == 1 && from.o > 0) {
                Pos t2 = from;
                t2.o = prevoff(cur, from.o);
                find_char('T', ch, 1, t2, &f2);
            }
            to = f2;
        } else
            find_char(fc, ch, cnt, from, &to);
        if (op) {
            if (fc == 'f')
                region_chars(m, from, pos_after(to));
            else if (fc == 't')
                region_chars(m, from, pos_after(to));
            else if (fc == 'F')
                region_chars(m, to, from);
            else {
                if (cur_off > 0 && (unsigned char)lt(cur)[prevoff(cur, cur_off)] == ch && cnt == 1)
                    verr(NULL);
                region_chars(m, to, from);
            }
            m->forward = fc == 'f' || fc == 't';
        }
        break;
    }
    case 'w':
    case 'W':
    case 'e':
    case 'E':
    case 'b':
    case 'B': {
        int big = isupper(key), end = 0;
        Pos p = from;
        if (nlines == 0)
            verr(NULL);
        if (key == 'w' || key == 'W') {
            Pos last = from;
            if (op == 'c' && cnt == 1 && from.o < len && is_blankc((unsigned char)lt(cur)[from.o])) {
                region_chars(m, from, pos_after(from));
                m->forward = 1;
                to = from;
                break;
            }
            if (!op) {
                Pos lc = buf_last_char();
                if (poscmp(from, lc) >= 0)
                    verr(NULL);
            }
            for (int i = 0; i < cnt; i++) {
                Pos q = word_fwd(p, big, &end);
                if (end) {
                    if (!op && i == 0) {
                        /* already in the last word: go to the last character */
                        p = q;
                    }
                    break;
                }
                last = p;
                p = q;
            }
            to = p;
            (void)last;
            if (op) {
                m->forward = 1;
                if (end) {
                    Pos e = buf_last_char();
                    region_chars(m, from, pos_after(e));
                } else if (op == 'c') {
                    /* up to the last character before the blanks/EOL preceding the word */
                    Pos e = to;
                    while (pos_prev(&e) && poscmp(e, from) > 0) {
                        int cl = wclass(e, big);
                        if (cl != C_BLANK && cl != C_EOL && cl != C_BLANKLINE)
                            break;
                    }
                    if (poscmp(e, from) < 0)
                        e = from;
                    region_chars(m, from, pos_after(e));
                } else if (to.l > from.l) {
                    Pos e = { to.l - 1, ll(to.l - 1) };
                    if (poscmp(e, from) <= 0)
                        e = pos_after(from);
                    region_chars(m, from, e);
                } else
                    region_chars(m, from, to);
            }
        } else if (key == 'e' || key == 'E') {
            for (int i = 0; i < cnt; i++) {
                Pos q = word_end(p, big, &end);
                if (end && i == 0 && poscmp(q, p) <= 0)
                    verr(NULL);
                p = q;
                if (end)
                    break;
            }
            to = p;
            if (op) {
                region_chars(m, from, pos_after(to));
                m->forward = 1;
            }
        } else {
            if (from.l == 1 && from.o == 0)
                verr(NULL);
            for (int i = 0; i < cnt; i++) {
                Pos q = word_back(p, big, &end);
                p = q;
                if (end)
                    break;
            }
            to = p;
            if (op)
                region_chars(m, to, from);
        }
        if (to.o > ll(to.l))
            to.o = ll(to.l);
        break;
    }
    case 'G': {
        int l = has_cnt ? cnt : nlines;
        if (l > nlines || l < 1)
            verr(NULL);
        to.l = l;
        to.o = fnb(l);
        m->colrule = COL_NONBLANK;
        m->ctx = 1;
        if (op)
            region_lines(m, cur, l);
        m->forward = l >= cur;
        break;
    }
    case 'H':
    case 'L':
    case 'M': {
        int l;
        if (mode == MODE_OPEN) {
            to.l = cur;
            m->colrule = COL_NONBLANK;
            if (op)
                region_lines(m, cur, cur);
            break;
        }
        if (key == 'H') {
            l = top_line + cnt - 1;
            if (l > bot_line || l > nlines)
                verr(NULL);
        } else if (key == 'L') {
            l = bot_line - (cnt - 1);
            if (l < top_line)
                verr(NULL);
        } else {
            int shown = bot_line - top_line + 1;
            l = top_line + (shown + 1) / 2 - 1;
        }
        to.l = l;
        to.o = fnb(l);
        m->colrule = COL_NONBLANK;
        m->ctx = l != cur;
        if (op)
            region_lines(m, cur, l);
        m->forward = l >= cur;
        break;
    }
    case '%': {
        Pos mt;
        if (!match_bracket(from, &mt))
            verr(NULL);
        to = mt;
        m->ctx = 1;
        if (op) {
            Pos a = from, b = mt;
            int after = poscmp(mt, from) > 0;
            if (after && from.o <= fnb(from.l) && mt.o >= lastoff(mt.l) - 0 &&
                mt.o >= (int)(lastoff(mt.l))) {
                region_lines(m, from.l, mt.l);
            } else if (!after && from.o >= lastoff(from.l) && mt.o <= fnb(mt.l)) {
                region_lines(m, mt.l, from.l);
            } else {
                if (!after) {
                    a = mt;
                    b = from;
                }
                region_chars(m, a, pos_after(b));
            }
            m->forward = after;
        }
        break;
    }
    case '\'':
    case '`': {
        int idx = mark_index(c->marg), l, o;
        if (idx < 0)
            verr(NULL);
        if (!mark_get(idx, &l, &o))
            verr("Mark not set");
        if (o > ll(l) || (o >= ll(l) && ll(l) > 0))
            o = fnb(l);
        to.l = l;
        to.o = key == '\'' ? fnb(l) : o;
        m->colrule = key == '\'' ? COL_NONBLANK : COL_TO;
        m->ctx = 1;
        if (op) {
            if (key == '\'')
                region_lines(m, cur, l);
            else {
                Pos a = from, b = to;
                if (poscmp(a, b) == 0)
                    verr(NULL);
                if (poscmp(a, b) > 0) {
                    a = to;
                    b = from;
                }
                if ((ll(a.l) == 0 || a.o <= fnb(a.l)) && (ll(b.l) == 0 || b.o == 0) && b.l > a.l)
                    region_lines(m, a.l, b.l - 1);
                else if (b.l > a.l && (ll(b.l) == 0 || b.o <= fnb(b.l))) {
                    Pos e = { b.l - 1, ll(b.l - 1) };
                    region_chars(m, a, e);
                } else
                    region_chars(m, a, b);
            }
            m->forward = poscmp(to, from) > 0;
        }
        break;
    }
    case '[':
    case ']':
    case '{':
    case '}': {
        int kind = (key == '[' || key == ']') ? 0 : 1, dir = (key == ']' || key == '}') ? 1 : -1;
        int l = cur;
        for (int i = 0; i < cnt; i++) {
            int nl = l;
            if (dir > 0) {
                if (nl >= nlines) {
                    if (i == 0 && (cur == nlines && (cur_off >= lastoff(cur))))
                        verr(NULL);
                    break;
                }
                nl++;
                while (nl < nlines && !boundary_line(nl, kind))
                    nl++;
            } else {
                if (nl <= 1) {
                    if (i == 0 && cur_off == 0)
                        verr(NULL);
                    break;
                }
                nl--;
                while (nl > 1 && !boundary_line(nl, kind))
                    nl--;
            }
            l = nl;
        }
        to.l = l;
        to.o = 0;
        if (l == nlines && dir > 0 && ll(l) > 0 && kind == 0 && !boundary_line(l, 0))
            to.o = lastoff(l);
        m->ctx = 1;
        if (op) {
            if (dir > 0) {
                if ((cur_off == 0 || len == 0) && l > cur && to.o == 0 && l != nlines)
                    region_lines(m, cur, l - 1);
                else if (l == nlines && to.l == nlines)
                    region_chars(m, from, (Pos){ nlines, ll(nlines) });
                else
                    region_chars(m, from, to);
                m->forward = 1;
            } else {
                if ((cur_off == 0 || len == 0) && l < cur)
                    region_lines(m, l, cur - 1 >= l ? cur - 1 : l);
                else
                    region_chars(m, to, from);
            }
        }
        break;
    }
    case '(':
    case ')': {
        Pos p = from;
        for (int i = 0; i < cnt; i++) {
            Pos q = p;
            int found = 0;
            if (key == ')') {
                while (pos_next(&q))
                    if (sentence_start(q)) {
                        found = 1;
                        break;
                    }
                if (!found) {
                    q = (Pos){ nlines, ll(nlines) ? lastoff(nlines) : 0 };
                    if (i == 0 && poscmp(q, p) <= 0)
                        verr(NULL);
                    p = q;
                    break;
                }
            } else {
                while (pos_prev(&q))
                    if (sentence_start(q)) {
                        found = 1;
                        break;
                    }
                if (!found) {
                    q = (Pos){ 1, 0 };
                    if (i == 0 && poscmp(q, p) >= 0)
                        verr(NULL);
                    p = q;
                    break;
                }
            }
            p = q;
        }
        if (p.o > ll(p.l))
            p.o = ll(p.l);
        to = p;
        m->ctx = 1;
        if (op) {
            if (key == ')') {
                if (to.l == nlines && to.o >= lastoff(nlines) && ll(nlines) &&
                    !sentence_start(to))
                    region_chars(m, from, (Pos){ nlines, ll(nlines) });
                else if ((from.o == 0 || len == 0) && to.o == 0 && to.l > from.l)
                    region_lines(m, from.l, to.l - 1);
                else
                    region_chars(m, from, to);
                m->forward = 1;
            } else
                region_chars(m, to, from);
        }
        break;
    }
    case '/':
    case '?':
        search_motion(c, key == '/' ? 1 : -1, c->mtext ? c->mtext : "", m);
        return;
    case 'n':
    case 'N': {
        int dir = search_dir ? search_dir : 1;
        if (key == 'N')
            dir = -dir;
        {
            int sd = search_dir;
            search_motion(c, dir, "", m);
            search_dir = sd ? sd : 1;
        }
        return;
    }
    default:
        verr(NULL);
    }
    m->to = to;
}

/* read the motion for an operator; fills c->mcmd etc. */
static int is_motion_key(int k)
{
    return k > 0 && strchr("\'(-jH\r)$kL,%lM\b[]_nN{};tT?wW^bB+eE|fF/hG0` ", k) != NULL;
}

static int read_motion(VCmd *c)
{
    int k = key_get(1, NULL);
    c->has_mcount = 0;
    c->mcount = 0;
    while (k >= '0' && k <= '9' && (k != '0' || c->has_mcount)) {
        c->mcount = c->mcount * 10 + (k - '0');
        if (c->mcount > 100000000)
            c->mcount = 100000000;
        c->has_mcount = 1;
        k = key_get(1, NULL);
    }
    if (k < 0 || k == 033)
        return 0;
    c->mcmd = k;
    if (k == c->op)
        return 1;
    switch (k) {
    case 'f':
    case 'F':
    case 't':
    case 'T':
    case '\'':
    case '`':
        c->marg = getkey_arg();
        if (c->marg == 033)
            return 0;
        break;
    case '[':
    case ']': {
        int k2 = key_get(1, NULL);
        if (k2 != k)
            return 0;
        break;
    }
    case '/':
    case '?': {
        char *t = read_search_line(k);
        if (!t)
            return 0;
        free(c->mtext);
        c->mtext = t;
        break;
    }
    case 016:
    case 020:
    case 012:
    case 010:
    case 0x7f:
        break;
    default:
        if (!is_motion_key(k) && k != erase_char())
            return 0;
        if (k == erase_char())
            c->mcmd = 'h';
    }
    return 1;
}

/* ---------- text operations ---------- */

/* copy the region to a register */
static void yank_region(int reg, MRes *m, int shift)
{
    Reg r;
    memset(&r, 0, sizeof r);
    if (m->linemode) {
        reg_store_lines(reg, m->ra.l, m->rb.l, shift);
        return;
    }
    r.linemode = 0;
    r.n = m->rb.l - m->ra.l + 1;
    r.t = xmalloc((size_t)r.n * sizeof(char *));
    r.l = xmalloc((size_t)r.n * sizeof(int));
    for (int i = 0; i < r.n; i++) {
        int l = m->ra.l + i, a = i == 0 ? m->ra.o : 0, b = l == m->rb.l ? m->rb.o : ll(l);
        if (b > ll(l))
            b = ll(l);
        if (a > b)
            a = b;
        r.t[i] = xstrndup(lt(l) + a, (size_t)(b - a));
        r.l[i] = b - a;
    }
    reg_store(reg, &r, shift);
}

/* delete a character region */
static void delete_chars(Pos a, Pos b)
{
    Str s;
    if (b.o > ll(b.l))
        b.o = ll(b.l);
    str_init(&s);
    str_addn(&s, lt(a.l), (size_t)a.o);
    str_addn(&s, lt(b.l) + b.o, (size_t)(ll(b.l) - b.o));
    if (b.l > a.l)
        buf_delete(a.l + 1, b.l);
    buf_change(a.l, s.s ? s.s : "", (int)s.len);
    str_free(&s);
}

static int region_multiline(MRes *m)
{
    return m->linemode || m->rb.l > m->ra.l;
}

static int numeric_shift(VCmd *c, MRes *m)
{
    if (region_multiline(m))
        return 1;
    switch (c->mcmd) {
    case '%':
    case '`':
    case '/':
    case '?':
    case '(':
    case ')':
    case 'N':
    case 'n':
    case '{':
    case '}':
        return 1;
    }
    return 0;
}

static void op_delete(VCmd *c, MRes *m)
{
    Pos from = { cur, cur_off };
    yank_region(c->reg, m, numeric_shift(c, m));
    if (m->linemode) {
        int a = m->ra.l, b = m->rb.l;
        long before = b - a + 1;
        buf_delete(a, b);
        if (nlines == 0) {
            cur = 0;
            cur_off = 0;
            want_col = 0;
        } else {
            cur = a <= nlines ? a : nlines;
            cur_off = fnb(cur);
            want_col = col_of(lt(cur), ll(cur), cur_off);
        }
        report_changes(0, "deleted", before);
        return;
    }
    delete_chars(m->ra, m->rb);
    cur = m->ra.l;
    if (ll(cur) == 0) {
        cur_off = 0;
        want_col = 0;
        return;
    }
    if (m->forward || poscmp(m->ra, from) == 0) {
        int o = m->ra.o;
        if (o >= ll(cur))
            o = lastoff(cur);
        set_cursor(cur, o);
    } else {
        int o = m->ra.o;
        if (o >= ll(cur))
            o = lastoff(cur);
        set_cursor(cur, o);
    }
}

static void op_yank(VCmd *c, MRes *m)
{
    Pos from = { cur, cur_off };
    int n = m->rb.l - m->ra.l + 1;
    yank_region(c->reg, m, 0);
    if (m->linemode && n >= OV(OP_REPORT) + 1)
        msg("%d lines yanked", n);
    if (!m->forward && (m->linemode ? m->ra.l < from.l : poscmp(m->ra, from) < 0)) {
        /* backward motion: the start of the region */
        cur = m->ra.l;
        if (m->linemode)
            cursor_to_want();     /* as historical vi: keep the column */
        else
            set_cursor(cur, ll(cur) == 0 ? 0 : m->ra.o);
    }
}

static void op_shift(VCmd *c, MRes *m, int dir)
{
    int a = m->ra.l, b = m->rb.l;
    if (!m->linemode && m->rb.o == 0 && b > a)
        b--;
    (void)c;
    shift_lines(a, b, dir, 1);
    cur = a;
    set_cursor(cur, fnb(cur));
    report_changes(0, dir > 0 ? ">ed" : "<ed", b - a + 1);
}

static void op_filter(VCmd *c, MRes *m, int count_given)
{
    int a = m->ra.l, b = m->rb.l, n;
    char *cmd;
    if (!m->linemode && m->rb.o == 0 && b > a)
        b--;
    if (c->mcmd == 'n' || c->mcmd == 'N')
        cmd = xstrdup("!");
    else if (c->bangcmd)
        cmd = xstrdup(c->bangcmd);
    else {
        cmd = vs_getline("!", 0);
        if (!cmd)
            ex_err_silent();
        free(c->bangcmd);
        c->bangcmd = xstrdup(cmd);
    }
    {
        int changed;
        char *e = expand_bang(cmd, &changed);
        free(cmd);
        cmd = e;
    }
    free(last_bang_cmd);
    last_bang_cmd = xstrdup(cmd);
    if (nlines == 0 || (c->mcmd == '!' && nlines == 0 && !count_given)) {
        Str out;
        str_init(&out);
        run_shell(cmd, 0, 0, 0, &out, 0);
        free(cmd);
        {
            size_t st = 0;
            int at = 0;
            for (size_t i = 0; i <= out.len; i++)
                if (i == out.len || out.s[i] == '\n') {
                    if (i == out.len && i == st)
                        break;
                    buf_insert1(at++, out.s + st, (int)(i - st));
                    st = i + 1;
                }
            cur = nlines ? 1 : 0;
        }
        str_free(&out);
        set_nonblank();
        return;
    }
    n = filter_lines(a, b, cmd, 1);
    free(cmd);
    cur = a <= nlines ? a : nlines;
    if (cur < 1 && nlines)
        cur = 1;
    (void)n;
    set_nonblank();
}

/* ---------- text input commands ---------- */

static void start_input(VCmd *c, int kind, int cnt, const char *erase, int elen, int ai_line,
                        int ai_first, int linecmd)
{
    InputSpec sp;
    Str rec;
    memset(&sp, 0, sizeof sp);
    sp.kind = kind;
    sp.count = cnt;
    sp.erase = erase;
    sp.erase_len = elen;
    sp.ai_line = ai_line;
    sp.ai_first = ai_first;
    sp.linecmd = linecmd;
    sp.replay = replaying && rep_has_text ? (rep_text.s ? rep_text.s : "") : NULL;
    str_init(&rec);
    vi_input(&sp, &rec);
    if (!replaying) {
        str_free(&rep_text);
        rep_text = rec;
        rep_has_text = 1;
    } else
        str_free(&rec);
    (void)c;
}

static void op_change(VCmd *c, MRes *m)
{
    yank_region(c->reg, m, numeric_shift(c, m));
    if (m->linemode) {
        int a = m->ra.l, b = m->rb.l;
        Str ind;
        int k = 0;
        str_init(&ind);
        if (OV(OP_AUTOINDENT)) {
            const char *s = lt(a);
            while (k < ll(a) && (s[k] == ' ' || s[k] == '\t'))
                k++;
            str_addn(&ind, s, (size_t)k);
        }
        if (b > a)
            buf_delete(a + 1, b);
        buf_change(a, ind.s ? ind.s : "", (int)ind.len);
        str_free(&ind);
        cur = a;
        cur_off = k;
        start_input(c, IN_CHANGE, 1, NULL, 0, 0, 0, 1);
        return;
    }
    if (m->rb.l > m->ra.l) {
        delete_chars(m->ra, m->rb);
        cur = m->ra.l;
        cur_off = m->ra.o;
        start_input(c, IN_INSERT, 1, NULL, 0, 0, 0, 0);
        return;
    }
    {
        /* within a line: the region becomes erase-columns ending in '$' */
        int a = m->ra.o, b = m->rb.o;
        char *er;
        int elen;
        Str s;
        if (b > ll(m->ra.l))
            b = ll(m->ra.l);
        elen = b - a;
        er = xstrndup(lt(m->ra.l) + a, (size_t)elen);
        if (elen > 0) {
            int w = 0;
            for (int p = 0; p < elen; p += ch_len(er, elen, p))
                w += ch_width(er, elen, p, 0);
            if (w > 1) {
                /* replace the last character with '$' */
                int lp2 = ch_prev(er, elen);
                er[lp2] = '$';
                elen = lp2 + 1;
            }
        }
        str_init(&s);
        str_addn(&s, lt(m->ra.l), (size_t)a);
        str_addn(&s, lt(m->ra.l) + b, (size_t)(ll(m->ra.l) - b));
        buf_change(m->ra.l, s.s ? s.s : "", (int)s.len);
        str_free(&s);
        cur = m->ra.l;
        cur_off = a;
        start_input(c, IN_INSERT, 1, er, elen, 0, 0, 0);
        free(er);
    }
}

/* ---------- put ---------- */

static void vi_put(VCmd *c, int after)
{
    int reg = c->reg, cnt = c->has_count ? c->count : 1;
    Reg *r = reg_get(reg);
    if (!r)
        verr(NULL);
    if (r->n == 0)
        verr("The buffer is empty");
    if (r->linemode) {
        int at = nlines == 0 ? 0 : (after ? cur : cur - 1);
        long a0 = rep_added;
        for (int i = 0; i < cnt; i++)
            buf_insert(at + i * r->n, r->t, r->l, r->n);
        cur = at + 1;
        {
            const char *s = r->t[0];
            int len = r->l[0], k = 0;
            while (k < len && is_blankc((unsigned char)s[k]))
                k++;
            if (k >= len && len > 0)
                k = ch_prev(s, len);
            set_cursor(cur, k < ll(cur) ? k : 0);
        }
        if (rep_added - a0 >= OV(OP_REPORT))
            msg("%ld more lines", rep_added - a0);
        return;
    }
    {
        /* character mode */
        Str all;
        int at, l0 = cur;
        const char *s;
        int len;
        if (nlines == 0) {
            buf_insert1(0, "", 0);
            cur = 1;
            cur_off = 0;
            l0 = 1;
        }
        s = lt(cur);
        len = ll(cur);
        at = (after && len > 0) ? nextoff(cur, cur_off) : cur_off;
        if (at > len)
            at = len;
        str_init(&all);
        for (int i = 0; i < cnt; i++)
            for (int k = 0; k < r->n; k++) {
                str_addn(&all, r->t[k], (size_t)r->l[k]);
                if (k < r->n - 1)
                    str_addc(&all, '\n');
            }
        {
            /* build the new lines */
            Str first;
            const char *t = all.s ? all.s : "";
            const char *nl = strchr(t, '\n');
            str_init(&first);
            str_addn(&first, s, (size_t)at);
            if (!nl) {
                str_adds(&first, t);
                str_addn(&first, s + at, (size_t)(len - at));
                buf_change(cur, first.s ? first.s : "", (int)first.len);
                str_free(&first);
                if (reg == 0 || reg == '"')
                    set_cursor(cur, at + (int)all.len > 0 ? ch_prev(lt(cur), at + (int)all.len) : 0);
                else
                    set_cursor(cur, at);
            } else {
                char *rest = xstrndup(s + at, (size_t)(len - at));
                int line = cur, k = 0;
                str_addn(&first, t, (size_t)(nl - t));
                buf_change(line, first.s ? first.s : "", (int)first.len);
                str_free(&first);
                t = nl + 1;
                for (;;) {
                    const char *e = strchr(t, '\n');
                    k++;
                    if (!e) {
                        Str last;
                        str_init(&last);
                        str_adds(&last, t);
                        str_adds(&last, rest);
                        buf_insert1(line + k - 1, last.s ? last.s : "", (int)last.len);
                        str_free(&last);
                        break;
                    }
                    buf_insert1(line + k - 1, t, (int)(e - t));
                    t = e + 1;
                }
                free(rest);
                set_cursor(l0, at < ll(l0) ? at : lastoff(l0));
            }
        }
        str_free(&all);
    }
}

/* ---------- miscellaneous commands ---------- */

static void vi_join(int cnt)
{
    int n = cnt <= 2 ? 1 : cnt - 1, len0;
    if (cur >= nlines)
        verr(NULL);
    if (cur + n > nlines)
        n = nlines - cur;
    len0 = ll(cur);
    join_lines(cur, cur + n, 0);
    if (ll(cur) > len0)
        set_cursor(cur, len0);
    else
        set_cursor(cur, lastoff(cur));
}

static void vi_replace_char(int cnt, int ch, int literal)
{
    int len = ll(cur), o = cur_off, n = 0, end = o;
    if (ch == 033 && !literal)
        return;
    while (n < cnt && end < len) {
        end = nextoff(cur, end);
        n++;
    }
    if (n < cnt || len == 0)
        verr(NULL);
    if ((ch == '\r' || ch == '\n') && !literal) {
        Str a, b;
        int k = 0;
        const char *s = lt(cur);
        str_init(&a);
        str_init(&b);
        str_addn(&a, s, (size_t)o);
        if (OV(OP_AUTOINDENT)) {
            while (k < len && is_blankc((unsigned char)s[k]))
                k++;
            str_addn(&b, s, (size_t)k);
        }
        {
            int r = end;
            if (OV(OP_AUTOINDENT))
                while (r < len && is_blankc((unsigned char)s[r]))
                    r++;
            str_addn(&b, s + r, (size_t)(len - r));
        }
        buf_change(cur, a.s ? a.s : "", (int)a.len);
        for (int i = 1; i < cnt; i++)
            buf_insert1(cur + i - 1, "", 0);
        buf_insert1(cur + cnt - 1, b.s ? b.s : "", (int)b.len);
        str_free(&a);
        str_free(&b);
        cur += cnt;
        set_nonblank();
        return;
    }
    {
        Str s;
        char cb[MB_LEN_MAX];
        int cl = 1;
        str_init(&s);
        str_addn(&s, lt(cur), (size_t)o);
        cb[0] = (char)ch;
        if (ch > 0x7f && mb) {
            /* a multibyte character arrives as several keys */
            int need = 0;
            unsigned char u = (unsigned char)ch;
            if ((u & 0xE0) == 0xC0)
                need = 1;
            else if ((u & 0xF0) == 0xE0)
                need = 2;
            else if ((u & 0xF8) == 0xF0)
                need = 3;
            for (int i = 0; i < need; i++)
                cb[cl++] = (char)getkey_arg();
        }
        for (int i = 0; i < cnt; i++)
            str_addn(&s, cb, (size_t)cl);
        str_addn(&s, lt(cur) + end, (size_t)(len - end));
        buf_change(cur, s.s, (int)s.len);
        str_free(&s);
        set_cursor(cur, o + (cnt - 1) * cl);
    }
}

static void vi_tilde(int cnt)
{
    Pos p = { cur, cur_off };
    int n = 0;
    if (nlines == 0 || ll(cur) == 0)
        verr(NULL);
    while (n < cnt) {
        int len = ll(p.l);
        if (p.o < len) {
            const char *s = lt(p.l);
            wint_t c = ch_wc(s, len, p.o);
            int cl = ch_len(s, len, p.o);
            wint_t d = c;
            if (!(c & 0x40000000u)) {
                if (c < 0x80)
                    d = islower((int)c) ? (wint_t)toupper((int)c) : isupper((int)c) ? (wint_t)tolower((int)c) : c;
                else if (mb)
                    d = iswlower(c) ? towupper(c) : iswupper(c) ? towlower(c) : c;
            }
            if (d != c) {
                char mbuf[MB_LEN_MAX];
                int ml = 1;
                Str ns;
                if (d < 0x80 || !mb)
                    mbuf[0] = (char)d;
                else {
                    mbstate_t st;
                    size_t k;
                    memset(&st, 0, sizeof st);
                    k = wcrtomb(mbuf, (wchar_t)d, &st);
                    ml = k == (size_t)-1 ? 0 : (int)k;
                }
                if (ml > 0) {
                    str_init(&ns);
                    str_addn(&ns, s, (size_t)p.o);
                    str_addn(&ns, mbuf, (size_t)ml);
                    str_addn(&ns, s + p.o + cl, (size_t)(len - p.o - cl));
                    buf_change(p.l, ns.s, (int)ns.len);
                    str_free(&ns);
                    cl = ml;
                }
            }
            n++;
            if (n >= cnt) {
                int nx = p.o + cl;
                if (nx < ll(p.l))
                    p.o = nx;
                break;
            }
            p.o += cl;
            if (p.o >= ll(p.l)) {
                if (p.l >= nlines) {
                    p.o = lastoff(p.l);
                    break;
                }
                p.l++;
                p.o = 0;
            }
        } else {
            if (p.l >= nlines)
                break;
            p.l++;
            p.o = 0;
        }
    }
    set_cursor(p.l, p.o < ll(p.l) ? p.o : lastoff(p.l));
}

static void vi_x(VCmd *c, int before)
{
    int cnt = c->has_count ? c->count : 1, len = ll(cur);
    MRes m;
    memset(&m, 0, sizeof m);
    if (before) {
        int o = cur_off;
        if (o == 0)
            verr(NULL);
        for (int i = 0; i < cnt && o > 0; i++)
            o = prevoff(cur, o);
        m.ra = (Pos){ cur, o };
        m.rb = (Pos){ cur, cur_off };
        yank_region(c->reg, &m, 0);
        delete_chars(m.ra, m.rb);
        set_cursor(cur, o < ll(cur) ? o : lastoff(cur));
        return;
    }
    if (len == 0)
        verr(NULL);
    {
        int o = cur_off;
        for (int i = 0; i < cnt && o < len; i++)
            o = nextoff(cur, o);
        m.ra = (Pos){ cur, cur_off };
        m.rb = (Pos){ cur, o };
        yank_region(c->reg, &m, 0);
        delete_chars(m.ra, m.rb);
        if (ll(cur) == 0)
            set_cursor(cur, 0);
        else if (cur_off >= ll(cur))
            set_cursor(cur, lastoff(cur));
    }
}

static void vi_undo(void)
{
    int fc, bd, what, keep_off = cur_off;
    long a0 = rep_added, d0 = rep_deleted;
    set_context_mark();
    if (do_undo(&fc, &bd, &what) < 0)
        verr("Nothing to undo");
    if (fc > 0 && fc <= nlines)
        cur = fc;
    else if (bd >= 1 && bd <= nlines)
        cur = bd;
    else
        cur = nlines > 0 ? 1 : 0;
    if (cur >= 1) {
        if (what == 4 && keep_off < ll(cur))
            set_cursor(cur, keep_off);
        else
            set_nonblank();
    }
    {
        long a = rep_added - a0, d = rep_deleted - d0, net = a > d ? a - d : d - a;
        if (net >= OV(OP_REPORT))
            msg("%ld %s lines", net, a > d ? "more" : "fewer");
    }
}

static void vi_U(void)
{
    if (cur < 1 || !u_text || lp(cur)->id != u_id)
        verr(NULL);
    {
        char *t = xstrndup(u_text, (size_t)u_len);
        buf_change(cur, t, u_len);
        free(t);
    }
    set_cursor(cur, 0);
}

/* run an ex command line typed after ':' */
static void colon_command(void)
{
    char *line;
    int again;
    Source vs;
    memset(&vs, 0, sizeof vs);
    vs.kind = SRC_VI;
    for (;;) {
        line = vs_getline(":", 0);
        if (!line)
            return;
        vs_ex_begin();
        {
            Source *saved = cur_source;
            extern char *vi_src_get(Source *, int, int, const char *);
            vs.get = vi_src_get;
            cur_source = &vs;
            ex_run(line, &vs);
            cur_source = saved;
        }
        free(line);
        vs_ex_end(0);
        if (vi_quit || !vi_active)
            return;
        if (vs_wait_needed) {
            vs_wait_key(1, &again);
            if (again)
                continue;
        }
        return;
    }
}

char *vi_src_get(Source *s, int textmode, int lnum, const char *indent)
{
    (void)s;
    (void)lnum;
    (void)indent;
    return vs_getline("", textmode);
}

/* ---------- scrolling ---------- */

static void open_z(VCmd *c);

static void scroll_cmd(int key, int cnt, int has_cnt)
{
    int tr = vs_text_rows;
    switch (key) {
    case 004: /* ^D */
    case 025: /* ^U */
        if (has_cnt)
            scroll_count = cnt;
        if (!scroll_count)
            scroll_count = (int)OV(OP_SCROLL);
        if (key == 004) {
            if (cur >= nlines)
                verr(NULL);
            if (mode == MODE_OPEN) {
                /* write the following lines */
            }
            cur = cur + scroll_count > nlines ? nlines : cur + scroll_count;
            top_line += scroll_count;
            if (top_line > nlines)
                top_line = nlines;
        } else {
            if (cur <= 1)
                verr(NULL);
            cur = scroll_count >= cur ? 1 : cur - scroll_count;
            top_line -= scroll_count;
            if (top_line < 1)
                top_line = 1;
        }
        set_nonblank();
        break;
    case 006: /* ^F */
    case 002: { /* ^B */
        int w = (int)OV(OP_WINDOW);
        if (mode == MODE_OPEN) {
            VCmd zc;
            memset(&zc, 0, sizeof zc);
            open_z(&zc);
            return;
        }
        if (key == 006) {
            int nt;
            if (cur >= nlines && bot_line >= nlines)
                verr(NULL);
            if (w < 3)
                nt = bot_line + 1;
            else
                nt = top_line + cnt * (w - 2);
            if (nt > nlines)
                nt = nlines;
            if (nt <= top_line)
                nt = top_line + 1 <= nlines ? top_line + 1 : nlines;
            top_line = nt;
            if (cur < top_line)
                cur = top_line;
            if (cur < top_line || cur > vs_last_line_from(top_line))
                cur = top_line;
            else
                cur = top_line;
        } else {
            int nt;
            if (top_line <= 1 && cur <= 1)
                verr(NULL);
            if (w < 3) {
                vs_bottom(top_line - 1 >= 1 ? top_line - 1 : 1);
                nt = top_line;
            } else
                nt = top_line - cnt * (w - 2);
            if (nt < 1)
                nt = 1;
            top_line = nt;
            cur = vs_last_line_from(top_line);
        }
        set_nonblank();
        (void)tr;
        break;
    }
    case 005: /* ^E */
        if (bot_line >= nlines)
            verr(NULL);
        top_line += cnt;
        if (top_line > nlines)
            top_line = nlines;
        if (cur < top_line) {
            cur = top_line;
            cursor_to_want();
        }
        break;
    case 031: /* ^Y */
        if (top_line <= 1)
            verr(NULL);
        top_line -= cnt;
        if (top_line < 1)
            top_line = 1;
        if (cur > vs_last_line_from(top_line)) {
            /* the current character is no longer displayed */
            cur = top_line;
            cursor_to_want();
        }
        break;
    }
}

/* open mode z (and ^F, ^B): write the lines around the current line */
static void open_z(VCmd *c)
{
    int n = (c->has_count ? c->count : (int)OV(OP_WINDOW) - 1) - 2, save = cur, saveoff = cur_off;
    long win = OV(OP_WINDOW);
    int wchanged = opts[OP_WINDOW].changed;
    char b[64];
    if (n < 0)
        n = 0;
    snprintf(b, sizeof b, "z=%d", n);
    vs_ex_begin();
    ex_run(b, NULL);
    out_write("\n", 1);
    vs_ex_end(0);
    opts[OP_WINDOW].num = win;
    opts[OP_WINDOW].changed = wchanged;
    cur = save;
    cur_off = saveoff;
}

static void z_cmd(VCmd *c)
{
    int line = c->has_count ? c->count : cur, k, cnt = 0, has = 0;
    if (mode == MODE_OPEN) {
        open_z(c);
        return;
    }
    if (line > nlines)
        line = nlines;
    if (line < 1)
        line = 1;
    k = key_get(0, NULL);
    while (k >= '0' && k <= '9') {
        cnt = cnt * 10 + k - '0';
        has = 1;
        k = key_get(0, NULL);
    }
    if (mode == MODE_OPEN) {
        if (k != '\r' && k != '\n' && k != '.' && k != '-' && k != '+' && k != '^')
            verr(NULL);
        return;
    }
    if (has) {
        opt_window_set(cnt);
        opts[OP_WINDOW].changed = 1;
        vs_text_rows_update();
        vs_redraw_all();
    }
    switch (k) {
    case '\r':
    case '\n':
        top_line = line;
        cur = line;
        break;
    case '.':
        vs_center(line);
        cur = line;
        break;
    case '-':
        vs_bottom(line);
        cur = line;
        break;
    case '+':
        if (c->has_count) {
            top_line = line;
            cur = line;
        } else {
            if (bot_line >= nlines)
                verr(NULL);
            top_line = bot_line + 1;
            cur = top_line;
        }
        break;
    case '^':
        if (c->has_count) {
            vs_bottom(line);
            {
                int t = top_line - 1;
                if (t < 1) {
                    top_line = 1;
                } else {
                    vs_bottom(t);
                }
                cur = vs_last_line_from(top_line);
            }
        } else {
            if (top_line <= 1)
                verr(NULL);
            vs_bottom(top_line - 1);
            cur = vs_last_line_from(top_line);
        }
        break;
    default:
        verr(NULL);
    }
    set_nonblank();
}

/* ^] : tag under the cursor */
static void tag_cmd(void)
{
    const char *s;
    int len, o;
    Str t;
    if (nlines == 0)
        verr(NULL);
    s = lt(cur);
    len = ll(cur);
    o = cur_off;
    if (o >= len)
        verr(NULL);
    if (is_blankc((unsigned char)s[o])) {
        while (o < len && is_blankc((unsigned char)s[o]))
            o++;
        if (o >= len)
            verr(NULL);
    } else if (!is_wordc(ch_wc(s, len, o)))
        verr(NULL);
    str_init(&t);
    str_addc(&t, s[o]);
    o = nextoff(cur, o);
    while (o < len && is_wordc(ch_wc(s, len, o))) {
        int n = ch_len(s, len, o);
        str_addn(&t, s + o, (size_t)n);
        o += n;
    }
    vs_ex_begin();
    {
        jmp_buf jb, *saved = errjmp;
        errjmp = &jb;
        if (setjmp(jb)) {
            errjmp = saved;
            vs_ex_end(0);
            str_free(&t);
            ex_err_silent();
        }
        tag_goto(t.s, 0);
        errjmp = saved;
    }
    vs_ex_end(0);
    str_free(&t);
}

/* ---------- command dispatch ---------- */

static void exec_cmd(VCmd *c);

static int is_change_cmd(int k)
{
    return k && strchr("!<>ACDIJOPRSXYacdiopsrxy~&", k) != NULL;
}

static void do_operator(VCmd *c)
{
    MRes m;
    int cnt = 1, mc = c->mcmd;
    if (c->has_count)
        cnt = c->count;
    if (c->has_mcount)
        cnt *= c->mcount;
    if (mc == c->op) {
        /* the command repeated: lines */
        if (nlines == 0 && c->op == '!') {
            memset(&m, 0, sizeof m);
            m.linemode = 1;
            op_filter(c, &m, c->has_count);
            return;
        }
        if (nlines == 0)
            verr(NULL);
        if (cur + cnt - 1 > nlines)
            verr(NULL);
        memset(&m, 0, sizeof m);
        region_lines(&m, cur, cur + cnt - 1);
        m.forward = 1;
    } else {
        int hc = c->has_count || c->has_mcount;
        motion(c, mc, cnt, hc, &m);
        if (c->op == '<' || c->op == '>' || c->op == '!') {
            /* whole lines */
            int a = m.ra.l, b = m.rb.l;
            if (!m.linemode && b > a && m.rb.o == 0)
                b--;
            region_lines(&m, a, b);
        }
        if (!m.linemode && poscmp(m.ra, m.rb) == 0 && c->op != 'c')
            verr(NULL);
    }
    switch (c->op) {
    case 'd':
        op_delete(c, &m);
        break;
    case 'y':
        op_yank(c, &m);
        break;
    case 'c':
        op_change(c, &m);
        break;
    case '<':
        op_shift(c, &m, -1);
        break;
    case '>':
        op_shift(c, &m, 1);
        break;
    case '!':
        op_filter(c, &m, c->has_count);
        break;
    }
}

static void exec_motion_cmd(VCmd *c)
{
    MRes m;
    Pos from = { cur, cur_off };
    c->op = 0;
    motion(c, c->cmd, c->has_count ? c->count : 1, c->has_count, &m);
    if (m.ctx && (m.to.l != from.l || (m.to.o != from.o && strchr("/?nN`", c->cmd))))
        set_context_mark();
    cur = m.to.l;
    switch (m.colrule) {
    case COL_KEEP:
        cursor_to_want();
        break;
    case COL_NONBLANK:
        set_nonblank();
        break;
    case COL_TO:
        set_cursor(m.to.l, m.to.o < ll(m.to.l) || ll(m.to.l) == 0 ? m.to.o : lastoff(m.to.l));
        break;
    case COL_EOL:
        cur_off = lastoff(cur);
        want_col = -1;
        break;
    }
    if (c->cmd == '/' || c->cmd == '?') {
        /* a trailing z command */
        const char *t = c->mtext ? c->mtext : "";
        const char *z = strrchr(t, 'z');
        if (z && z > t && *skipbl(z + 1) != 0) {
            int ch = *skipbl(z + 1);
            if (ch == '.')
                vs_center(cur);
            else if (ch == '-')
                vs_bottom(cur);
            else if (ch == '+' || ch == '\r')
                top_line = cur;
        }
    }
}

static void exec_input_cmd(VCmd *c)
{
    int cnt = c->has_count ? c->count : 1;
    switch (c->cmd) {
    case 'a':
        if (nlines == 0) {
            buf_insert1(0, "", 0);
            cur = 1;
            cur_off = 0;
        } else if (ll(cur) > 0)
            cur_off = nextoff(cur, cur_off);
        start_input(c, IN_APPEND, cnt, NULL, 0, 0, 0, 0);
        break;
    case 'A':
        if (nlines == 0) {
            buf_insert1(0, "", 0);
            cur = 1;
        }
        cur_off = ll(cur);
        start_input(c, IN_APPEND, cnt, NULL, 0, 0, 0, 0);
        break;
    case 'i':
        if (nlines == 0) {
            buf_insert1(0, "", 0);
            cur = 1;
            cur_off = 0;
        }
        start_input(c, IN_INSERT, cnt, NULL, 0, 0, 0, 0);
        break;
    case 'I':
        if (nlines == 0) {
            buf_insert1(0, "", 0);
            cur = 1;
        }
        cur_off = fnb(cur);
        if (is_blank_line(cur))
            cur_off = ll(cur);
        start_input(c, IN_INSERT, cnt, NULL, 0, 0, 0, 0);
        break;
    case 'o':
    case 'O': {
        int at = c->cmd == 'o' ? cur : cur - 1, src = cur;
        if (nlines == 0)
            at = 0, src = 0;
        if (at < 0)
            at = 0;
        buf_insert1(at, "", 0);
        cur = at + 1;
        cur_off = 0;
        {
            InputSpec sp;
            Str rec;
            memset(&sp, 0, sizeof sp);
            sp.kind = IN_OPEN;
            sp.count = cnt;
            sp.ai_line = src >= 1 && src <= nlines ? (c->cmd == 'o' ? src : src + 1) : 0;
            sp.ai_first = 1;
            sp.linecmd = 1;
            sp.open_below = c->cmd == 'o';
            sp.replay = replaying && rep_has_text ? (rep_text.s ? rep_text.s : "") : NULL;
            str_init(&rec);
            vi_input(&sp, &rec);
            if (!replaying) {
                str_free(&rep_text);
                rep_text = rec;
                rep_has_text = 1;
            } else
                str_free(&rec);
        }
        break;
    }
    case 'R':
        if (nlines == 0) {
            buf_insert1(0, "", 0);
            cur = 1;
            cur_off = 0;
        }
        start_input(c, IN_REPLACE, cnt, NULL, 0, 0, 0, 0);
        break;
    }
}

static void exec_cmd(VCmd *c)
{
    int cnt = c->has_count ? c->count : 1;
    if (nlines == 0) {
        /* an empty edit buffer: only commands that add text make sense */
        if (strchr("cCsS", c->cmd)) {
            buf_insert1(0, "", 0);
            cur = 1;
            cur_off = 0;
            if (c->cmd == 'c' && c->mcmd != 'c') {
                buf_delete(1, 1);
                cur = 0;
                verr(NULL);
            }
            exec_input_cmd(&(VCmd){ .cmd = 'i' });
            return;
        }
        if (!strchr("aAiIoOpPR!", c->cmd) && !is_motion_key(c->cmd))
            verr(NULL);
    }
    if (cur < 1 && nlines > 0)
        cur = 1;
    switch (c->cmd) {
    case 'c':
    case 'd':
    case 'y':
    case '<':
    case '>':
    case '!':
        c->op = c->cmd;
        do_operator(c);
        return;
    case 'C':
    case 'D':
    case 'Y':
    case 's':
    case 'S':
        c->op = c->cmd == 'C' || c->cmd == 's' || c->cmd == 'S' ? 'c' : c->cmd == 'D' ? 'd' : 'y';
        c->mcmd = c->cmd == 'C' || c->cmd == 'D' ? '$' : c->cmd == 's' ? 'l' : '_';
        if (c->cmd == 'D') {
            c->has_count = 0;
        }
        if (c->cmd == 'S' || c->cmd == 'Y') {
            c->mcmd = c->op;     /* cc, yy */
        }
        c->has_mcount = 0;
        if (c->cmd == 's' && nlines > 0 && ll(cur) == 0) {
            /* s on an empty line: just insert */
            exec_input_cmd(&(VCmd){ .cmd = 'i', .has_count = c->has_count, .count = c->count });
            return;
        }
        do_operator(c);
        return;
    case 'a':
    case 'A':
    case 'i':
    case 'I':
    case 'o':
    case 'O':
    case 'R':
        exec_input_cmd(c);
        return;
    case 'x':
        vi_x(c, 0);
        return;
    case 'X':
        vi_x(c, 1);
        return;
    case 'p':
        vi_put(c, 1);
        return;
    case 'P':
        vi_put(c, 0);
        return;
    case 'J':
        vi_join(cnt);
        return;
    case 'r':
        vi_replace_char(cnt, c->arg, c->marg);
        return;
    case '~':
        vi_tilde(cnt);
        return;
    case '&': {
        vs_ex_begin();
        {
            jmp_buf jb, *saved = errjmp;
            int r;
            errjmp = &jb;
            r = setjmp(jb);
            if (!r)
                ex_run("&", NULL);
            errjmp = saved;
            vs_ex_end(0);
            if (r)
                ex_err_silent();
        }
        return;
    }
    }
    if (is_motion_key(c->cmd) || c->cmd == 016 || c->cmd == 020 || c->cmd == 012 || c->cmd == 010 ||
        c->cmd == '\n') {
        exec_motion_cmd(c);
        return;
    }
    verr(NULL);
}

static void save_repeat(VCmd *c)
{
    free(rep.mtext);
    free(rep.bangcmd);
    rep = *c;
    rep.mtext = c->mtext ? xstrdup(c->mtext) : NULL;
    rep.bangcmd = c->bangcmd ? xstrdup(c->bangcmd) : NULL;
    rep_valid = 1;
}

static void do_repeat(VCmd *dot)
{
    VCmd c;
    if (!rep_valid)
        verr(NULL);
    c = rep;
    c.mtext = rep.mtext ? xstrdup(rep.mtext) : NULL;
    c.bangcmd = rep.bangcmd ? xstrdup(rep.bangcmd) : NULL;
    if (dot->has_count) {
        c.has_count = 1;
        c.count = dot->count;
        c.has_mcount = 0;
        c.mcount = 0;
    }
    if ((c.cmd == 'p' || c.cmd == 'P') && c.reg >= '1' && c.reg < '9') {
        c.reg++;
        rep.reg = c.reg;
    }
    replaying = 1;
    {
        jmp_buf jb, *saved = errjmp;
        errjmp = &jb;
        if (setjmp(jb)) {
            errjmp = saved;
            replaying = 0;
            free(c.mtext);
            free(c.bangcmd);
            ex_err_silent();
        }
        exec_cmd(&c);
        errjmp = saved;
    }
    replaying = 0;
    free(c.mtext);
    free(c.bangcmd);
}

static void snapshot_line(void)
{
    if (cur >= 1 && cur <= nlines && lp(cur)->id != u_id) {
        free(u_text);
        u_text = xstrndup(lt(cur), (size_t)ll(cur));
        u_len = ll(cur);
        u_id = lp(cur)->id;
    }
}

static void execute_buffer(int reg, VCmd *c)
{
    Reg *r;
    Str s;
    if (reg == '@') {
        if (!last_at_buf)
            verr("No previously executed buffer");
        reg = last_at_buf;
    }
    r = reg_get(reg);
    if (!r)
        verr(NULL);
    last_at_buf = reg;
    str_init(&s);
    if (c->has_count)
        str_fmt(&s, "%d", c->count);
    for (int i = 0; i < r->n; i++) {
        str_addn(&s, r->t[i], (size_t)r->l[i]);
        if (r->linemode || i < r->n - 1)
            str_addc(&s, '\n');
    }
    key_push_buffer(s.s ? s.s : "", (int)s.len);
    str_free(&s);
}

/* read and execute one command */
static void one_command(void *arg)
{
    VCmd c;
    int k, flags = 0;
    (void)arg;
    memset(&c, 0, sizeof c);
    snapshot_line();
    k = key_get(1, &flags);
    if (status_sticky) {
        status_sticky = 0;
        vs_clear_message();
    }
    if (k == K_RESIZE)
        return;
    if (k == K_EOF) {
        got_hup = 1;
        return;
    }
    if (k == K_INTR) {
        vi_beep();
        return;
    }
    /* buffer and count, in either order */
    for (;;) {
        if (k == '"') {
            c.reg = getkey_arg();
            if (!isalnum(c.reg) && c.reg != '"')
                verr(NULL);
            k = key_get(1, NULL);
            continue;
        }
        if (k >= '1' && k <= '9' && !c.has_count) {
            c.has_count = 1;
            c.count = k - '0';
            k = key_get(1, NULL);
            while (k >= '0' && k <= '9') {
                c.count = c.count * 10 + (k - '0');
                if (c.count > 100000000)
                    c.count = 100000000;
                k = key_get(1, NULL);
            }
            continue;
        }
        break;
    }
    if (k < 0) {
        if (k == K_INTR)
            vi_beep();
        return;
    }
    c.cmd = k;
    if (k != 'Z' && k != ':')
        ex_quit_pending = 0;
    switch (k) {
    case 033:
        if (c.has_count || c.reg)
            return;
        if (flags)
            vi_beep();
        else
            verr(NULL);
        return;
    case ':':
        if (c.has_count) {
            char b[64];
            snprintf(b, sizeof b, ".,.+%d", c.count - 1);
            key_push(b, (int)strlen(b), 0, 1);
        }
        colon_command();
        return;
    case 'Q':
        vi_quit = 1;
        return;
    case 'Z': {
        int k2 = getkey_arg();
        if (k2 != 'Z')
            verr(NULL);
        vs_ex_begin();
        ex_run("x", NULL);
        vs_ex_end(0);
        return;
    }
    case 007:
        show_file_info();
        return;
    case 014:
    case 022:
        vs_redraw_all();
        return;
    case 036: {
        /* ^^: edit the alternate file */
        if (!altpath)
            verr("No alternate file");
        vs_ex_begin();
        {
            jmp_buf jb, *saved = errjmp;
            int r;
            errjmp = &jb;
            r = setjmp(jb);
            if (!r)
                ex_run("e #", NULL);
            errjmp = saved;
            vs_ex_end(0);
            if (r)
                ex_err_silent();
        }
        return;
    }
    case 035:
        tag_cmd();
        return;
    case 002:
    case 006:
    case 004:
    case 025:
    case 005:
    case 031:
        if (nlines == 0)
            verr(NULL);
        scroll_cmd(k, c.has_count ? c.count : 1, c.has_count);
        return;
    case 'z':
        z_cmd(&c);
        return;
    case 'm': {
        int ch = getkey_arg(), idx = mark_index(ch);
        if (idx < 0)
            verr(NULL);
        if (nlines == 0)
            verr(NULL);
        mark_set(idx, cur, cur_off);
        return;
    }
    case 'u':
        vi_undo();
        return;
    case 'U':
        vi_U();
        return;
    case '.':
        do_repeat(&c);
        return;
    case '@': {
        int ch = getkey_arg();
        execute_buffer(ch, &c);
        return;
    }
    case 'f':
    case 'F':
    case 't':
    case 'T':
    case '\'':
    case '`':
        c.marg = getkey_arg();
        if (c.marg == 033)
            return;
        break;
    case 'r': {
        int ch = getkey_arg();
        if (ch == 026) {
            c.marg = 1;   /* literal */
            ch = getkey_arg();
        }
        c.arg = ch;
        break;
    }
    case '[':
    case ']': {
        int k2 = key_get(1, NULL);
        if (k2 != k)
            verr(NULL);
        break;
    }
    case '/':
    case '?': {
        char *t = read_search_line(k);
        if (!t)
            return;
        c.mtext = t;
        break;
    }
    case 'c':
    case 'd':
    case 'y':
    case '<':
    case '>':
    case '!':
        c.op = k;
        if (!read_motion(&c)) {
            free(c.mtext);
            if (k == '!' || 1)
                return;
        }
        break;
    }
    if (c.cmd == 0x7f || c.cmd == erase_char())
        c.cmd = 'h';
    {
        jmp_buf jb, *saved = errjmp;
        errjmp = &jb;
        if (setjmp(jb)) {
            errjmp = saved;
            free(c.mtext);
            free(c.bangcmd);
            ex_err_silent();
        }
        exec_cmd(&c);
        errjmp = saved;
    }
    if (is_change_cmd(c.cmd) && !(flags & KF_MAPPED && !strchr("aAiIoOcCsSR", c.cmd)))
        save_repeat(&c);
    free(c.mtext);
    free(c.bangcmd);
}

/* the mesg option: while in visual mode, others may not write to the terminal */
static mode_t saved_tty_mode;
static int tty_mode_saved;

static void mesg_off(void)
{
    struct stat st;
    if (OV(OP_MESG) || fstat(0, &st) < 0)
        return;
    if (st.st_mode & (S_IWGRP | S_IWOTH)) {
        saved_tty_mode = st.st_mode & 07777;
        if (fchmod(0, saved_tty_mode & ~(mode_t)(S_IWGRP | S_IWOTH)) == 0)
            tty_mode_saved = 1;
    }
}

static void mesg_restore(void)
{
    if (tty_mode_saved) {
        if (fchmod(0, saved_tty_mode) < 0) {
            /* nothing to do */
        }
        tty_mode_saved = 0;
    }
}

void vi_main(int line, int type, int count)
{
    int was_active = vi_active;
    (void)count;
    if (was_active)
        return;
    vi_active = 1;
    vi_quit = 0;
    mesg_off();
    term_init();
    term_raw_on();
    ex_typeahead_to_keys();
    term_install_tstp();
    vs_redraw_all();
    if (line >= 1 && line <= nlines)
        cur = line;
    if (nlines > 0 && cur < 1)
        cur = 1;
    if (cur > nlines)
        cur = nlines;
    if (cur >= 1 && cur_off >= ll(cur))
        cur_off = ll(cur) ? lastoff(cur) : 0;
    if (!marks[MARK_CONTEXT].set && nlines > 0)
        mark_set(MARK_CONTEXT, cur, cur_off);
    vs_text_rows_update();
    switch (type) {
    case '+':
        top_line = cur;
        break;
    case '-':
        vs_bottom(cur);
        break;
    case '.':
        vs_center(cur);
        break;
    case '^': {
        int w = (int)OV(OP_WINDOW), l = cur <= w ? 1 : cur - (w - 1);
        vs_bottom(l);
        break;
    }
    default:
        top_line = cur > 0 ? cur : 1;
        vs_center(cur > 0 ? cur : 1);
        if (cur <= vs_text_rows / 2 || 1) {
            top_line = 1;
            if (cur > vs_last_line_from(1))
                vs_center(cur);
        }
    }
    if (pending_msg) {
        vs_message(pending_msg, 0);
        free(pending_msg);
        pending_msg = NULL;
    }
    while (!vi_quit) {
        if (got_hup)
            hup_exit();
        if (got_winch || got_cont) {
            got_winch = got_cont = 0;
            term_get_size();
            opts_screen_changed();
            if (term_raw == 0)
                term_raw_on();
            vs_redraw_all();
        }
        if (nlines > 0 && (cur < 1 || cur > nlines))
            cur = cur < 1 ? 1 : nlines;
        if (cur >= 1 && cur_off > ll(cur))
            cur_off = ll(cur);
        if (cur >= 1 && cur_off >= ll(cur) && ll(cur) > 0)
            cur_off = lastoff(cur);
        mode_indicator = mode == MODE_OPEN ? "COMMAND MODE (open)" : "COMMAND MODE";
        if (!key_pending())
            vs_refresh();
        if (!key_pending_mapped())
            undo_inhibit_new = 0;
        else
            undo_inhibit_new = 1;
        undo_open();
        if (protect(one_command, NULL) < 0) {
            key_flush_mapped();
        }
        undo_close();
        if (vs_wait_needed && !vi_quit && vi_active)
            vs_wait_key(0, NULL);
        intr = 0;
    }
    vi_active = 0;
    mesg_restore();
    term_remove_tstp();
    tmove(rows - 1, 0);
    tclear_eol();
    tflush();
    {
        /* hand keys typed ahead to ex command mode */
        Str ahead;
        str_init(&ahead);
        while (key_pending()) {
            int k = key_get(0, NULL);
            if (k >= 0)
                str_addc(&ahead, (char)k);
        }
        while (key_peek_timeout(0)) {
            int k = key_raw();
            if (k < 0)
                break;
            str_addc(&ahead, (char)k);
        }
        term_raw_off();
        ex_typeahead(ahead.s, (int)ahead.len);
        str_free(&ahead);
    }
}
