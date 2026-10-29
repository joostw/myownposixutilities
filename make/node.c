/*
 * node.c - the table of targets and prerequisites, and of inference
 * rules.
 */
#include "make.h"

#define NHASH 4096
static Node *ntab[NHASH];
static Node wait_storage = { .name = ".WAIT" };
Node *wait_node = &wait_storage;

static unsigned hash(const char *s)
{
    unsigned h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h % NHASH;
}

Node *node_find(const char *name)
{
    for (Node *n = ntab[hash(name)]; n; n = n->hnext)
        if (strcmp(n->name, name) == 0)
            return n;
    return NULL;
}

Node *node_get(const char *name)
{
    Node *n = node_find(name);
    unsigned h;
    if (n)
        return n;
    h = hash(name);
    n = xcalloc(1, sizeof *n);
    n->name = xstrdup(name);
    parse_lib_member(name, &n->lib, &n->member);
    n->hnext = ntab[h];
    ntab[h] = n;
    return n;
}

void node_add_pre(Node *n, Node *p)
{
    if (n->npre + 1 > n->cappre) {
        n->cappre = n->cappre ? n->cappre * 2 : 4;
        n->pre = xrealloc(n->pre, (size_t)n->cappre * sizeof(Node *));
    }
    n->pre[n->npre++] = p;
}

void node_foreach(void (*fn)(Node *n))
{
    for (int i = 0; i < NHASH; i++)
        for (Node *n = ntab[i]; n; n = n->hnext)
            fn(n);
}

static void reset(Node *n)
{
    n->state = ST_NEW;
    n->ok = 0;
    n->stat_done = 0;
    n->newer = 0;
    n->made = 0;
    n->visiting = 0;
    free(n->all);
    n->all = NULL;
    n->nall = 0;
    n->implicit = NULL;
    n->rcmds = NULL;
    free(n->stem);
    n->stem = NULL;
    n->from_default = 0;
    n->dflt_ar = 0;
}

void node_reset_all(void)
{
    node_foreach(reset);
}

/* ---------- inference rules ---------- */

static Infer *itab;

Infer *infer_get(const char *name)
{
    for (Infer *r = itab; r; r = r->next)
        if (strcmp(r->name, name) == 0)
            return r;
    return NULL;
}

void infer_set(const char *name, Cmds *c)
{
    Infer *r = infer_get(name);
    if (!r) {
        r = xcalloc(1, sizeof *r);
        r->name = xstrdup(name);
        r->next = itab;
        itab = r;
    }
    r->cmds = c;
}

void infer_clear_all(void)
{
    itab = NULL;
}

void infer_foreach(void (*fn)(Infer *r))
{
    for (Infer *r = itab; r; r = r->next)
        fn(r);
}
