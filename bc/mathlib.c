/*
 * mathlib.c - the functions of bc -l: s (sine), c (cosine), a
 * (arctangent), l (natural logarithm), e (exponential) and j (Bessel
 * function of the first kind), computed with guard digits and
 * truncated to the scale in effect when they are called.
 */
#include "bc.h"

#define GUARD 12

static Num cst(long v)
{
    return num_from_long(v);
}

/* a / b at scale w, b nonzero */
static Num dv(Num a, Num b, long w)
{
    Num q;
    if (num_div(a, b, w, &q))
        bc_runtime_error("divide by zero");
    return q;
}

static Num ml(Num a, Num b, long w)
{
    return num_mul(a, b, w);
}

/* replaces *a by f(*a, b) */
#define UPDATE(a, expr) \
    do { \
        Num t_ = (expr); \
        num_free(&(a)); \
        (a) = t_; \
    } while (0)

static long int_digits(Num x)
{
    Num t = num_trunc(x);
    long d = 0;
    t.neg = 0;
    while (!num_is_zero(t)) {
        Num ten = cst(10), q;
        num_div(t, ten, 0, &q);
        num_free(&t);
        num_free(&ten);
        t = q;
        d++;
        if (d > 18)
            break;
    }
    num_free(&t);
    return d;
}

/* |x| as a long, saturating */
static long abs_long(Num x)
{
    long v;
    if (!num_to_long(x, &v))
        return LONG_MAX / 4;
    return v < 0 ? -v : v;
}

/* e^x for x >= 0, at scale w */
static Num exp_pos(Num x, long w)
{
    Num sum, term, half = num_zero(), xr = num_copy(x);
    long n = 0;
    /* halve x to below 1/2: e^x = (e^(x/2^n))^(2^n) */
    {
        Num lim = num_parse("5", 10);
        lim.scale = 1;      /* 0.5 */
        while (num_cmp(xr, lim) > 0) {
            Num two = cst(2);
            UPDATE(xr, dv(xr, two, w));
            num_free(&two);
            n++;
        }
        num_free(&lim);
    }
    (void)half;
    sum = cst(1);
    term = cst(1);
    for (long k = 1;; k++) {
        Num kk = cst(k);
        UPDATE(term, ml(term, xr, w));
        UPDATE(term, dv(term, kk, w));
        num_free(&kk);
        if (num_is_zero(term))
            break;
        UPDATE(sum, num_add(sum, term));
    }
    for (long i = 0; i < n; i++)
        UPDATE(sum, ml(sum, sum, w));
    num_free(&term);
    num_free(&xr);
    return sum;
}

static Num math_e(Num x, long s)
{
    long big = abs_long(x);
    long w;
    int neg = x.neg;
    Num ax = num_copy(x), r;
    ax.neg = 0;
    if (big > 100000000L)
        bc_runtime_error("e: argument too large");
    /* the integer digits of e^|x| and the error growth of the squarings */
    w = s + GUARD + (long)(0.4343 * (double)big) + 2 * int_digits(ax) + 4;
    r = exp_pos(ax, w);
    if (neg) {
        Num one = cst(1);
        UPDATE(r, dv(one, r, w));
        num_free(&one);
    }
    num_free(&ax);
    UPDATE(r, num_rescale(r, s));
    return r;
}

/* ln(x) for x > 0, at scale w */
static Num log_pos(Num x, long w)
{
    Num xr = num_copy(x), u, u2, sum, pw, f;
    Num two = cst(2), lo, hi;
    long k = 0;
    hi = cst(2);
    lo = num_parse("5", 10);
    lo.scale = 1;
    /* square roots bring x near 1: ln x = 2^k ln(x^(1/2^k)) */
    while (num_cmp(xr, hi) >= 0 || num_cmp(xr, lo) <= 0) {
        Num t;
        num_sqrt(xr, w, &t);
        num_free(&xr);
        xr = t;
        k++;
    }
    num_free(&hi);
    num_free(&lo);
    /* ln x = 2 atanh(u), u = (x - 1)/(x + 1) */
    {
        Num one = cst(1), a = num_sub(xr, one), b = num_add(xr, one);
        u = dv(a, b, w);
        num_free(&one);
        num_free(&a);
        num_free(&b);
    }
    u2 = ml(u, u, w);
    sum = num_copy(u);
    pw = num_copy(u);
    for (long i = 3;; i += 2) {
        Num ii = cst(i), t;
        UPDATE(pw, ml(pw, u2, w));
        t = dv(pw, ii, w);
        num_free(&ii);
        if (num_is_zero(t)) {
            num_free(&t);
            break;
        }
        UPDATE(sum, num_add(sum, t));
        num_free(&t);
    }
    f = cst(2);
    for (long i = 0; i < k; i++)
        UPDATE(f, num_add(f, f));
    UPDATE(sum, num_mul_exact(sum, f));
    num_free(&f);
    num_free(&two);
    num_free(&u);
    num_free(&u2);
    num_free(&pw);
    num_free(&xr);
    return sum;
}

static Num math_l(Num x, long s)
{
    Num r;
    long w;
    if (x.neg || num_is_zero(x))
        bc_runtime_error("l: the argument must be positive");
    w = s + GUARD + 2 * int_digits(x) + 4;
    /* small arguments need more digits for their square roots */
    if (x.scale > s)
        w += x.scale - s;
    r = log_pos(x, w + 10);
    UPDATE(r, num_rescale(r, s));
    return r;
}

/* arctan(x) for x >= 0, at scale w */
static Num atan_pos(Num x, long w)
{
    Num xr = num_copy(x), lim = num_parse("2", 10), sum, pw, x2;
    long k = 0;
    lim.scale = 1;          /* 0.2 */
    /* arctan x = 2 arctan(x / (1 + sqrt(1 + x^2))) */
    while (num_cmp(xr, lim) > 0) {
        Num one = cst(1), sq = ml(xr, xr, w), t, rt, den;
        t = num_add(one, sq);
        num_sqrt(t, w, &rt);
        den = num_add(one, rt);
        UPDATE(xr, dv(xr, den, w));
        num_free(&one);
        num_free(&sq);
        num_free(&t);
        num_free(&rt);
        num_free(&den);
        k++;
    }
    num_free(&lim);
    x2 = ml(xr, xr, w);
    sum = num_copy(xr);
    pw = num_copy(xr);
    for (long i = 3;; i += 2) {
        Num ii = cst(i), t;
        UPDATE(pw, ml(pw, x2, w));
        t = dv(pw, ii, w);
        num_free(&ii);
        if (num_is_zero(t)) {
            num_free(&t);
            break;
        }
        if ((i / 2) % 2)
            UPDATE(sum, num_sub(sum, t));
        else
            UPDATE(sum, num_add(sum, t));
        num_free(&t);
    }
    {
        Num f = cst(1);
        for (long i = 0; i < k; i++)
            UPDATE(f, num_add(f, f));
        UPDATE(sum, num_mul_exact(sum, f));
        num_free(&f);
    }
    num_free(&x2);
    num_free(&pw);
    num_free(&xr);
    return sum;
}

static Num math_a(Num x, long s)
{
    long w = s + GUARD + 2 * int_digits(x) + 4;
    Num ax = num_copy(x), r;
    ax.neg = 0;
    r = atan_pos(ax, w);
    if (x.neg && !num_is_zero(r))
        r.neg = 1;
    num_free(&ax);
    UPDATE(r, num_rescale(r, s));
    return r;
}

/* pi at scale w */
static Num pi_at(long w)
{
    static Num cache;
    static long cache_w = -1;
    if (cache_w < w) {
        /* pi = 16 atan(1/5) - 4 atan(1/239) */
        Num one = cst(1), five = cst(5), n239 = cst(239), a, b, t1, t2;
        long ww = w + 5;
        a = dv(one, five, ww);
        b = dv(one, n239, ww);
        t1 = atan_pos(a, ww);
        t2 = atan_pos(b, ww);
        {
            Num s16 = cst(16), s4 = cst(4), p1 = num_mul_exact(t1, s16), p2 = num_mul_exact(t2, s4);
            num_free(&cache);
            cache = num_sub(p1, p2);
            num_free(&s16);
            num_free(&s4);
            num_free(&p1);
            num_free(&p2);
        }
        cache_w = w;
        num_free(&one);
        num_free(&five);
        num_free(&n239);
        num_free(&a);
        num_free(&b);
        num_free(&t1);
        num_free(&t2);
    }
    return num_rescale(cache, w);
}

/* sin(x) at scale s; cos when cosine is set */
static Num sin_cos(Num x, long s, int cosine)
{
    long w = s + GUARD + 2 * int_digits(x) + 6;
    Num pi = pi_at(w + int_digits(x) + 4), twopi, xr, q, sum, term, x2;
    twopi = num_add(pi, pi);
    xr = num_copy(x);
    /* reduce to |x| < 2 pi */
    q = dv(xr, twopi, 0);
    if (!num_is_zero(q)) {
        Num m = num_mul_exact(q, twopi);
        UPDATE(xr, num_sub(xr, m));
        num_free(&m);
    }
    UPDATE(xr, num_rescale(xr, w));
    num_free(&q);
    x2 = ml(xr, xr, w);
    /* sin x = x - x^3/3! + ...; cos x = 1 - x^2/2! + ... */
    sum = cosine ? num_rescale(cst(1), w) : num_copy(xr);
    term = num_copy(sum);
    for (long k = 1;; k++) {
        Num d = cosine ? cst((2 * k - 1) * (2 * k)) : cst((2 * k) * (2 * k + 1));
        UPDATE(term, ml(term, x2, w));
        UPDATE(term, dv(term, d, w));
        num_free(&d);
        if (num_is_zero(term))
            break;
        if (k % 2)
            UPDATE(sum, num_sub(sum, term));
        else
            UPDATE(sum, num_add(sum, term));
    }
    num_free(&term);
    num_free(&x2);
    num_free(&xr);
    num_free(&pi);
    num_free(&twopi);
    UPDATE(sum, num_rescale(sum, s));
    return sum;
}

/* the Bessel function J_n(x) */
static Num math_j(Num nn, Num x, long s)
{
    long n, big = abs_long(x), w;
    int negate = 0;
    Num half, h2, term, sum, two = cst(2);
    if (!num_to_long(nn, &n) || n > 100000000L || n < -100000000L)
        bc_runtime_error("j: order too large");
    if (n < 0) {
        n = -n;
        negate = n % 2;
    }
    if (big > 10000000L)
        bc_runtime_error("j: argument too large");
    /* the terms grow to about e^|x| before they shrink */
    w = s + GUARD + (long)(0.4343 * (double)big) + 2 * int_digits(x) + 4;
    half = dv(x, two, w);
    h2 = ml(half, half, w);
    /* (x/2)^n / n! */
    term = cst(1);
    for (long i = 1; i <= n; i++) {
        Num ii = cst(i);
        UPDATE(term, ml(term, half, w));
        UPDATE(term, dv(term, ii, w));
        num_free(&ii);
    }
    sum = num_copy(term);
    for (long k = 1;; k++) {
        Num d = cst(k);
        Num e = cst(k + n);
        UPDATE(term, ml(term, h2, w));
        UPDATE(term, dv(term, d, w));
        UPDATE(term, dv(term, e, w));
        num_free(&d);
        num_free(&e);
        if (num_is_zero(term))
            break;
        if (k % 2)
            UPDATE(sum, num_sub(sum, term));
        else
            UPDATE(sum, num_add(sum, term));
    }
    if (negate && !num_is_zero(sum))
        sum.neg = !sum.neg;
    num_free(&half);
    num_free(&h2);
    num_free(&term);
    num_free(&two);
    UPDATE(sum, num_rescale(sum, s));
    return sum;
}

Num math_call(int which, Num *args, int nargs, long scale)
{
    (void)nargs;
    switch (which) {
    case M_S: return sin_cos(args[0], scale, 0);
    case M_C: return sin_cos(args[0], scale, 1);
    case M_A: return math_a(args[0], scale);
    case M_L: return math_l(args[0], scale);
    case M_E: return math_e(args[0], scale);
    case M_J: return math_j(args[0], args[1], scale);
    }
    return num_zero();
}

void math_define(void)
{
    static const struct {
        const char *name;
        int which, nargs;
    } fns[] = { { "s", M_S, 1 }, { "c", M_C, 1 }, { "a", M_A, 1 }, { "l", M_L, 1 }, { "e", M_E, 1 },
                { "j", M_J, 2 } };
    for (size_t i = 0; i < sizeof fns / sizeof fns[0]; i++) {
        Func *f = calloc(1, sizeof *f);
        if (!f)
            bc_nomem();
        f->builtin = fns[i].which;
        f->nparams = fns[i].nargs;
        lookup(fns[i].name)->func = f;
    }
}
