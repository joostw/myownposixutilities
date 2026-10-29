/*
 * regex.c - regular expressions for ex and vi.
 *
 * A backtracking matcher for POSIX basic regular expressions with the ex
 * extensions \< \> and ~, the "magic" edit option, and ignorecase applied
 * at match time.  Matching finds the leftmost-longest match.  Also
 * replacement strings for the s command.
 */
#include "ex.h"

enum { N_CHAR, N_ANY, N_SET, N_BOL, N_EOL, N_WB, N_WE, N_GROUP, N_BREF };

typedef struct SetItem {
    int type;            /* 0 char, 1 range, 2 class */
    wint_t lo, hi;
    wctype_t cls;
} SetItem;

typedef struct Set {
    int neg;
    unsigned char ascii[16];   /* bitmap for 0..127 */
    unsigned char ascii_ic[16];
    SetItem *it;
    int n;
} Set;

typedef struct Node {
    int type;
    int min, max;
    struct Node *next;
    struct Node *sub;      /* group */
    int gi;                /* group index / backref */
    char cb[MB_LEN_MAX + 1];
    int cn;
    wint_t wc;
    Set *set;
    struct Node *alloc;
} Node;

struct Regex {
    Node *prog;
    Node *nodes;
    int nsub;
    int refs;
};

typedef struct P {
    const char *p;
    int delim;
    int magic;
    Regex *re;
    char *err;
    int ngroups;
} P;

#define REMAX 0x7fffffff

static Node *nnew(P *ps, int type)
{
    Node *n = xcalloc(1, sizeof *n);
    n->type = type;
    n->min = n->max = 1;
    n->alloc = ps->re->nodes;
    ps->re->nodes = n;
    return n;
}

static void set_bit(unsigned char *bm, int c) { bm[c >> 3] |= (unsigned char)(1 << (c & 7)); }
static int get_bit(const unsigned char *bm, int c) { return bm[c >> 3] & (1 << (c & 7)); }

static void set_add(Set *s, int type, wint_t lo, wint_t hi, wctype_t cls)
{
    s->it = xrealloc(s->it, (size_t)(s->n + 1) * sizeof(SetItem));
    s->it[s->n].type = type;
    s->it[s->n].lo = lo;
    s->it[s->n].hi = hi;
    s->it[s->n].cls = cls;
    s->n++;
}

/* decode one character from the pattern */
static wint_t pat_char(const char **pp, char *cb, int *cn)
{
    const char *p = *pp;
    int n = ch_len(p, (int)strlen(p), 0);
    wint_t wc = ch_wc(p, (int)strlen(p), 0);
    if (cb) {
        memcpy(cb, p, (size_t)n);
        cb[n] = 0;
        *cn = n;
    }
    *pp = p + n;
    return wc;
}

static int wc_lower(wint_t c)
{
    if (c < 0x80)
        return tolower((int)c);
    if (c & 0x40000000u)
        return (int)c;
    return mb ? (int)towlower(c) : tolower((int)c);
}

static void set_finish(Set *s)
{
    /* precompute ASCII bitmaps */
    for (int c = 0; c < 128; c++) {
        int in = 0, inic = 0;
        for (int i = 0; i < s->n && !(in && inic); i++) {
            SetItem *it = &s->it[i];
            int cands[3] = { c, tolower(c), toupper(c) };
            for (int k = 0; k < 3; k++) {
                int x = cands[k], hit = 0;
                if (it->type == 0)
                    hit = (wint_t)x == it->lo;
                else if (it->type == 1)
                    hit = (wint_t)x >= it->lo && (wint_t)x <= it->hi;
                else
                    hit = iswctype((wint_t)x, it->cls) != 0;
                if (hit) {
                    if (k == 0)
                        in = 1;
                    inic = 1;
                }
            }
        }
        if (in)
            set_bit(s->ascii, c);
        if (inic)
            set_bit(s->ascii_ic, c);
    }
}

static int set_has(Set *s, wint_t c, int icase)
{
    int r = 0;
    if (c < 128)
        r = get_bit(icase ? s->ascii_ic : s->ascii, (int)c) != 0;
    else if (!(c & 0x40000000u)) {
        wint_t cands[3];
        int nc = 1;
        cands[0] = c;
        if (icase) {
            cands[nc++] = towlower(c);
            cands[nc++] = towupper(c);
        }
        for (int i = 0; i < s->n && !r; i++) {
            SetItem *it = &s->it[i];
            for (int k = 0; k < nc && !r; k++) {
                wint_t x = cands[k];
                if (it->type == 0)
                    r = x == it->lo;
                else if (it->type == 1)
                    r = x >= it->lo && x <= it->hi;
                else
                    r = iswctype(x, it->cls) != 0;
            }
        }
    }
    return s->neg ? !r : r;
}

/* parse a bracket expression; ps->p points after '[' */
static Set *parse_bracket(P *ps)
{
    Set *s = xcalloc(1, sizeof *s);
    const char *p = ps->p;
    int first = 1;
    if (*p == '^') {
        s->neg = 1;
        p++;
    }
    for (;;) {
        wint_t lo;
        if (*p == 0 || (*p == '\n')) {
            ps->err = "Missing ]";
            free(s);
            return NULL;
        }
        if (*p == ']' && !first) {
            p++;
            break;
        }
        first = 0;
        if (p[0] == '[' && (p[1] == ':' || p[1] == '=' || p[1] == '.')) {
            int kind = p[1];
            const char *q = p + 2, *e = q;
            while (*e && !(e[0] == kind && e[1] == ']'))
                e++;
            if (!*e) {
                ps->err = "Missing ]";
                free(s);
                return NULL;
            }
            if (kind == ':') {
                char name[32];
                size_t n = (size_t)(e - q);
                wctype_t t;
                if (n >= sizeof name) {
                    ps->err = "Invalid character class";
                    free(s);
                    return NULL;
                }
                memcpy(name, q, n);
                name[n] = 0;
                t = wctype(name);
                if (!t) {
                    ps->err = "Invalid character class";
                    free(s);
                    return NULL;
                }
                set_add(s, 2, 0, 0, t);
                p = e + 2;
                continue;
            }
            /* equivalence class or collating symbol: a single character */
            {
                const char *t = q;
                lo = pat_char(&t, NULL, NULL);
                if (t != e) {
                    ps->err = "Invalid collating element";
                    free(s);
                    return NULL;
                }
                p = e + 2;
            }
        } else
            lo = pat_char(&p, NULL, NULL);
        if (p[0] == '-' && p[1] && p[1] != ']') {
            wint_t hi;
            p++;
            if (p[0] == '[' && (p[1] == '.' || p[1] == '=')) {
                int kind = p[1];
                const char *q = p + 2, *e = q;
                while (*e && !(e[0] == kind && e[1] == ']'))
                    e++;
                if (!*e) {
                    ps->err = "Missing ]";
                    free(s);
                    return NULL;
                }
                hi = pat_char(&q, NULL, NULL);
                p = e + 2;
            } else
                hi = pat_char(&p, NULL, NULL);
            if (hi < lo) {
                ps->err = "Invalid range";
                free(s);
                return NULL;
            }
            set_add(s, 1, lo, hi, 0);
        } else
            set_add(s, 0, lo, lo, 0);
    }
    ps->p = p;
    set_finish(s);
    return s;
}

static int at_end(P *ps, const char *p)
{
    return *p == 0 || *p == '\n' || (ps->delim && (unsigned char)*p == ps->delim);
}

static Node *parse_seq(P *ps, int depth);

static Node *literal_node(P *ps, const char *bytes, int n)
{
    Node *nd = nnew(ps, N_CHAR);
    memcpy(nd->cb, bytes, (size_t)n);
    nd->cb[n] = 0;
    nd->cn = n;
    nd->wc = ch_wc(bytes, n, 0);
    return nd;
}

static int parse_int(const char **pp)
{
    long v = 0;
    const char *p = *pp;
    if (!isdigit((unsigned char)*p))
        return -1;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        if (v > 32767)
            v = 32767;
        p++;
    }
    *pp = p;
    return (int)v;
}

static Node *parse_seq(P *ps, int depth)
{
    Node *head = NULL, *tail = NULL, *prev = NULL;
    int seq_start = 1;
    for (;;) {
        const char *p = ps->p;
        Node *atom = NULL;
        int c = (unsigned char)*p;
        if (at_end(ps, p))
            break;
        if (c == '\\' && p[1] == ')') {
            if (depth == 0) {
                ps->err = "Unmatched \\)";
                return NULL;
            }
            break;
        }
        /* repetition operators */
        if (prev && prev->type != N_BOL &&
            ((c == '*' && ps->magic) || (c == '\\' && p[1] == '*' && !ps->magic))) {
            ps->p = p + (c == '*' ? 1 : 2);
            if (prev->type == N_EOL || prev->type == N_WB || prev->type == N_WE) {
                /* '*' after an anchor: literal */
                atom = literal_node(ps, "*", 1);
                goto add;
            }
            prev->min = 0;
            prev->max = REMAX;
            continue;
        }
        if (prev && prev->type != N_BOL && c == '\\' && p[1] == '{') {
            int m, n;
            const char *q = p + 2;
            m = parse_int(&q);
            if (m < 0) {
                ps->err = "Invalid interval";
                return NULL;
            }
            n = m;
            if (*q == ',') {
                q++;
                n = parse_int(&q);
                if (n < 0)
                    n = REMAX;
            }
            if (q[0] != '\\' || q[1] != '}' || n < m) {
                ps->err = "Invalid interval";
                return NULL;
            }
            ps->p = q + 2;
            prev->min = m;
            prev->max = n;
            continue;
        }
        if (c == '^' && seq_start) {
            atom = nnew(ps, N_BOL);
            ps->p = p + 1;
            goto add;
        }
        if (c == '$') {
            const char *q = p + 1;
            if (at_end(ps, q) || (q[0] == '\\' && q[1] == ')')) {
                atom = nnew(ps, N_EOL);
                ps->p = q;
                goto add;
            }
            atom = literal_node(ps, "$", 1);
            ps->p = q;
            goto add;
        }
        if ((c == '.' && ps->magic) || (c == '\\' && p[1] == '.' && !ps->magic)) {
            atom = nnew(ps, N_ANY);
            ps->p = p + (c == '.' ? 1 : 2);
            goto add;
        }
        if ((c == '[' && ps->magic) || (c == '\\' && p[1] == '[' && !ps->magic)) {
            ps->p = p + (c == '[' ? 1 : 2);
            atom = nnew(ps, N_SET);
            atom->set = parse_bracket(ps);
            if (!atom->set)
                return NULL;
            goto add;
        }
        if ((c == '~' && ps->magic) || (c == '\\' && p[1] == '~' && !ps->magic)) {
            /* the previous replacement string, literally */
            const char *r = last_repl ? last_repl : "";
            ps->p = p + (c == '~' ? 1 : 2);
            while (*r) {
                int n = ch_len(r, (int)strlen(r), 0);
                atom = literal_node(ps, r, n);
                r += n;
                if (*r) {
                    if (!head)
                        head = atom;
                    else
                        tail->next = atom;
                    tail = atom;
                    atom = NULL;
                }
            }
            if (!atom) {
                prev = tail;
                seq_start = 0;
                continue;
            }
            goto add;
        }
        if (c == '*' && !prev && ps->magic) {
            atom = literal_node(ps, "*", 1);
            ps->p = p + 1;
            goto add;
        }
        if (c == '\\') {
            int d = (unsigned char)p[1];
            if (d == 0 || d == '\n') {
                ps->err = "Trailing \\";
                return NULL;
            }
            if (ps->delim && d == ps->delim) {
                atom = literal_node(ps, p + 1, 1);
                ps->p = p + 2;
                goto add;
            }
            if (d == '(') {
                Node *g = nnew(ps, N_GROUP);
                if (ps->ngroups >= NSUB - 1) {
                    ps->err = "Too many \\(";
                    return NULL;
                }
                g->gi = ++ps->ngroups;
                ps->p = p + 2;
                g->sub = parse_seq(ps, depth + 1);
                if (ps->err)
                    return NULL;
                if (!(ps->p[0] == '\\' && ps->p[1] == ')')) {
                    ps->err = "Unmatched \\(";
                    return NULL;
                }
                ps->p += 2;
                atom = g;
                goto add;
            }
            if (d >= '1' && d <= '9') {
                if (d - '0' > ps->ngroups) {
                    ps->err = "Invalid back reference";
                    return NULL;
                }
                atom = nnew(ps, N_BREF);
                atom->gi = d - '0';
                ps->p = p + 2;
                goto add;
            }
            if (d == '<') {
                atom = nnew(ps, N_WB);
                ps->p = p + 2;
                goto add;
            }
            if (d == '>') {
                atom = nnew(ps, N_WE);
                ps->p = p + 2;
                goto add;
            }
            if (d == '{') {
                if (!prev) {
                    atom = literal_node(ps, "{", 1);
                    ps->p = p + 2;
                    goto add;
                }
                ps->err = "Invalid interval";
                return NULL;
            }
            if (d == 'n') {
                atom = literal_node(ps, "\n", 1);
                ps->p = p + 2;
                goto add;
            }
            {
                const char *q = p + 1;
                int n = ch_len(q, (int)strlen(q), 0);
                atom = literal_node(ps, q, n);
                ps->p = q + n;
                goto add;
            }
        }
        {
            int n = ch_len(p, (int)strlen(p), 0);
            atom = literal_node(ps, p, n);
            ps->p = p + n;
        }
    add:
        if (!head)
            head = atom;
        else
            tail->next = atom;
        tail = atom;
        prev = atom;
        seq_start = 0;
    }
    return head;
}

static void set_free(Set *s)
{
    if (s) {
        free(s->it);
        free(s);
    }
}

void re_free(Regex *re)
{
    Node *n, *nx;
    if (!re || --re->refs > 0)
        return;
    for (n = re->nodes; n; n = nx) {
        nx = n->alloc;
        set_free(n->set);
        free(n);
    }
    free(re);
}

Regex *re_compile(const char *src, int delim, const char **endp, int magic, char **errp)
{
    P ps;
    Regex *re = xcalloc(1, sizeof *re);
    re->refs = 1;
    memset(&ps, 0, sizeof ps);
    ps.p = src;
    ps.delim = delim;
    ps.magic = magic;
    ps.re = re;
    re->prog = parse_seq(&ps, 0);
    if (ps.err) {
        if (errp)
            *errp = ps.err;
        re_free(re);
        return NULL;
    }
    re->nsub = ps.ngroups;
    if (endp)
        *endp = ps.p;
    return re;
}

int re_nsub(Regex *re) { return re->nsub; }

/* ---------- matching ---------- */

typedef struct MS {
    const char *s;
    int len;
    int icase;
    int best;
    int caps[2 * NSUB];
    int bcaps[2 * NSUB];
    long steps;
    int aborted;
} MS;

typedef struct Cont {
    Node *g;
    int cnt, start;
    struct Cont *up;
} Cont;

static int mnode(MS *st, Node *n, int pos, Cont *k);

static int wordc_at(MS *st, int pos)
{
    if (pos < 0 || pos >= st->len)
        return 0;
    return is_wordc(ch_wc(st->s, st->len, pos));
}

/* match one occurrence of a simple atom at pos; length or -1 */
static int match1(MS *st, Node *n, int pos)
{
    const char *s = st->s;
    int len = st->len;
    switch (n->type) {
    case N_CHAR:
        if (pos + n->cn > len)
            return -1;
        if (memcmp(s + pos, n->cb, (size_t)n->cn) == 0)
            return n->cn;
        if (st->icase) {
            int l = ch_len(s, len, pos);
            wint_t c = ch_wc(s, len, pos);
            if (wc_lower(c) == wc_lower(n->wc))
                return l;
        }
        return -1;
    case N_ANY:
        if (pos >= len)
            return -1;
        return ch_len(s, len, pos);
    case N_SET: {
        wint_t c;
        if (pos >= len)
            return -1;
        c = ch_wc(s, len, pos);
        if (!mb && c >= 0x80)
            c = btowc((int)c);
        if (set_has(n->set, c, st->icase))
            return ch_len(s, len, pos);
        return -1;
    }
    case N_BREF: {
        int so = st->caps[2 * n->gi], eo = st->caps[2 * n->gi + 1];
        int l;
        if (so < 0 || eo < 0)
            return -1;
        l = eo - so;
        if (pos + l > len)
            return -1;
        if (memcmp(s + so, s + pos, (size_t)l) == 0)
            return l;
        if (st->icase) {
            int a = so, b = pos;
            while (a < eo) {
                int la = ch_len(s, len, a), lb = ch_len(s, len, b);
                if (b >= len || wc_lower(ch_wc(s, len, a)) != wc_lower(ch_wc(s, len, b)))
                    return -1;
                a += la;
                b += lb;
            }
            return b - pos;
        }
        return -1;
    }
    }
    return -1;
}

static int grp(MS *st, Node *g, int cnt, int pos, Cont *k)
{
    if (cnt < g->max) {
        Cont c;
        c.g = g;
        c.cnt = cnt;
        c.start = pos;
        c.up = k;
        if (mnode(st, g->sub, pos, &c))
            return 1;
    }
    if (cnt >= g->min)
        return mnode(st, g->next, pos, k);
    return 0;
}

static int mnode(MS *st, Node *n, int pos, Cont *k)
{
    if ((++st->steps & 0xfff) == 0 && intr) {
        st->aborted = 1;
        return 1;
    }
    if (!n) {
        if (!k) {
            if (pos > st->best) {
                st->best = pos;
                memcpy(st->bcaps, st->caps, sizeof st->caps);
            }
            return pos == st->len;
        } else {
            Node *g = k->g;
            int so = st->caps[2 * g->gi], eo = st->caps[2 * g->gi + 1], r;
            st->caps[2 * g->gi] = k->start;
            st->caps[2 * g->gi + 1] = pos;
            if (pos == k->start) {
                /* empty iteration: do not iterate again */
                if (k->cnt + 1 >= g->min)
                    r = mnode(st, g->next, pos, k->up);
                else
                    r = grp(st, g, k->cnt + 1, pos, k->up);
            } else
                r = grp(st, g, k->cnt + 1, pos, k->up);
            if (!r) {
                st->caps[2 * g->gi] = so;
                st->caps[2 * g->gi + 1] = eo;
            }
            return r;
        }
    }
    switch (n->type) {
    case N_BOL:
        return pos == 0 ? mnode(st, n->next, pos, k) : 0;
    case N_EOL:
        return pos == st->len ? mnode(st, n->next, pos, k) : 0;
    case N_WB:
        if (wordc_at(st, pos) && (pos == 0 || !wordc_at(st, ch_prev(st->s, pos))))
            return mnode(st, n->next, pos, k);
        return 0;
    case N_WE:
        if (pos > 0 && wordc_at(st, ch_prev(st->s, pos)) && !wordc_at(st, pos))
            return mnode(st, n->next, pos, k);
        return 0;
    case N_GROUP:
        return grp(st, n, 0, pos, k);
    default:
        break;
    }
    if (n->min == 1 && n->max == 1) {
        int l = match1(st, n, pos);
        if (l < 0)
            return 0;
        return mnode(st, n->next, pos + l, k);
    }
    {
        int sbuf[128], *ps = sbuf, cap = 128, cnt = 0, p = pos, r = 0, zero = 0;
        ps[0] = pos;
        while (cnt < n->max) {
            int l = match1(st, n, p);
            if (l < 0)
                break;
            if (l == 0) {
                zero = 1;   /* empty back reference: any number of repetitions */
                break;
            }
            p += l;
            cnt++;
            if (cnt + 1 >= cap) {
                int *np = xmalloc((size_t)cap * 2 * sizeof(int));
                memcpy(np, ps, (size_t)cap * sizeof(int));
                if (ps != sbuf)
                    free(ps);
                ps = np;
                cap *= 2;
            }
            ps[cnt] = p;
        }
        if (zero && mnode(st, n->next, p, k))
            r = 1;
        else
            for (int j = cnt; j >= n->min; j--)
                if (mnode(st, n->next, ps[j], k)) {
                    r = 1;
                    break;
                }
        if (ps != sbuf)
            free(ps);
        return r;
    }
}

int re_exec(Regex *re, const char *s, int len, int start, int anchored, Match *m)
{
    MS st;
    int p = start;
    Node *first = re->prog;
    st.s = s;
    st.len = len;
    st.icase = (int)OV(OP_IGNORECASE);
    st.steps = 0;
    st.aborted = 0;
    if (first && first->type == N_BOL && !anchored) {
        if (start > 0)
            return 0;
        anchored = 1;
    }
    for (;;) {
        if (p > len)
            return 0;
        if (!anchored && first && first->type == N_CHAR && first->min >= 1 && !st.icase) {
            const char *q = memchr(s + p, first->cb[0], (size_t)(len - p));
            if (!q)
                return 0;
            p = (int)(q - s);
        }
        st.best = -1;
        for (int i = 0; i < 2 * NSUB; i++)
            st.caps[i] = -1;
        mnode(&st, re->prog, p, NULL);
        if (st.aborted) {
            intr = 0;
            ex_err("Interrupted");
        }
        if (st.best >= 0) {
            if (m) {
                for (int i = 1; i < NSUB; i++) {
                    m->so[i] = st.bcaps[2 * i];
                    m->eo[i] = st.bcaps[2 * i + 1];
                    if (m->so[i] < 0 || m->eo[i] < 0)
                        m->so[i] = m->eo[i] = -1;
                }
                m->so[0] = p;
                m->eo[0] = st.best;
            }
            return 1;
        }
        if (anchored || p >= len)
            return 0;
        p += ch_len(s, len, p);
    }
}

/* find the end of a delimited RE (the unescaped delimiter, outside brackets) */
const char *re_skip(const char *p, int delim)
{
    int magic = (int)OV(OP_MAGIC);
    while (*p && *p != '\n') {
        if ((unsigned char)*p == delim)
            return p;
        if (*p == '\\') {
            if (!p[1] || p[1] == '\n')
                return p + 1;
            if (p[1] == '[' && !magic) {
                p += 2;
                goto bracket;
            }
            p += 2;
            continue;
        }
        if (*p == '[' && magic) {
            p++;
        bracket: {
                const char *q = p;
                if (*q == '^')
                    q++;
                if (*q == ']')
                    q++;
                while (*q && *q != '\n' && *q != ']') {
                    if (q[0] == '[' && (q[1] == ':' || q[1] == '=' || q[1] == '.')) {
                        int kind = q[1];
                        const char *e = q + 2;
                        while (*e && *e != '\n' && !(e[0] == kind && e[1] == ']'))
                            e++;
                        if (*e == kind) {
                            q = e + 2;
                            continue;
                        }
                    }
                    q++;
                }
                if (*q == ']')
                    p = q + 1;
                /* else: unterminated; leave p after '[' */
            }
            continue;
        }
        p++;
    }
    return p;
}

/* ---------- last RE ---------- */

Regex *last_re;
char *last_repl;

void set_last_re(Regex *re)
{
    if (re == last_re)
        return;
    if (re)
        re->refs++;
    re_free(last_re);
    last_re = re;
}

/*
 * Parse a delimited RE starting at *pp (just after the opening delimiter).
 * An empty RE means the last RE.  *pp is left after the closing delimiter
 * if there is one.
 */
Regex *get_re(const char **pp, int delim, int set_last)
{
    const char *p = *pp, *end;
    Regex *re;
    char *err = NULL;
    end = re_skip(p, delim);
    if (end == p) {
        if (!last_re)
            ex_err("No previous regular expression");
        re = last_re;
        re->refs++;
    } else {
        re = re_compile(p, delim, NULL, (int)OV(OP_MAGIC), &err);
        if (!re)
            ex_err("%s", err ? err : "Invalid regular expression");
        if (set_last)
            set_last_re(re);
    }
    if (delim && (unsigned char)*end == delim)
        end++;
    *pp = end;
    return re;   /* caller must re_free */
}

/* ---------- replacement strings ---------- */

enum { R_LIT, R_GRP, R_CASE, R_SPLIT };
typedef struct RTok {
    int type;
    int n;          /* group number, case op */
    char *s;
    int len;
} RTok;
struct Repl {
    RTok *t;
    int n;
};

static void rt_add(Repl *r, int type, int n, const char *s, int len)
{
    if (type == R_LIT && r->n > 0 && r->t[r->n - 1].type == R_LIT) {
        RTok *t = &r->t[r->n - 1];
        t->s = xrealloc(t->s, (size_t)(t->len + len + 1));
        memcpy(t->s + t->len, s, (size_t)len);
        t->len += len;
        t->s[t->len] = 0;
        return;
    }
    r->t = xrealloc(r->t, (size_t)(r->n + 1) * sizeof(RTok));
    r->t[r->n].type = type;
    r->t[r->n].n = n;
    r->t[r->n].s = type == R_LIT ? xstrndup(s, (size_t)len) : NULL;
    r->t[r->n].len = len;
    r->n++;
}

static void repl_parse(Repl *r, const char *p, int delim, const char **endp, int magic,
                       Str *expanded, int depth)
{
    while (*p && *p != '\n' && !(delim && (unsigned char)*p == delim)) {
        int c = (unsigned char)*p;
        if (c == '\r') {
            rt_add(r, R_SPLIT, 0, NULL, 0);
            if (expanded)
                str_addc(expanded, '\r');
            p++;
            continue;
        }
        if ((c == '&' && magic) || (c == '\\' && p[1] == '&' && !magic)) {
            rt_add(r, R_GRP, 0, NULL, 0);
            if (expanded)
                str_addn(expanded, p, c == '&' ? 1 : 2);
            p += c == '&' ? 1 : 2;
            continue;
        }
        if ((c == '~' && magic) || (c == '\\' && p[1] == '~' && !magic)) {
            if (last_repl && depth < 2)
                repl_parse(r, last_repl, 0, NULL, magic, NULL, depth + 1);
            if (expanded && last_repl)
                str_adds(expanded, last_repl);
            p += c == '~' ? 1 : 2;
            continue;
        }
        if (c == '\\') {
            int d = (unsigned char)p[1];
            if (d == 0) {
                rt_add(r, R_LIT, 0, "\\", 1);
                if (expanded)
                    str_addc(expanded, '\\');
                p++;
                continue;
            }
            if (expanded)
                str_addn(expanded, p, 2);
            p += 2;
            if (d == '\n' || d == '\r') {
                rt_add(r, R_SPLIT, 0, NULL, 0);
                continue;
            }
            if (delim && d == delim) {
                char ch = (char)d;
                rt_add(r, R_LIT, 0, &ch, 1);
                continue;
            }
            if (d >= '0' && d <= '9') {
                rt_add(r, R_GRP, d - '0', NULL, 0);
                continue;
            }
            if (d == 'u' || d == 'l' || d == 'U' || d == 'L' || d == 'e' || d == 'E') {
                rt_add(r, R_CASE, d, NULL, 0);
                continue;
            }
            {
                const char *q = p - 1;
                int n = ch_len(q, (int)strlen(q), 0);
                rt_add(r, R_LIT, 0, q, n);
                if (expanded && n > 1)
                    str_addn(expanded, q + 1, (size_t)(n - 1));
                p = q + n;
                continue;
            }
        }
        {
            int n = ch_len(p, (int)strlen(p), 0);
            rt_add(r, R_LIT, 0, p, n);
            if (expanded)
                str_addn(expanded, p, (size_t)n);
            p += n;
        }
    }
    if (endp)
        *endp = p;
}

Repl *repl_compile(const char *src, int delim, const char **endp, int magic)
{
    Repl *r = xcalloc(1, sizeof *r);
    Str ex;
    const char *e;
    str_init(&ex);
    /* an entire replacement of '%' means the previous replacement */
    e = src;
    if (src[0] == '%' && (src[1] == 0 || src[1] == '\n' || (delim && (unsigned char)src[1] == delim))) {
        if (last_repl)
            repl_parse(r, last_repl, 0, NULL, magic, NULL, 1);
        e = src + 1;
        str_adds(&ex, last_repl ? last_repl : "");
    } else
        repl_parse(r, src, delim, &e, magic, &ex, 0);
    if (endp)
        *endp = e;
    free(last_repl);
    last_repl = str_detach(&ex);
    return r;
}

void repl_free(Repl *r)
{
    if (!r)
        return;
    for (int i = 0; i < r->n; i++)
        free(r->t[i].s);
    free(r->t);
    free(r);
}

static void emit_case(Str *out, const char *s, int len, int *once, int mode)
{
    int p = 0;
    while (p < len) {
        int n = ch_len(s, len, p);
        int conv = *once ? *once : mode;
        if (s[p] == REPL_SPLIT) {
            str_addc(out, s[p]);
            p++;
            continue;
        }
        if (conv) {
            wint_t c = ch_wc(s, len, p);
            if (!(c & 0x40000000u)) {
                wint_t d = (conv == 'u' || conv == 'U') ? (c < 0x80 ? (wint_t)toupper((int)c) : towupper(c))
                                                     : (c < 0x80 ? (wint_t)tolower((int)c) : towlower(c));
                if (d < 0x80 || !mb) {
                    str_addc(out, (char)d);
                } else {
                    char mbuf[MB_LEN_MAX];
                    mbstate_t st;
                    size_t k;
                    memset(&st, 0, sizeof st);
                    k = wcrtomb(mbuf, (wchar_t)d, &st);
                    if (k == (size_t)-1)
                        str_addn(out, s + p, (size_t)n);
                    else
                        str_addn(out, mbuf, k);
                }
            } else
                str_addn(out, s + p, (size_t)n);
            *once = 0;
        } else
            str_addn(out, s + p, (size_t)n);
        p += n;
    }
}

void repl_apply(Repl *r, const char *line, int len, Match *m, Str *out)
{
    int once = 0, mode = 0;
    (void)len;
    for (int i = 0; i < r->n; i++) {
        RTok *t = &r->t[i];
        switch (t->type) {
        case R_LIT:
            emit_case(out, t->s, t->len, &once, mode);
            break;
        case R_GRP:
            if (m->so[t->n] >= 0)
                emit_case(out, line + m->so[t->n], m->eo[t->n] - m->so[t->n], &once, mode);
            break;
        case R_CASE:
            if (t->n == 'u' || t->n == 'l')
                once = t->n;
            else if (t->n == 'U' || t->n == 'L')
                mode = t->n;
            else
                mode = 0;
            break;
        case R_SPLIT:
            str_addc(out, REPL_SPLIT);
            break;
        }
    }
}

Regex *re_ref(Regex *re)
{
    if (re)
        re->refs++;
    return re;
}
