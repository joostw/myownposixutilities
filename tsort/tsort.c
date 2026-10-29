/*
 * tsort.c - tsort: topological sort (POSIX.1-2024, XCU tsort)
 *
 * Synopsis: tsort [-w] [file]
 *
 * The items are read in pairs (separated by blanks and newlines); a pair
 * of different items orders them, a pair of the same item only names it.
 * The order is found with Kahn's algorithm, taking among the items that
 * are free at each step the one that became free first (in the order of
 * the input at the start), so the output is the same for the same input.
 * When only items in or after cycles are left, a cycle is found by
 * following predecessors from the earliest remaining item; it is reported
 * with its items, and broken by writing one of them.
 *
 * Choices where the standard leaves room:
 *  - Only <blank> and <newline> separate items.
 *  - Without -w the cycle messages are warnings: the exit status stays 0.
 *    With -w they are diagnostics, and the exit status is the number of
 *    cycles found, at most 125.
 *  - An odd number of items is an error (status 1, nothing written).
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *xrealloc(void *p, size_t n)
{
    if (!(p = realloc(p, n ? n : 1))) {
        fprintf(stderr, "tsort: out of memory\n");
        exit(1);
    }
    return p;
}

struct node {
    char *name;
    size_t *succ, *pred;
    size_t nsucc, capsucc, npred, cappred;
    size_t indeg;
    bool done;
    size_t next;                /* hash chain */
};

static struct node *nodes;
static size_t nnodes, capnodes;
static size_t *bucket;
static size_t nbucket;

static uint64_t hash(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 1099511628211ull;
    return h;
}

static void rehash(void)
{
    nbucket = nbucket ? nbucket * 2 : 1024;
    free(bucket);
    bucket = xrealloc(NULL, nbucket * sizeof *bucket);
    for (size_t i = 0; i < nbucket; i++)
        bucket[i] = SIZE_MAX;
    for (size_t i = 0; i < nnodes; i++) {
        size_t b = hash(nodes[i].name) & (nbucket - 1);
        nodes[i].next = bucket[b];
        bucket[b] = i;
    }
}

static size_t node(const char *name)
{
    if (!nbucket)
        rehash();
    size_t b = hash(name) & (nbucket - 1);
    for (size_t i = bucket[b]; i != SIZE_MAX; i = nodes[i].next)
        if (strcmp(nodes[i].name, name) == 0)
            return i;
    if (nnodes == capnodes) {
        capnodes = capnodes ? capnodes * 2 : 1024;
        nodes = xrealloc(nodes, capnodes * sizeof *nodes);
    }
    size_t l = strlen(name) + 1;
    nodes[nnodes] = (struct node){ memcpy(xrealloc(NULL, l), name, l), NULL, NULL, 0, 0, 0, 0, 0,
                                   false, bucket[b] };
    bucket[b] = nnodes;
    if (++nnodes > nbucket)
        rehash();
    return nnodes - 1;
}

static void edge(size_t a, size_t b)
{
    struct node *n = &nodes[a];
    if (n->nsucc == n->capsucc) {
        n->capsucc = n->capsucc ? n->capsucc * 2 : 4;
        n->succ = xrealloc(n->succ, n->capsucc * sizeof *n->succ);
    }
    n->succ[n->nsucc++] = b;
    struct node *m = &nodes[b];
    if (m->npred == m->cappred) {
        m->cappred = m->cappred ? m->cappred * 2 : 4;
        m->pred = xrealloc(m->pred, m->cappred * sizeof *m->pred);
    }
    m->pred[m->npred++] = a;
    m->indeg++;
}

static void put(const char *s)
{
    if (puts(s) == EOF) {
        fprintf(stderr, "tsort: write error: %s\n", strerror(errno));
        exit(1);
    }
}

/* only items in cycles, or after them, are left: from item s, follow
   predecessors that are not written (every one left has one) until an
   item repeats; report that cycle in its order; the item to break it at */
static size_t report_cycle(size_t s)
{
    size_t *path = NULL, np = 0, cap = 0;
    size_t *pos = xrealloc(NULL, nnodes * sizeof *pos);
    for (size_t i = 0; i < nnodes; i++)
        pos[i] = SIZE_MAX;
    size_t cur = s;
    while (pos[cur] == SIZE_MAX) {
        if (np == cap) {
            cap = cap ? cap * 2 : 16;
            path = xrealloc(path, cap * sizeof *path);
        }
        pos[cur] = np;
        path[np++] = cur;
        size_t prev = SIZE_MAX;
        for (size_t k = 0; k < nodes[cur].npred; k++)
            if (!nodes[nodes[cur].pred[k]].done) {
                prev = nodes[cur].pred[k];
                break;
            }
        if (prev == SIZE_MAX)
            break;              /* cannot happen */
        cur = prev;
    }
    /* path[pos[cur]] .. path[np-1] is the cycle, backwards */
    fprintf(stderr, "tsort: a cycle:");
    size_t first = pos[cur] != SIZE_MAX ? pos[cur] : 0;
    for (size_t i = np; i > first; i--)
        fprintf(stderr, " %s", nodes[path[i - 1]].name);
    fputc('\n', stderr);
    size_t r = path[np - 1];
    free(path);
    free(pos);
    return r;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool opt_w = false;
    int c;
    while ((c = getopt(argc, argv, "w")) != -1) {
        if (c == 'w')
            opt_w = true;
        else {
            fprintf(stderr, "usage: tsort [-w] [file]\n");
            return 1;
        }
    }
    if (argc - optind > 1) {
        fprintf(stderr, "usage: tsort [-w] [file]\n");
        return 1;
    }
    FILE *fp = stdin;
    const char *name = "standard input";
    if (optind < argc && strcmp(argv[optind], "-") != 0) {
        name = argv[optind];
        if (!(fp = fopen(name, "r"))) {
            fprintf(stderr, "tsort: %s: %s\n", name, strerror(errno));
            return 1;
        }
    }
    /* read the items */
    char *item = NULL;
    size_t len = 0, cap = 0;
    size_t pending = SIZE_MAX;  /* the first item of a pair */
    int ch;
    for (;;) {
        ch = getc(fp);
        if (ch == EOF || ch == ' ' || ch == '\t' || ch == '\n') {
            if (len > 0) {
                item[len] = '\0';
                size_t id = node(item);
                if (pending == SIZE_MAX)
                    pending = id;
                else {
                    if (pending != id)
                        edge(pending, id);
                    pending = SIZE_MAX;
                }
                len = 0;
            }
            if (ch == EOF)
                break;
            continue;
        }
        if (len + 2 > cap) {
            cap = cap ? cap * 2 : 64;
            item = xrealloc(item, cap);
        }
        item[len++] = (char)ch;
    }
    if (ferror(fp)) {
        fprintf(stderr, "tsort: %s: %s\n", name, strerror(errno));
        return 1;
    }
    if (pending != SIZE_MAX) {
        fprintf(stderr, "tsort: %s: an odd number of items\n", name);
        return 1;
    }
    /* Kahn's algorithm, the free items in the order they appeared */
    size_t *queue = xrealloc(NULL, (nnodes + 1) * sizeof *queue);
    size_t head = 0, tail = 0, written = 0, cycles = 0, scan = 0;
    for (size_t i = 0; i < nnodes; i++)
        if (nodes[i].indeg == 0)
            queue[tail++] = i;
    while (written < nnodes) {
        if (head == tail) {
            /* only cycles are left: report one, break it at its first item */
            while (nodes[scan].done)
                scan++;
            size_t r = report_cycle(scan);
            cycles++;
            nodes[r].indeg = 0;
            queue[tail++] = r;
        }
        size_t i = queue[head++];
        if (nodes[i].done)
            continue;
        nodes[i].done = true;
        written++;
        put(nodes[i].name);
        for (size_t k = 0; k < nodes[i].nsucc; k++) {
            size_t s = nodes[i].succ[k];
            if (!nodes[s].done && nodes[s].indeg > 0 && --nodes[s].indeg == 0)
                queue[tail++] = s;
        }
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "tsort: write error: %s\n", strerror(errno));
        return 1;
    }
    if (opt_w && cycles)
        return cycles > 125 ? 125 : (int)cycles;
    return 0;
}
