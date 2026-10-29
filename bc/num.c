/*
 * num.c - arbitrary-precision decimal arithmetic for bc: exact integer
 * mantissas in base 10^9 with a decimal scale, truncating to the
 * scales the standard prescribes.
 */
#include "bc.h"

static void *nalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        bc_nomem();
    return p;
}

static const uint32_t pow10_small[10] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000,
                                          1000000000 };

void num_init(void)
{
}

Num num_zero(void)
{
    Num z = { 0, 0, 0, NULL };
    return z;
}

void num_free(Num *a)
{
    free(a->d);
    a->d = NULL;
    a->n = 0;
    a->neg = 0;
}

static Num mk(size_t n)
{
    Num r = { 0, 0, n, NULL };
    r.d = nalloc(n * sizeof(uint32_t));
    memset(r.d, 0, n * sizeof(uint32_t));
    return r;
}

static void trim(Num *a)
{
    while (a->n && a->d[a->n - 1] == 0)
        a->n--;
    if (!a->n)
        a->neg = 0;
}

Num num_copy(Num a)
{
    Num r = a;
    if (a.n) {
        r.d = nalloc(a.n * sizeof(uint32_t));
        memcpy(r.d, a.d, a.n * sizeof(uint32_t));
    } else
        r.d = NULL;
    return r;
}

Num num_from_long(long v)
{
    Num r = mk(3);
    unsigned long u = v < 0 ? 0UL - (unsigned long)v : (unsigned long)v;
    r.neg = v < 0;
    for (int i = 0; i < 3; i++) {
        r.d[i] = (uint32_t)(u % NUM_BASE);
        u /= NUM_BASE;
    }
    trim(&r);
    return r;
}

int num_is_zero(Num a)
{
    return a.n == 0;
}

/* ---------- magnitudes ---------- */

static int mag_cmp(const uint32_t *a, size_t an, const uint32_t *b, size_t bn)
{
    if (an != bn)
        return an < bn ? -1 : 1;
    for (size_t i = an; i-- > 0;)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

static Num mag_add(Num a, Num b)
{
    size_t n = (a.n > b.n ? a.n : b.n) + 1;
    Num r = mk(n);
    uint32_t carry = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t s = (uint64_t)carry + (i < a.n ? a.d[i] : 0) + (i < b.n ? b.d[i] : 0);
        r.d[i] = (uint32_t)(s % NUM_BASE);
        carry = (uint32_t)(s / NUM_BASE);
    }
    trim(&r);
    return r;
}

/* a - b, with |a| >= |b| */
static Num mag_sub(Num a, Num b)
{
    Num r = mk(a.n);
    int64_t borrow = 0;
    for (size_t i = 0; i < a.n; i++) {
        int64_t s = (int64_t)a.d[i] - borrow - (i < b.n ? b.d[i] : 0);
        borrow = 0;
        if (s < 0) {
            s += NUM_BASE;
            borrow = 1;
        }
        r.d[i] = (uint32_t)s;
    }
    trim(&r);
    return r;
}

static Num mag_mul(Num a, Num b)
{
    Num r;
    size_t ta = 0, tb = 0;
    if (!a.n || !b.n)
        return num_zero();
    /* low zero limbs (numbers at a large scale with few digits) are skipped */
    while (ta < a.n && !a.d[ta])
        ta++;
    while (tb < b.n && !b.d[tb])
        tb++;
    r = mk(a.n + b.n);
    for (size_t i = ta; i < a.n; i++) {
        uint64_t carry = 0, ai = a.d[i];
        if (!ai)
            continue;
        for (size_t j = tb; j < b.n; j++) {
            uint64_t t = ai * b.d[j] + r.d[i + j] + carry;
            r.d[i + j] = (uint32_t)(t % NUM_BASE);
            carry = t / NUM_BASE;
        }
        for (size_t k = i + b.n; carry; k++) {
            uint64_t t = r.d[k] + carry;
            r.d[k] = (uint32_t)(t % NUM_BASE);
            carry = t / NUM_BASE;
        }
    }
    trim(&r);
    return r;
}

/* a * m + add, in place growth */
static Num mag_mul_small(Num a, uint32_t m, uint32_t add)
{
    Num r = mk(a.n + 1);
    uint64_t carry = add;
    for (size_t i = 0; i < a.n; i++) {
        uint64_t t = (uint64_t)a.d[i] * m + carry;
        r.d[i] = (uint32_t)(t % NUM_BASE);
        carry = t / NUM_BASE;
    }
    r.d[a.n] = (uint32_t)carry;
    trim(&r);
    return r;
}

static Num mag_div_small(Num a, uint32_t m, uint32_t *rem)
{
    Num r = mk(a.n);
    uint64_t rr = 0;
    for (size_t i = a.n; i-- > 0;) {
        uint64_t cur = rr * NUM_BASE + a.d[i];
        r.d[i] = (uint32_t)(cur / m);
        rr = cur % m;
    }
    if (rem)
        *rem = (uint32_t)rr;
    trim(&r);
    return r;
}

/* Knuth's algorithm D; q and r may be NULL */
static void mag_divmod(Num a, Num b, Num *q, Num *r)
{
    size_t n = b.n, m;
    uint32_t f;
    Num u, v, qq;
    if (mag_cmp(a.d, a.n, b.d, b.n) < 0) {
        if (q)
            *q = num_zero();
        if (r) {
            *r = num_copy(a);
            r->neg = 0;
            r->scale = 0;
        }
        return;
    }
    if (n == 1) {
        uint32_t rem;
        Num t = mag_div_small(a, b.d[0], &rem);
        if (q)
            *q = t;
        else
            num_free(&t);
        if (r)
            *r = num_from_long((long)rem);
        return;
    }
    f = NUM_BASE / (b.d[n - 1] + 1);
    u = mag_mul_small(a, f, 0);
    v = mag_mul_small(b, f, 0);
    /* u needs a.n + 1 limbs */
    if (u.n == a.n) {
        uint32_t *nd = nalloc((u.n + 1) * sizeof(uint32_t));
        memcpy(nd, u.d, u.n * sizeof(uint32_t));
        nd[u.n] = 0;
        free(u.d);
        u.d = nd;
        u.n++;
    }
    m = u.n - n;
    qq = mk(m);
    for (size_t j = m; j-- > 0;) {
        uint64_t num = (uint64_t)u.d[j + n] * NUM_BASE + u.d[j + n - 1];
        uint64_t qhat = num / v.d[n - 1], rhat = num % v.d[n - 1];
        int64_t borrow, t;
        while (qhat >= NUM_BASE || qhat * v.d[n - 2] > rhat * NUM_BASE + u.d[j + n - 2]) {
            qhat--;
            rhat += v.d[n - 1];
            if (rhat >= NUM_BASE)
                break;
        }
        /* u[j..j+n] -= qhat * v */
        borrow = 0;
        {
            uint64_t carry = 0;
            for (size_t i = 0; i < n; i++) {
                uint64_t p = qhat * v.d[i] + carry;
                carry = p / NUM_BASE;
                t = (int64_t)u.d[i + j] - (int64_t)(p % NUM_BASE) - borrow;
                borrow = 0;
                if (t < 0) {
                    t += NUM_BASE;
                    borrow = 1;
                }
                u.d[i + j] = (uint32_t)t;
            }
            t = (int64_t)u.d[j + n] - (int64_t)carry - borrow;
            borrow = 0;
            if (t < 0) {
                t += NUM_BASE;
                borrow = 1;
            }
            u.d[j + n] = (uint32_t)t;
        }
        if (borrow) {
            /* add back */
            uint64_t c = 0;
            qhat--;
            for (size_t i = 0; i < n; i++) {
                uint64_t s = (uint64_t)u.d[i + j] + v.d[i] + c;
                u.d[i + j] = (uint32_t)(s % NUM_BASE);
                c = s / NUM_BASE;
            }
            u.d[j + n] = (uint32_t)(((uint64_t)u.d[j + n] + c) % NUM_BASE);
        }
        qq.d[j] = (uint32_t)qhat;
    }
    trim(&qq);
    if (q)
        *q = qq;
    else
        num_free(&qq);
    if (r) {
        u.n = n;
        trim(&u);
        *r = mag_div_small(u, f, NULL);
    }
    num_free(&u);
    num_free(&v);
}

/* a * 10^k */
static Num mag_shift_up(Num a, long k)
{
    Num t, r;
    size_t limbs = (size_t)(k / 9);
    if (!a.n || k <= 0)
        return num_copy(a);
    t = mag_mul_small(a, pow10_small[k % 9], 0);
    r = mk(t.n + limbs);
    memcpy(r.d + limbs, t.d, t.n * sizeof(uint32_t));
    num_free(&t);
    trim(&r);
    return r;
}

/* floor(a / 10^k) */
static Num mag_shift_down(Num a, long k)
{
    size_t limbs = (size_t)(k / 9);
    Num t, r;
    if (k <= 0)
        return num_copy(a);
    if (limbs >= a.n)
        return num_zero();
    t.neg = 0;
    t.scale = 0;
    t.n = a.n - limbs;
    t.d = a.d + limbs;
    if (k % 9)
        r = mag_div_small(t, pow10_small[k % 9], NULL);
    else
        r = num_copy(t);
    trim(&r);
    return r;
}

/* ---------- numbers ---------- */

Num num_rescale(Num a, long scale)
{
    Num r;
    if (scale >= a.scale)
        r = mag_shift_up(a, scale - a.scale);
    else
        r = mag_shift_down(a, a.scale - scale);
    r.neg = a.neg;
    r.scale = scale;
    trim(&r);
    return r;
}

int num_cmp_abs(Num a, Num b)
{
    int c;
    if (a.scale == b.scale)
        return mag_cmp(a.d, a.n, b.d, b.n);
    if (a.scale < b.scale) {
        Num t = mag_shift_up(a, b.scale - a.scale);
        c = mag_cmp(t.d, t.n, b.d, b.n);
        num_free(&t);
    } else {
        Num t = mag_shift_up(b, a.scale - b.scale);
        c = mag_cmp(a.d, a.n, t.d, t.n);
        num_free(&t);
    }
    return c;
}

int num_cmp(Num a, Num b)
{
    if (a.neg != b.neg)
        return a.neg ? -1 : 1;
    return a.neg ? -num_cmp_abs(a, b) : num_cmp_abs(a, b);
}

Num num_neg(Num a)
{
    Num r = num_copy(a);
    if (r.n)
        r.neg = !r.neg;
    return r;
}

/* a and b at the same scale (the larger), as magnitudes */
static void align(Num a, Num b, Num *x, Num *y, long *scale)
{
    long s = a.scale > b.scale ? a.scale : b.scale;
    *x = mag_shift_up(a, s - a.scale);
    *y = mag_shift_up(b, s - b.scale);
    *scale = s;
}

static Num addsub(Num a, Num b, int negate_b)
{
    Num x, y, r;
    long s;
    int bneg = b.n ? (b.neg ^ negate_b) : 0;
    align(a, b, &x, &y, &s);
    if (a.neg == bneg) {
        r = mag_add(x, y);
        r.neg = a.neg;
    } else {
        int c = mag_cmp(x.d, x.n, y.d, y.n);
        if (c >= 0) {
            r = mag_sub(x, y);
            r.neg = a.neg;
        } else {
            r = mag_sub(y, x);
            r.neg = bneg;
        }
    }
    r.scale = s;
    trim(&r);
    num_free(&x);
    num_free(&y);
    return r;
}

Num num_add(Num a, Num b)
{
    return addsub(a, b, 0);
}

Num num_sub(Num a, Num b)
{
    return addsub(a, b, 1);
}

Num num_mul_exact(Num a, Num b)
{
    Num r = mag_mul(a, b);
    r.neg = a.neg != b.neg;
    r.scale = a.scale + b.scale;
    trim(&r);
    return r;
}

static long maxl(long a, long b)
{
    return a > b ? a : b;
}

static long minl(long a, long b)
{
    return a < b ? a : b;
}

Num num_mul(Num a, Num b, long scale)
{
    Num p = num_mul_exact(a, b), r;
    long rs = minl(a.scale + b.scale, maxl(scale, maxl(a.scale, b.scale)));
    r = num_rescale(p, rs);
    num_free(&p);
    return r;
}

int num_div(Num a, Num b, long scale, Num *q)
{
    /* q = trunc(a / b * 10^scale) / 10^scale = trunc(Ma * 10^(scale + sb - sa) / Mb) */
    long k = scale + b.scale - a.scale;
    Num x, qq;
    if (!b.n)
        return -1;
    if (k >= 0)
        x = mag_shift_up(a, k);
    else
        x = mag_shift_down(a, -k);
    mag_divmod(x, b, &qq, NULL);
    num_free(&x);
    qq.neg = a.neg != b.neg;
    qq.scale = scale;
    trim(&qq);
    *q = qq;
    return 0;
}

int num_mod(Num a, Num b, long scale, Num *r)
{
    Num q, p, d;
    long rs = maxl(scale + b.scale, a.scale);
    if (num_div(a, b, scale, &q))
        return -1;
    p = num_mul_exact(q, b);
    d = num_sub(a, p);
    *r = num_rescale(d, rs);
    num_free(&q);
    num_free(&p);
    num_free(&d);
    return 0;
}

int num_is_integer(Num a)
{
    Num t;
    int r;
    if (!a.scale || !a.n)
        return 1;
    t = num_rescale(a, 0);
    t.scale = 0;
    {
        Num back = num_rescale(t, a.scale);
        r = num_cmp(back, a) == 0;
        num_free(&back);
    }
    num_free(&t);
    return r;
}

Num num_trunc(Num a)
{
    return num_rescale(a, 0);
}

int num_to_long(Num a, long *v)
{
    Num t = num_rescale(a, 0);
    unsigned long u = 0;
    if (t.n > 3) {
        num_free(&t);
        return 0;
    }
    for (size_t i = t.n; i-- > 0;) {
        if (u > (unsigned long)LONG_MAX / NUM_BASE) {
            num_free(&t);
            return 0;
        }
        u = u * NUM_BASE + t.d[i];
    }
    if (u > (unsigned long)LONG_MAX) {
        num_free(&t);
        return 0;
    }
    *v = t.neg ? -(long)u : (long)u;
    num_free(&t);
    return 1;
}

int num_pow(Num a, Num b, long scale, Num *r)
{
    long e, rs;
    int negexp;
    Num res, base;
    if (!num_to_long(b, &e) || e > 1000000000L || e < -1000000000L)
        return -2;
    negexp = e < 0;
    if (negexp)
        e = -e;
    /* the exact power */
    res = num_from_long(1);
    base = num_copy(a);
    base.neg = 0;
    {
        unsigned long bits = (unsigned long)e;
        while (bits) {
            if (bits & 1) {
                Num t = num_mul_exact(res, base);
                num_free(&res);
                res = t;
            }
            bits >>= 1;
            if (bits) {
                Num t = num_mul_exact(base, base);
                num_free(&base);
                base = t;
            }
        }
    }
    num_free(&base);
    if (a.neg && (e & 1) && res.n)
        res.neg = 1;
    if (negexp) {
        Num one = num_from_long(1), q;
        if (!res.n) {
            num_free(&one);
            num_free(&res);
            return -1;
        }
        num_div(one, res, scale, &q);
        num_free(&one);
        num_free(&res);
        *r = q;
        return 0;
    }
    rs = minl(a.scale * e, maxl(scale, a.scale));
    *r = num_rescale(res, rs);
    num_free(&res);
    return 0;
}

/* the integer square root of a magnitude (Newton) */
static Num mag_isqrt(Num a)
{
    Num x, y;
    size_t digits;
    if (!a.n)
        return num_zero();
    /* a first guess above the root: 10^ceil(digits/2) */
    digits = (a.n - 1) * 9;
    {
        uint32_t t = a.d[a.n - 1];
        while (t) {
            digits++;
            t /= 10;
        }
    }
    x = mag_shift_up(num_from_long(1), (long)((digits + 1) / 2));
    for (;;) {
        Num q, s;
        mag_divmod(a, x, &q, NULL);
        s = mag_add(x, q);
        y = mag_div_small(s, 2, NULL);
        num_free(&q);
        num_free(&s);
        if (mag_cmp(y.d, y.n, x.d, x.n) >= 0) {
            num_free(&y);
            break;
        }
        num_free(&x);
        x = y;
    }
    return x;
}

int gnu_sqrt_quirk;

int num_sqrt(Num a, long scale, Num *r)
{
    long rs = maxl(scale, a.scale);
    Num x, s;
    if (a.neg && a.n)
        return -1;
    if (gnu_sqrt_quirk) {
        /* (testing only) GNU bc gives 0 and 1 the scale 0 */
        Num one = num_from_long(1);
        int c = num_cmp(a, one);
        num_free(&one);
        if (!a.n || c == 0) {
            *r = a.n ? num_from_long(1) : num_zero();
            return 0;
        }
    }
    /* sqrt(Ma / 10^sa) * 10^rs = sqrt(Ma * 10^(2 rs - sa)) */
    x = mag_shift_up(a, 2 * rs - a.scale);
    s = mag_isqrt(x);
    num_free(&x);
    s.scale = rs;
    s.neg = 0;
    trim(&s);
    *r = s;
    return 0;
}

static long mag_digits(Num a)
{
    long d;
    uint32_t t;
    if (!a.n)
        return 0;
    d = (long)(a.n - 1) * 9;
    for (t = a.d[a.n - 1]; t; t /= 10)
        d++;
    return d;
}

long num_length(Num a)
{
    long digits = mag_digits(a);
    /* integer digits + scale; for |a| < 1 the scale (at least 1), as in historical bc */
    if (digits <= a.scale)
        return a.scale ? a.scale : 1;
    return digits;
}

Num num_parse(const char *s, int base)
{
    Num ip = num_zero(), fp = num_zero(), den;
    const char *p = s, *dot = strchr(s, '.');
    long fdigits = 0;
    /* a single digit before the point has its face value, as for ibase = A;
       other digits beyond the base count as base - 1, as in historical bc */
    int single = s[0] && s[0] != '.' && (!s[1] || s[1] == '.');
    for (; *p && *p != '.'; p++) {
        int d = isdigit((unsigned char)*p) ? *p - '0' : *p - 'A' + 10;
        Num t;
        if (!single && d >= base)
            d = base - 1;
        t = mag_mul_small(ip, (uint32_t)base, (uint32_t)d);
        num_free(&ip);
        ip = t;
    }
    if (!dot)
        return ip;
    for (p = dot + 1; *p; p++) {
        int d = isdigit((unsigned char)*p) ? *p - '0' : *p - 'A' + 10;
        Num t;
        if (d >= base)
            d = base - 1;
        t = mag_mul_small(fp, (uint32_t)base, (uint32_t)d);
        num_free(&fp);
        fp = t;
        fdigits++;
    }
    if (base == 10) {
        Num a = mag_shift_up(ip, fdigits), r;
        r = mag_add(a, fp);
        num_free(&a);
        num_free(&ip);
        num_free(&fp);
        r.scale = fdigits;
        return r;
    }
    /* fraction = fp / base^fdigits, truncated to fdigits decimal places */
    den = num_from_long(1);
    for (long i = 0; i < fdigits; i++) {
        Num t = mag_mul_small(den, (uint32_t)base, 0);
        num_free(&den);
        den = t;
    }
    {
        Num q, total;
        num_div(fp, den, fdigits, &q);
        total = num_add(ip, q);
        num_free(&q);
        num_free(&ip);
        num_free(&fp);
        num_free(&den);
        if (total.scale < fdigits) {
            Num t = num_rescale(total, fdigits);
            num_free(&total);
            total = t;
        }
        return total;
    }
}

/* ---------- output ---------- */

static void put_digit(long d, long obase, Putc out, void *arg, int first_frac)
{
    static const char hex[] = "0123456789ABCDEF";
    if (obase <= 16) {
        out(hex[d], arg);
        return;
    }
    {
        /* a multi-digit decimal number as wide as obase - 1 */
        char buf[32];
        int w = 0;
        long m = obase - 1;
        while (m) {
            w++;
            m /= 10;
        }
        if (!first_frac)
            out(' ', arg);
        snprintf(buf, sizeof buf, "%0*ld", w, d);
        for (char *q = buf; *q; q++)
            out(*q, arg);
    }
}

void num_print(Num a, long obase, Putc out, void *arg)
{
    Num ip, fp;
    if (!a.n) {
        out('0', arg);
        return;
    }
    if (a.neg)
        out('-', arg);
    ip = num_rescale(a, 0);
    ip.neg = 0;
    if (obase == 10) {
        /* the digits of the mantissa directly */
        long nd = mag_digits(a), sc = a.scale;
        char *buf = nalloc(a.n * 9 + (size_t)(nd > sc ? nd : sc) + 2), *q;
        long len = 0;
        Num m = num_copy(a);
        m.neg = 0;
        /* all the digits, least significant first */
        while (m.n) {
            uint32_t rem;
            Num t = mag_div_small(m, 1000000000u, &rem);
            for (int i = 0; i < 9; i++) {
                buf[len++] = (char)('0' + rem % 10);
                rem /= 10;
            }
            num_free(&m);
            m = t;
        }
        num_free(&m);
        while (len > sc && len > 0 && buf[len - 1] == '0')
            len--;
        while (len < sc)
            buf[len++] = '0';
        /* integer part */
        for (q = buf + len; q > buf + sc;)
            out(*--q, arg);
        if (sc > 0) {
            out('.', arg);
            for (q = buf + sc; q > buf;)
                out(*--q, arg);
        }
        free(buf);
        num_free(&ip);
        return;
    }
    /* the integer part, by repeated division */
    if (ip.n) {
        size_t cap = 64, n = 0;
        long *dig = nalloc(cap * sizeof(long));
        Num bo = num_from_long(obase);
        while (ip.n) {
            Num q, r;
            long d;
            mag_divmod(ip, bo, &q, &r);
            num_to_long(r, &d);
            num_free(&r);
            num_free(&ip);
            ip = q;
            if (n == cap) {
                cap *= 2;
                dig = realloc(dig, cap * sizeof(long));
                if (!dig)
                    bc_nomem();
            }
            dig[n++] = d;
        }
        while (n)
            put_digit(dig[--n], obase, out, arg, 0);
        free(dig);
        num_free(&bo);
    }
    num_free(&ip);
    /* the fraction, by repeated multiplication, until obase^k >= 10^scale */
    if (a.scale > 0) {
        Num t = num_trunc(a), pw = num_from_long(1), limit, bo = num_from_long(obase);
        int first = 1;
        t.neg = 0;
        {
            Num aa = num_copy(a);
            aa.neg = 0;
            fp = num_sub(aa, t);
            num_free(&aa);
        }
        num_free(&t);
        limit = mag_shift_up(num_from_long(1), a.scale);
        out('.', arg);
        while (mag_cmp(pw.d, pw.n, limit.d, limit.n) < 0) {
            Num m = num_mul_exact(fp, bo), ipart, rest;
            long d;
            ipart = num_trunc(m);
            num_to_long(ipart, &d);
            rest = num_sub(m, ipart);
            num_free(&m);
            num_free(&ipart);
            num_free(&fp);
            fp = num_rescale(rest, a.scale);
            num_free(&rest);
            put_digit(d, obase, out, arg, first);
            first = 0;
            {
                Num np = mag_mul_small(pw, (uint32_t)(obase < NUM_BASE ? obase : 1), 0);
                if (obase >= NUM_BASE) {
                    Num big = num_from_long(obase);
                    num_free(&np);
                    np = mag_mul(pw, big);
                    num_free(&big);
                }
                num_free(&pw);
                pw = np;
            }
        }
        num_free(&fp);
        num_free(&pw);
        num_free(&limit);
        num_free(&bo);
    }
}
