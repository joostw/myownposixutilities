/*
 * buf.c - the edit buffer (a gap array of lines), undo, marks and the
 * text buffers ("registers").
 */
#include "ex.h"

int nlines;
int modified;
long change_count;
long rep_added, rep_deleted;

static Line *arr;
static int cap, gap_start, gap_end;   /* physical gap [gap_start, gap_end) */
static unsigned long next_id = 1;

unsigned long new_line_id(void) { return next_id++; }

Line *lp(int i)
{
    int idx = i - 1;
    if (idx < gap_start)
        return &arr[idx];
    return &arr[idx + (gap_end - gap_start)];
}

static void move_gap(int to)
{
    int glen = gap_end - gap_start;
    if (to < gap_start) {
        int n = gap_start - to;
        memmove(&arr[gap_end - n], &arr[to], (size_t)n * sizeof(Line));
        gap_start = to;
        gap_end -= n;
    } else if (to > gap_start) {
        int n = to - gap_start;
        memmove(&arr[gap_start], &arr[gap_end], (size_t)n * sizeof(Line));
        gap_start += n;
        gap_end += n;
    }
    (void)glen;
}

static void ensure_gap(int n)
{
    int glen = gap_end - gap_start;
    if (glen >= n)
        return;
    {
        int ncap = cap ? cap * 2 : 1024;
        int tail = cap - gap_end;
        while (ncap - (cap - glen) < n)
            ncap *= 2;
        arr = xrealloc(arr, (size_t)ncap * sizeof(Line));
        memmove(&arr[ncap - tail], &arr[gap_end], (size_t)tail * sizeof(Line));
        gap_end = ncap - tail;
        cap = ncap;
    }
}

/* insert raw Line structures (taking ownership) after logical line `after` */
static void raw_insert(int after, Line *ls, int n)
{
    if (n <= 0)
        return;
    ensure_gap(n);
    move_gap(after);
    memcpy(&arr[gap_start], ls, (size_t)n * sizeof(Line));
    gap_start += n;
    nlines += n;
}

/* remove lines from..to, copying the Line structures into out */
static void raw_remove(int from, int to, Line *out)
{
    int n = to - from + 1;
    if (n <= 0)
        return;
    move_gap(to);
    memcpy(out, &arr[gap_start - n], (size_t)n * sizeof(Line));
    gap_start -= n;
    nlines -= n;
}

/* ---------- undo ---------- */

enum { U_INS, U_DEL, U_CHG };
typedef struct UOp {
    int type, at, n;
    Line *lines;    /* U_DEL: saved lines; U_CHG: one saved line */
} UOp;
typedef struct UGroup {
    UOp *ops;
    int n, cap;
} UGroup;

static UGroup ugroup;
static int udepth, need_new = 1, recording = 1;
int undo_inhibit_new;

static void ugroup_free(UGroup *g)
{
    for (int i = 0; i < g->n; i++) {
        UOp *o = &g->ops[i];
        if (o->lines) {
            for (int k = 0; k < (o->type == U_CHG ? 1 : o->n); k++)
                free(o->lines[k].s);
            free(o->lines);
        }
    }
    free(g->ops);
    g->ops = NULL;
    g->n = g->cap = 0;
}

void undo_discard(void)
{
    ugroup_free(&ugroup);
    need_new = 1;
}

void undo_open(void)
{
    if (udepth++ == 0 && !undo_inhibit_new)
        need_new = 1;
}

void undo_close(void)
{
    if (udepth > 0)
        udepth--;
}

static UOp *uop_new(int type)
{
    UOp *o;
    if (need_new) {
        ugroup_free(&ugroup);
        need_new = 0;
    }
    if (ugroup.n == ugroup.cap) {
        ugroup.cap = ugroup.cap ? ugroup.cap * 2 : 8;
        ugroup.ops = xrealloc(ugroup.ops, (size_t)ugroup.cap * sizeof(UOp));
    }
    o = &ugroup.ops[ugroup.n++];
    memset(o, 0, sizeof *o);
    o->type = type;
    return o;
}

static void note_change(void)
{
    modified = 1;
    change_count++;
}

void buf_insert(int after, char *const *texts, const int *lens, int n)
{
    Line *ls;
    if (n <= 0)
        return;
    ls = xmalloc((size_t)n * sizeof(Line));
    for (int i = 0; i < n; i++) {
        ls[i].len = lens[i];
        ls[i].s = xstrndup(texts[i], (size_t)lens[i]);
        ls[i].id = new_line_id();
    }
    raw_insert(after, ls, n);
    free(ls);
    if (recording) {
        UOp *o = uop_new(U_INS);
        o->at = after;
        o->n = n;
    }
    rep_added += n;
    note_change();
}

void buf_insert1(int after, const char *s, int len)
{
    char *t = (char *)s;
    buf_insert(after, &t, &len, 1);
}

void buf_delete(int from, int to)
{
    int n = to - from + 1;
    Line *ls;
    if (n <= 0)
        return;
    ls = xmalloc((size_t)n * sizeof(Line));
    raw_remove(from, to, ls);
    if (recording) {
        UOp *o = uop_new(U_DEL);
        o->at = from - 1;
        o->n = n;
        o->lines = ls;
    } else {
        for (int i = 0; i < n; i++)
            free(ls[i].s);
        free(ls);
    }
    rep_deleted += n;
    note_change();
}

/* move lines a1..a2 after dest, keeping their identities (and marks) */
void buf_move(int a1, int a2, int dest)
{
    int n = a2 - a1 + 1, after;
    Line *ls;
    if (n <= 0)
        return;
    ls = xmalloc((size_t)n * sizeof(Line));
    raw_remove(a1, a2, ls);
    if (recording) {
        UOp *o = uop_new(U_DEL);
        o->at = a1 - 1;
        o->n = n;
        o->lines = xmalloc((size_t)n * sizeof(Line));
        for (int i = 0; i < n; i++) {
            o->lines[i] = ls[i];
            o->lines[i].s = xstrndup(ls[i].s, (size_t)ls[i].len);
        }
    }
    after = dest < a1 ? dest : dest - n;
    raw_insert(after, ls, n);
    free(ls);
    if (recording) {
        UOp *o = uop_new(U_INS);
        o->at = after;
        o->n = n;
    }
    note_change();
}

void buf_change(int i, const char *s, int len)
{
    Line *l = lp(i);
    char *ns = xstrndup(s, (size_t)len);
    if (recording && !need_new && ugroup.n > 0 && ugroup.ops[ugroup.n - 1].type == U_CHG &&
        ugroup.ops[ugroup.n - 1].at == i) {
        /* the line is already saved in this group: keep the oldest copy */
        free(l->s);
    } else if (recording) {
        UOp *o = uop_new(U_CHG);
        o->at = i;
        o->n = 1;
        o->lines = xmalloc(sizeof(Line));
        o->lines[0] = *l;
    } else
        free(l->s);
    l->s = ns;
    l->len = len;
    note_change();
}

void buf_reset(void)
{
    for (int i = 1; i <= nlines; i++)
        free(lp(i)->s);
    free(arr);
    arr = NULL;
    cap = gap_start = gap_end = 0;
    nlines = 0;
    undo_discard();
}

void buf_load_line(const char *s, int len)
{
    Line l;
    l.s = xstrndup(s, (size_t)len);
    l.len = len;
    l.id = new_line_id();
    raw_insert(nlines, &l, 1);
}

int line_of_id(unsigned long id)
{
    static int hint = 1;
    int lo, hi;
    if (nlines == 0)
        return 0;
    if (hint < 1 || hint > nlines)
        hint = 1;
    /* search outward from the last position found */
    lo = hint;
    hi = hint + 1;
    while (lo >= 1 || hi <= nlines) {
        if (hi <= nlines) {
            if (lp(hi)->id == id)
                return hint = hi;
            hi++;
        }
        if (lo >= 1) {
            if (lp(lo)->id == id)
                return hint = lo;
            lo--;
        }
    }
    return 0;
}

/*
 * Undo the last group of changes.  The inverse operations become the new
 * last group, so that undo undoes itself.  Reports the first line added or
 * changed (0 if none) and the line before the first deleted line.
 */
int do_undo(int *first_changed, int *line_before_del, int *what)
{
    UGroup g = ugroup;
    int fc = 0, bd = -1, kinds = 0;
    if (g.n == 0)
        return -1;
    ugroup.ops = NULL;
    ugroup.n = ugroup.cap = 0;
    need_new = 0;
    for (int i = g.n - 1; i >= 0; i--) {
        UOp *o = &g.ops[i];
        switch (o->type) {
        case U_INS:
            buf_delete(o->at + 1, o->at + o->n);
            if (bd < 0 || o->at < bd)
                bd = o->at;
            kinds |= 2;
            break;
        case U_DEL: {
            UOp *r;
            raw_insert(o->at, o->lines, o->n);
            free(o->lines);
            o->lines = NULL;
            r = uop_new(U_INS);
            r->at = o->at;
            r->n = o->n;
            rep_added += o->n;
            note_change();
            if (!fc || o->at + 1 < fc)
                fc = o->at + 1;
            kinds |= 1;
            break;
        }
        case U_CHG: {
            Line *l = lp(o->at);
            UOp *r = uop_new(U_CHG);
            r->at = o->at;
            r->n = 1;
            r->lines = xmalloc(sizeof(Line));
            r->lines[0] = *l;
            l->s = o->lines[0].s;
            l->len = o->lines[0].len;
            free(o->lines);
            o->lines = NULL;
            note_change();
            if (!fc || o->at < fc)
                fc = o->at;
            kinds |= 4;
            break;
        }
        }
    }
    ugroup_free(&g);
    need_new = 1;
    if (first_changed)
        *first_changed = fc;
    if (line_before_del)
        *line_before_del = bd;
    if (what)
        *what = kinds;
    return 0;
}

/* ---------- marks ---------- */

Mark marks[27];

int mark_index(int c)
{
    if (c >= 'a' && c <= 'z')
        return c - 'a';
    if (c == '`' || c == '\'')
        return MARK_CONTEXT;
    return -1;
}

void mark_set(int idx, int line, int off)
{
    if (line < 1 || line > nlines) {
        marks[idx].set = 0;
        return;
    }
    marks[idx].id = lp(line)->id;
    marks[idx].off = off;
    marks[idx].set = 1;
}

int mark_get(int idx, int *line, int *off)
{
    int l;
    if (!marks[idx].set)
        return 0;
    l = line_of_id(marks[idx].id);
    if (!l)
        return 0;
    *line = l;
    if (off)
        *off = marks[idx].off;
    return 1;
}

void set_context_mark(void)
{
    if (nlines > 0 && cur >= 1)
        mark_set(MARK_CONTEXT, cur, cur_off);
}

/* ---------- text buffers ---------- */

Reg regs[36];

int reg_index(int c, int *append)
{
    if (append)
        *append = 0;
    if (c == 0 || c == '"')
        return 0;
    if (c >= '1' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return 10 + c - 'a';
    if (c >= 'A' && c <= 'Z') {
        if (append)
            *append = 1;
        return 10 + c - 'A';
    }
    return -1;
}

void reg_free(Reg *r)
{
    for (int i = 0; i < r->n; i++)
        free(r->t[i]);
    free(r->t);
    free(r->l);
    memset(r, 0, sizeof *r);
}

void reg_copy(Reg *dst, const Reg *src)
{
    Reg n;
    n.linemode = src->linemode;
    n.n = src->n;
    n.t = xmalloc((size_t)(src->n ? src->n : 1) * sizeof(char *));
    n.l = xmalloc((size_t)(src->n ? src->n : 1) * sizeof(int));
    for (int i = 0; i < src->n; i++) {
        n.t[i] = xstrndup(src->t[i], (size_t)src->l[i]);
        n.l[i] = src->l[i];
    }
    reg_free(dst);
    *dst = n;
}

void reg_store(int c, Reg *r, int shift_numeric)
{
    int app, idx = reg_index(c, &app);
    if (idx < 0)
        idx = 0;
    if (shift_numeric) {
        reg_free(&regs[9]);
        for (int i = 9; i > 1; i--)
            regs[i] = regs[i - 1];
        memset(&regs[1], 0, sizeof(Reg));
        reg_copy(&regs[1], r);
    }
    if (idx != 0) {
        if (app && regs[idx].n > 0) {
            Reg *d = &regs[idx];
            d->t = xrealloc(d->t, (size_t)(d->n + r->n) * sizeof(char *));
            d->l = xrealloc(d->l, (size_t)(d->n + r->n) * sizeof(int));
            for (int i = 0; i < r->n; i++) {
                d->t[d->n + i] = r->t[i];
                d->l[d->n + i] = r->l[i];
            }
            d->n += r->n;
            d->linemode = r->linemode;
            free(r->t);
            free(r->l);
        } else {
            reg_free(&regs[idx]);
            regs[idx] = *r;
        }
        reg_copy(&regs[0], &regs[idx]);
    } else {
        reg_free(&regs[0]);
        regs[0] = *r;
    }
    memset(r, 0, sizeof *r);
}

void reg_store_lines(int c, int from, int to, int shift_numeric)
{
    Reg r;
    int n = to - from + 1;
    if (n < 0)
        n = 0;
    r.linemode = 1;
    r.n = n;
    r.t = xmalloc((size_t)(n ? n : 1) * sizeof(char *));
    r.l = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    for (int i = 0; i < n; i++) {
        r.t[i] = xstrndup(LT(from + i), (size_t)LL(from + i));
        r.l[i] = LL(from + i);
    }
    reg_store(c, &r, shift_numeric);
}

Reg *reg_get(int c)
{
    int idx = reg_index(c, NULL);
    if (idx < 0)
        return NULL;
    return &regs[idx];
}
