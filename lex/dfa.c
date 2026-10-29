/*
 * dfa.c - from the rules to the scanner's automaton: Thompson NFAs,
 * byte equivalence classes, the subset construction (one start state
 * per start condition, and one for the beginning of a line), separate
 * automata for the two parts of variable trailing context, and Moore
 * minimization.
 */
#include "lex.h"

DState *dstates;
int ndstates;
int nclasses;
int ec[256];
int *start_state;
int *tc_rstart, *tc_xstart;
int nnfa;
int ndstates_before_min;

/* ---------- NFA ---------- */

enum { NS_EPS, NS_SET, NS_ACC };
typedef struct NState {
    int type;
    int set;
    int out1, out2;
    int acc;                 /* rule */
    int acckind;             /* 0 a rule, 1 the r of a rule, 2 its x */
} NState;

static NState *ns;
static int cns;

static int nnew(int type)
{
    if (nnfa == cns) {
        cns = cns ? cns * 2 : 1024;
        ns = xrealloc(ns, (size_t)cns * sizeof *ns);
    }
    ns[nnfa].type = type;
    ns[nnfa].set = -1;
    ns[nnfa].out1 = ns[nnfa].out2 = -1;
    ns[nnfa].acc = -1;
    ns[nnfa].acckind = 0;
    return nnfa++;
}

typedef struct Frag {
    int s, e;                /* e: an NS_EPS state with free outs */
} Frag;

static Frag build(Node *n)
{
    Frag f, a, b;
    switch (n->type) {
    case N_EMPTY:
        f.s = f.e = nnew(NS_EPS);
        return f;
    case N_SET:
        f.s = nnew(NS_SET);
        ns[f.s].set = n->set;
        f.e = nnew(NS_EPS);
        ns[f.s].out1 = f.e;
        return f;
    case N_CAT:
        a = build(n->a);
        b = build(n->b);
        ns[a.e].out1 = b.s;
        f.s = a.s;
        f.e = b.e;
        return f;
    case N_ALT:
        a = build(n->a);
        b = build(n->b);
        f.s = nnew(NS_EPS);
        f.e = nnew(NS_EPS);
        ns[f.s].out1 = a.s;
        ns[f.s].out2 = b.s;
        ns[a.e].out1 = f.e;
        ns[b.e].out1 = f.e;
        return f;
    case N_STAR:
    case N_PLUS:
    case N_QUEST:
        a = build(n->a);
        f.s = nnew(NS_EPS);
        f.e = nnew(NS_EPS);
        ns[f.s].out1 = a.s;
        if (n->type != N_PLUS)
            ns[f.s].out2 = f.e;
        ns[a.e].out1 = f.e;
        if (n->type != N_QUEST)
            ns[a.e].out2 = a.s;
        return f;
    case N_REP: {
        int first = 1;
        f.s = f.e = -1;
        for (int i = 0; i < n->min; i++) {
            a = build(n->a);
            if (first) {
                f = a;
                first = 0;
            } else {
                ns[f.e].out1 = a.s;
                f.e = a.e;
            }
        }
        if (n->max < 0) {
            Node star = { N_STAR, 0, n->a, NULL, 0, 0 };
            a = build(&star);
            if (first) {
                f = a;
                first = 0;
            } else {
                ns[f.e].out1 = a.s;
                f.e = a.e;
            }
        } else
            for (int i = n->min; i < n->max; i++) {
                Node q = { N_QUEST, 0, n->a, NULL, 0, 0 };
                a = build(&q);
                if (first) {
                    f = a;
                    first = 0;
                } else {
                    ns[f.e].out1 = a.s;
                    f.e = a.e;
                }
            }
        if (first)
            f.s = f.e = nnew(NS_EPS);
        return f;
    }
    }
    fatal("internal error: bad node");
    return f;
}

/* a rule's (or part's) automaton ending in an accepting state */
static int build_accepting(Node *n, int rule, int kind)
{
    Frag f = build(n);
    int acc = nnew(NS_ACC);
    ns[acc].acc = rule;
    ns[acc].acckind = kind;
    ns[f.e].out1 = acc;
    return f.s;
}

/* ---------- equivalence classes ---------- */

static int rep[256];         /* a byte of each class */
static unsigned char *member; /* [set * nclasses + class] */

static void compute_classes(void)
{
    int newc[256];
    for (int c = 0; c < 256; c++)
        ec[c] = 0;
    nclasses = 1;
    for (int s = 0; s < nsets; s++) {
        int map[2][256];
        int n = 0;
        for (int i = 0; i < 256; i++)
            map[0][i] = map[1][i] = -1;
        for (int c = 0; c < 256; c++) {
            int in = CS_HAS(&sets[s], c);
            if (map[in][ec[c]] < 0)
                map[in][ec[c]] = n++;
            newc[c] = map[in][ec[c]];
        }
        memcpy(ec, newc, sizeof ec);
        nclasses = n;
    }
    for (int c = 255; c >= 0; c--)
        rep[ec[c]] = c;
    member = xmalloc((size_t)(nsets ? nsets : 1) * (size_t)nclasses);
    for (int s = 0; s < nsets; s++)
        for (int k = 0; k < nclasses; k++)
            member[(size_t)s * nclasses + k] = (unsigned char)CS_HAS(&sets[s], rep[k]);
}

/* ---------- subset construction ---------- */

typedef struct Key {
    int n;
    int *v;                  /* sorted important NFA states */
} Key;

static Key *keys;
static int cdstates;
static int *dhash, hsize;
static int *dnext;
static int *stack, *mark, markgen;

static unsigned key_hash(const int *v, int n)
{
    unsigned h = (unsigned)n * 2654435761u;
    for (int i = 0; i < n; i++)
        h = h * 31 + (unsigned)v[i];
    return h;
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return x < y ? -1 : x > y;
}

/* the epsilon closure of v[0..n), as sorted important states into out */
static int closure(const int *v, int n, int *out)
{
    int sp = 0, m = 0;
    markgen++;
    for (int i = 0; i < n; i++)
        if (mark[v[i]] != markgen) {
            mark[v[i]] = markgen;
            stack[sp++] = v[i];
        }
    while (sp) {
        int s = stack[--sp];
        if (ns[s].type == NS_EPS) {
            int o[2] = { ns[s].out1, ns[s].out2 };
            for (int k = 0; k < 2; k++)
                if (o[k] >= 0 && mark[o[k]] != markgen) {
                    mark[o[k]] = markgen;
                    stack[sp++] = o[k];
                }
        } else
            out[m++] = s;
    }
    qsort(out, (size_t)m, sizeof *out, cmp_int);
    return m;
}

static void rehash(void)
{
    int n = hsize ? hsize * 2 : 4096;
    free(dhash);
    dhash = xmalloc((size_t)n * sizeof *dhash);
    for (int i = 0; i < n; i++)
        dhash[i] = -1;
    hsize = n;
    for (int d = 0; d < ndstates; d++) {
        unsigned h = key_hash(keys[d].v, keys[d].n) & (unsigned)(hsize - 1);
        dnext[d] = dhash[h];
        dhash[h] = d;
    }
}

static int find_dstate(const int *v, int n)
{
    unsigned h;
    if (n == 0)
        return -1;
    if (!hsize)
        rehash();
    h = key_hash(v, n) & (unsigned)(hsize - 1);
    for (int d = dhash[h]; d >= 0; d = dnext[d])
        if (keys[d].n == n && !memcmp(keys[d].v, v, (size_t)n * sizeof *v))
            return d;
    if (ndstates == cdstates) {
        cdstates = cdstates ? cdstates * 2 : 256;
        keys = xrealloc(keys, (size_t)cdstates * sizeof *keys);
        dnext = xrealloc(dnext, (size_t)cdstates * sizeof *dnext);
        dstates = xrealloc(dstates, (size_t)cdstates * sizeof *dstates);
    }
    keys[ndstates].n = n;
    keys[ndstates].v = xmalloc((size_t)n * sizeof *v);
    memcpy(keys[ndstates].v, v, (size_t)n * sizeof *v);
    memset(&dstates[ndstates], 0, sizeof dstates[0]);
    dnext[ndstates] = dhash[h];
    dhash[h] = ndstates;
    ndstates++;
    if (ndstates > hsize)
        rehash();
    return ndstates - 1;
}

static int start_from(const int *v, int n, int *buf)
{
    int m = closure(v, n, buf);
    return find_dstate(buf, m);
}

static void subset(void)
{
    int *buf = xmalloc((size_t)nnfa * sizeof(int));
    int *mv = xmalloc((size_t)nnfa * sizeof(int));
    for (int d = 0; d < ndstates; d++) {
        Key k = keys[d];
        int *acc = xmalloc((size_t)(k.n + 1) * sizeof(int));
        int nacc = 0;
        dstates[d].trans = xmalloc((size_t)nclasses * sizeof(int));
        for (int c = 0; c < nclasses; c++) {
            int m = 0, t;
            for (int i = 0; i < keys[d].n; i++) {
                NState *s = &ns[keys[d].v[i]];
                if (s->type == NS_SET && member[(size_t)s->set * nclasses + c])
                    mv[m++] = s->out1;
            }
            t = m ? start_from(mv, m, buf) : -1;
            dstates[d].trans[c] = t;
        }
        for (int i = 0; i < keys[d].n; i++) {
            NState *s = &ns[keys[d].v[i]];
            if (s->type != NS_ACC)
                continue;
            if (s->acckind == 0)
                acc[nacc++] = s->acc;
            else if (s->acckind == 1)
                dstates[d].racc = 1;
            else
                dstates[d].xacc = 1;
        }
        qsort(acc, (size_t)nacc, sizeof *acc, cmp_int);
        dstates[d].nacc = nacc;
        dstates[d].acc = acc;
    }
    free(buf);
    free(mv);
}

/* ---------- minimization ---------- */

static int *blk;
static int nblocks;

static unsigned sig_hash(int d, const int *cls)
{
    unsigned h = (unsigned)cls[d] * 2654435761u;
    for (int c = 0; c < nclasses; c++) {
        int t = dstates[d].trans[c];
        h = h * 31 + (unsigned)(t < 0 ? -1 : cls[t]);
    }
    return h;
}

static int same_sig(int a, int b, const int *cls)
{
    if (cls[a] != cls[b])
        return 0;
    for (int c = 0; c < nclasses; c++) {
        int x = dstates[a].trans[c], y = dstates[b].trans[c];
        if ((x < 0) != (y < 0) || (x >= 0 && cls[x] != cls[y]))
            return 0;
    }
    return 1;
}

static int same_accept(int a, int b)
{
    return dstates[a].nacc == dstates[b].nacc && dstates[a].racc == dstates[b].racc &&
           dstates[a].xacc == dstates[b].xacc &&
           !memcmp(dstates[a].acc, dstates[b].acc, (size_t)dstates[a].nacc * sizeof(int));
}

static void minimize(void)
{
    int n = ndstates, hs = 1;
    int *cls = xmalloc((size_t)n * sizeof(int)), *ncls = xmalloc((size_t)n * sizeof(int));
    int *head, *nxt = xmalloc((size_t)n * sizeof(int)), *repr = xmalloc((size_t)n * sizeof(int));
    int nb;
    while (hs < 2 * n)
        hs *= 2;
    head = xmalloc((size_t)hs * sizeof(int));

    /* initial blocks: the same acceptance */
    for (int i = 0; i < hs; i++)
        head[i] = -1;
    nb = 0;
    for (int d = 0; d < n; d++) {
        unsigned h = (unsigned)dstates[d].nacc * 7 + (unsigned)dstates[d].racc * 3 + (unsigned)dstates[d].xacc;
        int b;
        for (int i = 0; i < dstates[d].nacc; i++)
            h = h * 31 + (unsigned)dstates[d].acc[i];
        h &= (unsigned)(hs - 1);
        for (b = head[h]; b >= 0; b = nxt[b])
            if (same_accept(repr[b], d))
                break;
        if (b < 0) {
            b = nb++;
            repr[b] = d;
            nxt[b] = head[h];
            head[h] = b;
        }
        cls[d] = b;
    }
    /* refine until stable */
    for (;;) {
        int nn = 0;
        for (int i = 0; i < hs; i++)
            head[i] = -1;
        for (int d = 0; d < n; d++) {
            unsigned h = sig_hash(d, cls) & (unsigned)(hs - 1);
            int b;
            for (b = head[h]; b >= 0; b = nxt[b])
                if (same_sig(repr[b], d, cls))
                    break;
            if (b < 0) {
                b = nn++;
                repr[b] = d;
                nxt[b] = head[h];
                head[h] = b;
            }
            ncls[d] = b;
        }
        memcpy(cls, ncls, (size_t)n * sizeof(int));
        if (nn == nb)
            break;
        nb = nn;
    }
    /* the new states, in order of first appearance */
    {
        DState *nd = xcalloc((size_t)nb, sizeof *nd);
        int *done = xcalloc((size_t)nb, sizeof(int));
        for (int d = 0; d < n; d++) {
            int b = cls[d];
            if (done[b]) {
                free(dstates[d].trans);
                free(dstates[d].acc);
                continue;
            }
            done[b] = 1;
            nd[b] = dstates[d];
            for (int c = 0; c < nclasses; c++)
                if (nd[b].trans[c] >= 0)
                    nd[b].trans[c] = cls[nd[b].trans[c]];
        }
        free(dstates);
        free(done);
        dstates = nd;
    }
    blk = cls;
    nblocks = nb;
    ndstates = nb;
    free(ncls);
    free(head);
    free(nxt);
    free(repr);
}

void build_automata(void)
{
    int nstarts = 2 * nsconds;
    int **starts = xcalloc((size_t)nrules, sizeof *starts);
    int *rstart = xmalloc((size_t)nrules * sizeof(int));
    int *list = xmalloc(((size_t)nrules + 1) * sizeof(int));
    int *buf;

    for (int r = 0; r < nrules; r++)
        rstart[r] = build_accepting(rules[r].re, r, 0);
    tc_rstart = xmalloc((size_t)nrules * sizeof(int));
    tc_xstart = xmalloc((size_t)nrules * sizeof(int));
    {
        int *rn = xmalloc((size_t)nrules * sizeof(int)), *xn = xmalloc((size_t)nrules * sizeof(int));
        for (int r = 0; r < nrules; r++) {
            rn[r] = xn[r] = -1;
            if (rules[r].trailing && rules[r].rlen < 0 && rules[r].xlen < 0) {
                rn[r] = build_accepting(rules[r].r, r, 1);
                xn[r] = build_accepting(rules[r].x, r, 2);
            }
        }
        compute_classes();
        stack = xmalloc((size_t)nnfa * sizeof(int));
        mark = xcalloc((size_t)nnfa, sizeof(int));
        buf = xmalloc((size_t)nnfa * sizeof(int));

        start_state = xmalloc((size_t)nstarts * sizeof(int));
        for (int sc = 0; sc < nsconds; sc++)
            for (int bol = 0; bol < 2; bol++) {
                int n = 0;
                for (int r = 0; r < nrules; r++)
                    if (rules[r].sc[sc] && (bol || !rules[r].bol))
                        list[n++] = rstart[r];
                start_state[2 * sc + bol] = n ? start_from(list, n, buf) : -1;
            }
        for (int r = 0; r < nrules; r++) {
            tc_rstart[r] = rn[r] >= 0 ? start_from(&rn[r], 1, buf) : -1;
            tc_xstart[r] = xn[r] >= 0 ? start_from(&xn[r], 1, buf) : -1;
        }
        free(rn);
        free(xn);
    }
    subset();
    ndstates_before_min = ndstates;
    minimize();
    for (int i = 0; i < nstarts; i++)
        if (start_state[i] >= 0)
            start_state[i] = blk[start_state[i]];
    for (int r = 0; r < nrules; r++) {
        if (tc_rstart[r] >= 0)
            tc_rstart[r] = blk[tc_rstart[r]];
        if (tc_xstart[r] >= 0)
            tc_xstart[r] = blk[tc_xstart[r]];
    }
    free(starts);
    free(rstart);
    free(list);
    free(buf);
}
