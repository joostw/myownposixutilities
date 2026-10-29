/*
 * ldiff.c - the differences between two lists of lines, for delta
 *
 * Myers's O(ND) algorithm in linear space (the middle snake, divide and
 * conquer), on lines numbered by a hash table so that lines are compared
 * as integers.
 */

#include "sccs.h"

#include <stdlib.h>
#include <string.h>

static int *A, *B;              /* the lines as numbers */
static bool *ka, *nb;
static int *vf, *vb;            /* the forward and backward V, offset */
static int voff;

/* the middle snake of A[a0..a1) and B[b0..b1): its start in A and B */
static void middle(int a0, int a1, int b0, int b1, int *xs, int *ys, int *xe, int *ye)
{
    int n = a1 - a0, m = b1 - b0, delta = n - m;
    bool odd = delta & 1;
    int max = (n + m + 1) / 2;
    vf[voff + 1] = 0;
    vb[voff + 1] = 0;
    for (int d = 0; d <= max; d++) {
        for (int k = -d; k <= d; k += 2) {
            int x = (k == -d || (k != d && vf[voff + k - 1] < vf[voff + k + 1]))
                    ? vf[voff + k + 1] : vf[voff + k - 1] + 1;
            int y = x - k, x0 = x, y0 = y;
            while (x < n && y < m && A[a0 + x] == B[b0 + y])
                x++, y++;
            vf[voff + k] = x;
            if (odd && k >= delta - (d - 1) && k <= delta + (d - 1)
                && x + vb[voff + delta - k] >= n) {
                *xs = a0 + x0, *ys = b0 + y0, *xe = a0 + x, *ye = b0 + y;
                return;
            }
        }
        for (int k = -d; k <= d; k += 2) {
            int x = (k == -d || (k != d && vb[voff + k - 1] < vb[voff + k + 1]))
                    ? vb[voff + k + 1] : vb[voff + k - 1] + 1;
            int y = x - k, x0 = x, y0 = y;
            while (x < n && y < m && A[a1 - 1 - x] == B[b1 - 1 - y])
                x++, y++;
            vb[voff + k] = x;
            if (!odd && k >= delta - d && k <= delta + d && x + vf[voff + delta - k] >= n) {
                *xs = a1 - x, *ys = b1 - y, *xe = a1 - x0, *ye = b1 - y0;
                return;
            }
        }
    }
    /* not reached for valid input */
    *xs = *xe = a0, *ys = *ye = b0;
}

static void compare(int a0, int a1, int b0, int b1)
{
    /* common lines at both ends first */
    while (a0 < a1 && b0 < b1 && A[a0] == B[b0])
        ka[a0++] = true, nb[b0++] = false;
    while (a0 < a1 && b0 < b1 && A[a1 - 1] == B[b1 - 1])
        ka[--a1] = true, nb[--b1] = false;
    if (a0 == a1 || b0 == b1)
        return;             /* the rest of A is deleted, the rest of B new */
    int xs, ys, xe, ye;
    middle(a0, a1, b0, b1, &xs, &ys, &xe, &ye);
    for (int i = xs; i < xe; i++)
        ka[i] = true, nb[ys + (i - xs)] = false;
    if (xs == a0 && ys == b0 && xe == a1 && ye == b1)
        return;
    compare(a0, xs, b0, ys);
    compare(xe, a1, ye, b1);
}

struct entry {
    const char *s;
    int id;
    struct entry *next;
};

static unsigned hash(const char *s)
{
    unsigned h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h;
}

void ldiff(char **a, int na, char **b, int nb_, bool *keep_a, bool *new_b)
{
    size_t size = 1;
    while (size < (size_t)(na + nb_) * 2 + 16)
        size *= 2;
    struct entry **tab = xmalloc(size * sizeof *tab);
    memset(tab, 0, size * sizeof *tab);
    struct entry *pool = xmalloc((size_t)(na + nb_ + 1) * sizeof *pool);
    int used = 0, ids = 0;
    A = xmalloc((size_t)(na + 1) * sizeof *A);
    B = xmalloc((size_t)(nb_ + 1) * sizeof *B);
    for (int pass = 0; pass < 2; pass++) {
        char **v = pass ? b : a;
        int n = pass ? nb_ : na, *out = pass ? B : A;
        for (int i = 0; i < n; i++) {
            unsigned h = hash(v[i]) & (unsigned)(size - 1);
            struct entry *e;
            for (e = tab[h]; e && strcmp(e->s, v[i]) != 0; e = e->next)
                ;
            if (!e) {
                e = &pool[used++];
                e->s = v[i];
                e->id = ids++;
                e->next = tab[h];
                tab[h] = e;
            }
            out[i] = e->id;
        }
    }
    free(tab);
    free(pool);
    ka = keep_a;
    nb = new_b;
    for (int i = 0; i < na; i++)
        ka[i] = false;
    for (int j = 0; j < nb_; j++)
        nb[j] = true;           /* until matched */
    voff = na + nb_ + 2;
    vf = xmalloc((size_t)(2 * voff + 2) * sizeof *vf);
    vb = xmalloc((size_t)(2 * voff + 2) * sizeof *vb);
    compare(0, na, 0, nb_);
    /* runs of new or deleted lines slide down as far as they can (where
       the line after the run is the same as its first), as GNU diff
       places them */
    for (int j = 0; j < nb_; ) {
        if (!nb[j]) {
            j++;
            continue;
        }
        int k = j;
        while (k < nb_ && nb[k])
            k++;
        while (k < nb_ && !nb[k] && B[k] == B[j]) {
            nb[j] = false, nb[k] = true;
            j++, k++;
        }
        j = k;
    }
    for (int i = 0; i < na; ) {
        if (ka[i]) {
            i++;
            continue;
        }
        int k = i;
        while (k < na && !ka[k])
            k++;
        while (k < na && ka[k] && A[k] == A[i]) {
            ka[i] = true, ka[k] = false;
            i++, k++;
        }
        i = k;
    }
    free(vf);
    free(vb);
    free(A);
    free(B);
}
