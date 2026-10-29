/*
 * lalr.c - the LR(0) automaton and LALR(1) lookahead sets, computed
 * with the relations of DeRemer and Pennello (reads, includes,
 * lookback) and their digraph algorithm.
 */
#include "yacc.h"

State *states;
int nstates;
int *nullable;
int **derives;

static int cstates;
static Word **first_derives;     /* per nonterminal: rules added by closure */
static size_t rwords;            /* words in a rule set */
static size_t twords;            /* words in a terminal set */

/* ---------- grammar properties ---------- */

static void compute_derives(void)
{
    int *count = xcalloc((size_t)nsyms, sizeof *count);
    derives = xcalloc((size_t)nsyms, sizeof *derives);
    for (int r = 0; r < nrules; r++)
        count[rules[r].lhs]++;
    for (int s = ntokens; s < nsyms; s++) {
        derives[s] = xmalloc((size_t)(count[s] + 1) * sizeof(int));
        count[s] = 0;
    }
    for (int r = 0; r < nrules; r++) {
        int a = rules[r].lhs;
        derives[a][count[a]++] = r;
    }
    for (int s = ntokens; s < nsyms; s++)
        derives[s][count[s]] = -1;
    free(count);
}

static void compute_nullable(void)
{
    int changed = 1;
    nullable = xcalloc((size_t)nsyms, sizeof *nullable);
    while (changed) {
        changed = 0;
        for (int r = 0; r < nrules; r++) {
            int ok = 1;
            if (nullable[rules[r].lhs])
                continue;
            for (int i = rules[r].rhs; ritem[i] >= 0; i++)
                if (!nullable[ritem[i]]) {
                    ok = 0;
                    break;
                }
            if (ok) {
                nullable[rules[r].lhs] = 1;
                changed = 1;
            }
        }
    }
}

static void compute_first_derives(void)
{
    size_t nw = NWORDS(nnonterms);
    Word *eff = xcalloc((size_t)nnonterms * nw, sizeof(Word));
    int i, j, k;
    /* A -> B ...: B in eff(A); reflexive */
    for (i = 0; i < nnonterms; i++) {
        Word *row = eff + (size_t)i * nw;
        BIT_SET(row, i);
        for (int *r = derives[ntokens + i]; *r >= 0; r++) {
            int s = ritem[rules[*r].rhs];
            if (s >= ntokens)
                BIT_SET(row, s - ntokens);
        }
    }
    /* transitive closure (Warshall) */
    for (k = 0; k < nnonterms; k++) {
        Word *rk = eff + (size_t)k * nw;
        for (i = 0; i < nnonterms; i++) {
            Word *ri = eff + (size_t)i * nw;
            if (BIT_TEST(ri, k))
                for (size_t w = 0; w < nw; w++)
                    ri[w] |= rk[w];
        }
    }
    rwords = NWORDS(nrules);
    first_derives = xcalloc((size_t)nnonterms, sizeof *first_derives);
    for (i = 0; i < nnonterms; i++) {
        Word *row = eff + (size_t)i * nw;
        first_derives[i] = xcalloc(rwords, sizeof(Word));
        for (j = 0; j < nnonterms; j++)
            if (BIT_TEST(row, j))
                for (int *r = derives[ntokens + j]; *r >= 0; r++)
                    BIT_SET(first_derives[i], *r);
    }
    free(eff);
}

/* ---------- LR(0) ---------- */

static Word *ruleset;
static int *closure_buf;

int *closure(const int *kernel, int n, int *nout)
{
    int m = 0, ki = 0;
    memset(ruleset, 0, rwords * sizeof(Word));
    for (int i = 0; i < n; i++) {
        int s = ritem[kernel[i]];
        if (s >= ntokens) {
            Word *fd = first_derives[s - ntokens];
            for (size_t w = 0; w < rwords; w++)
                ruleset[w] |= fd[w];
        }
    }
    /* merge rule start items with the kernel, in item order */
    for (size_t w = 0; w < rwords; w++) {
        Word x = ruleset[w];
        while (x) {
            int b = 0;
            while (!((x >> b) & 1))
                b++;
            x &= ~((Word)1 << b);
            {
                int r = (int)(w * WBITS) + b;
                int item = rules[r].rhs;
                while (ki < n && kernel[ki] < item)
                    closure_buf[m++] = kernel[ki++];
                if (ki < n && kernel[ki] == item)
                    ki++;
                closure_buf[m++] = item;
            }
        }
    }
    while (ki < n)
        closure_buf[m++] = kernel[ki++];
    *nout = m;
    return closure_buf;
}

#define SHASH 4093
static int *shash;            /* chains of states by kernel hash */
static int *snext;

static unsigned kernel_hash(const int *k, int n)
{
    unsigned h = (unsigned)n;
    for (int i = 0; i < n; i++)
        h = h * 31 + (unsigned)k[i];
    return h;
}

static int find_state(const int *k, int n, int sym)
{
    unsigned h = kernel_hash(k, n) % SHASH;
    for (int s = shash[h]; s >= 0; s = snext[s])
        if (states[s].nkernel == n && !memcmp(states[s].kernel, k, (size_t)n * sizeof(int)))
            return s;
    if (nstates == cstates) {
        cstates *= 2;
        states = xrealloc(states, (size_t)cstates * sizeof *states);
        snext = xrealloc(snext, (size_t)cstates * sizeof *snext);
    }
    memset(&states[nstates], 0, sizeof states[0]);
    states[nstates].nkernel = n;
    states[nstates].kernel = xmalloc((size_t)n * sizeof(int));
    memcpy(states[nstates].kernel, k, (size_t)n * sizeof(int));
    states[nstates].accessing = sym;
    snext[nstates] = shash[h];
    shash[h] = nstates;
    return nstates++;
}

int goto_state(int s, int sym)
{
    State *st = &states[s];
    int lo = 0, hi = st->nshifts - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (st->shift_sym[mid] == sym)
            return st->shift_to[mid];
        if (st->shift_sym[mid] < sym)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

void build_lr0(void)
{
    int **bucket, *bcount, *bcap, *touched, ntouched;
    int k0 = 0;

    compute_derives();
    compute_nullable();
    compute_first_derives();
    ruleset = xcalloc(rwords, sizeof(Word));
    closure_buf = xmalloc((size_t)(nitems + 1) * sizeof(int));

    cstates = 256;
    states = xmalloc((size_t)cstates * sizeof *states);
    snext = xmalloc((size_t)cstates * sizeof *snext);
    shash = xmalloc(SHASH * sizeof *shash);
    for (int i = 0; i < SHASH; i++)
        shash[i] = -1;
    nstates = 0;
    find_state(&k0, 1, -1);

    bucket = xcalloc((size_t)nsyms, sizeof *bucket);
    bcount = xcalloc((size_t)nsyms, sizeof *bcount);
    bcap = xcalloc((size_t)nsyms, sizeof *bcap);
    touched = xmalloc((size_t)nsyms * sizeof *touched);

    for (int s = 0; s < nstates; s++) {
        int n, *items;
        int nred = 0;
        int *kernel = states[s].kernel;
        items = closure(kernel, states[s].nkernel, &n);
        ntouched = 0;
        for (int i = 0; i < n; i++) {
            int sym = ritem[items[i]];
            if (sym < 0) {
                nred++;
                continue;
            }
            if (!bcount[sym])
                touched[ntouched++] = sym;
            if (bcount[sym] == bcap[sym]) {
                bcap[sym] = bcap[sym] ? bcap[sym] * 2 : 8;
                bucket[sym] = xrealloc(bucket[sym], (size_t)bcap[sym] * sizeof(int));
            }
            bucket[sym][bcount[sym]++] = items[i] + 1;
        }
        states[s].nreds = nred;
        states[s].red_rule = xmalloc((size_t)(nred ? nred : 1) * sizeof(int));
        nred = 0;
        for (int i = 0; i < n; i++)
            if (ritem[items[i]] < 0)
                states[s].red_rule[nred++] = -ritem[items[i]] - 1;
        /* sort the touched symbols */
        for (int i = 1; i < ntouched; i++) {
            int v = touched[i], j = i - 1;
            while (j >= 0 && touched[j] > v) {
                touched[j + 1] = touched[j];
                j--;
            }
            touched[j + 1] = v;
        }
        {
            int *ss = xmalloc((size_t)(ntouched ? ntouched : 1) * sizeof(int));
            int *st = xmalloc((size_t)(ntouched ? ntouched : 1) * sizeof(int));
            for (int i = 0; i < ntouched; i++) {
                int sym = touched[i];
                ss[i] = sym;
                st[i] = find_state(bucket[sym], bcount[sym], sym);
                bcount[sym] = 0;
            }
            /* find_state may have moved the states array */
            states[s].nshifts = ntouched;
            states[s].shift_sym = ss;
            states[s].shift_to = st;
        }
    }
    for (int i = 0; i < nsyms; i++)
        free(bucket[i]);
    free(bucket);
    free(bcount);
    free(bcap);
    free(touched);
    free(shash);
    free(snext);
}

/* ---------- LALR(1) ---------- */

static int ngotos;
static int *gfrom, *gto, *gsym;
static int *gbase;            /* per state: goto index of its first nonterminal shift */
static int *gfirst;           /* per state: position of its first nonterminal shift */

static int find_goto(int s, int sym)
{
    State *st = &states[s];
    int lo = gfirst[s], hi = st->nshifts - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (st->shift_sym[mid] == sym)
            return gbase[s] + (mid - gfirst[s]);
        if (st->shift_sym[mid] < sym)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    fatal("internal error: no goto from state %d on %s", s, symbols[sym]->name);
    return -1;
}

typedef struct Edges {
    int n, cap;
    int *v;
} Edges;

static void edge_add(Edges *e, int x)
{
    if (e->n == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 4;
        e->v = xrealloc(e->v, (size_t)e->cap * sizeof(int));
    }
    e->v[e->n++] = x;
}

/* the digraph algorithm: F(x) = F'(x) U { F(y) : x R y } */
static int *dg_n, *dg_stack, dg_top;
static Word *dg_f;
static Edges *dg_r;

static void traverse(int x)
{
    int d;
    dg_stack[dg_top++] = x;
    d = dg_top;
    dg_n[x] = d;
    for (int i = 0; i < dg_r[x].n; i++) {
        int y = dg_r[x].v[i];
        Word *fx, *fy;
        if (dg_n[y] == 0)
            traverse(y);
        if (dg_n[y] < dg_n[x])
            dg_n[x] = dg_n[y];
        fx = dg_f + (size_t)x * twords;
        fy = dg_f + (size_t)y * twords;
        for (size_t w = 0; w < twords; w++)
            fx[w] |= fy[w];
    }
    if (dg_n[x] == d) {
        Word *fx = dg_f + (size_t)x * twords;
        for (;;) {
            int top = dg_stack[--dg_top];
            dg_n[top] = INT_MAX;
            if (top == x)
                break;
            memcpy(dg_f + (size_t)top * twords, fx, twords * sizeof(Word));
        }
    }
}

static void digraph(Edges *r, Word *f)
{
    dg_n = xcalloc((size_t)ngotos + 1, sizeof(int));
    dg_stack = xmalloc(((size_t)ngotos + 1) * sizeof(int));
    dg_top = 0;
    dg_f = f;
    dg_r = r;
    for (int x = 0; x < ngotos; x++)
        if (dg_n[x] == 0)
            traverse(x);
    free(dg_n);
    free(dg_stack);
}

void build_lalr(void)
{
    Word *F;
    Edges *reads, *includes;
    Edges **lookback;          /* per state, per reduction */
    int *path;

    twords = NWORDS(ntokens);

    /* the nonterminal transitions */
    gbase = xmalloc((size_t)nstates * sizeof(int));
    gfirst = xmalloc((size_t)nstates * sizeof(int));
    ngotos = 0;
    for (int s = 0; s < nstates; s++) {
        int j = 0;
        while (j < states[s].nshifts && states[s].shift_sym[j] < ntokens)
            j++;
        gfirst[s] = j;
        gbase[s] = ngotos;
        ngotos += states[s].nshifts - j;
    }
    gfrom = xmalloc(((size_t)ngotos + 1) * sizeof(int));
    gto = xmalloc(((size_t)ngotos + 1) * sizeof(int));
    gsym = xmalloc(((size_t)ngotos + 1) * sizeof(int));
    for (int s = 0; s < nstates; s++)
        for (int j = gfirst[s]; j < states[s].nshifts; j++) {
            int g = gbase[s] + j - gfirst[s];
            gfrom[g] = s;
            gto[g] = states[s].shift_to[j];
            gsym[g] = states[s].shift_sym[j];
        }

    /* DR and reads */
    F = xcalloc(((size_t)ngotos + 1) * twords, sizeof(Word));
    reads = xcalloc((size_t)ngotos + 1, sizeof(Edges));
    for (int g = 0; g < ngotos; g++) {
        State *r = &states[gto[g]];
        Word *f = F + (size_t)g * twords;
        for (int j = 0; j < r->nshifts; j++) {
            int sym = r->shift_sym[j];
            if (sym < ntokens)
                BIT_SET(f, sym);
            else if (nullable[sym])
                edge_add(&reads[g], find_goto(gto[g], sym));
        }
    }
    digraph(reads, F);

    /* includes and lookback */
    includes = xcalloc((size_t)ngotos + 1, sizeof(Edges));
    lookback = xcalloc((size_t)nstates, sizeof *lookback);
    for (int s = 0; s < nstates; s++)
        lookback[s] = xcalloc((size_t)(states[s].nreds ? states[s].nreds : 1), sizeof(Edges));
    {
        int maxlen = 0;
        for (int r = 0; r < nrules; r++)
            if (rules[r].len > maxlen)
                maxlen = rules[r].len;
        path = xmalloc(((size_t)maxlen + 1) * sizeof(int));
    }
    for (int g = 0; g < ngotos; g++) {
        int B = gsym[g];
        for (int *rp = derives[B]; *rp >= 0; rp++) {
            int r = *rp, s = gfrom[g], n = 0;
            for (int i = rules[r].rhs; ritem[i] >= 0; i++) {
                path[n++] = s;
                s = goto_state(s, ritem[i]);
                if (s < 0)
                    fatal("internal error: broken path");
            }
            /* s reduces r: lookback */
            {
                State *q = &states[s];
                int lo = 0, hi = q->nreds - 1, k = -1;
                while (lo <= hi) {
                    int mid = (lo + hi) / 2;
                    if (q->red_rule[mid] == r) {
                        k = mid;
                        break;
                    }
                    if (q->red_rule[mid] < r)
                        lo = mid + 1;
                    else
                        hi = mid - 1;
                }
                if (k < 0)
                    fatal("internal error: missing reduction");
                edge_add(&lookback[s][k], g);
            }
            /* includes: suffix nullable */
            for (int i = n - 1; i >= 0; i--) {
                int sym = ritem[rules[r].rhs + i];
                if (sym >= ntokens)
                    edge_add(&includes[find_goto(path[i], sym)], g);
                if (!nullable[sym])
                    break;
            }
        }
    }
    free(path);
    digraph(includes, F);

    /* the lookahead sets */
    for (int s = 0; s < nstates; s++) {
        State *q = &states[s];
        q->la = xmalloc((size_t)(q->nreds ? q->nreds : 1) * sizeof(Word *));
        for (int k = 0; k < q->nreds; k++) {
            Word *la = xcalloc(twords, sizeof(Word));
            Edges *e = &lookback[s][k];
            for (int i = 0; i < e->n; i++) {
                Word *f = F + (size_t)e->v[i] * twords;
                for (size_t w = 0; w < twords; w++)
                    la[w] |= f[w];
            }
            q->la[k] = la;
            free(e->v);
        }
        free(lookback[s]);
    }
    free(lookback);
    for (int g = 0; g < ngotos; g++) {
        free(reads[g].v);
        free(includes[g].v);
    }
    free(reads);
    free(includes);
    free(F);
    free(gfrom);
    free(gto);
    free(gsym);
    free(gbase);
    free(gfirst);
}
