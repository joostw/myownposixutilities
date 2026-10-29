/*
 * bltin2.c - echo, printf and test built-ins.
 */
#include "sh.h"
#include <inttypes.h>

/* ------------------------------------------------------------------ */
/* echo (XSI behaviour: no options, escape sequences)                 */
/* ------------------------------------------------------------------ */

int bi_echo(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        const char *p = argv[i];
        if (i > 1)
            outc(&out1, ' ');
        while (*p) {
            if (*p != '\\') {
                outc(&out1, *p++);
                continue;
            }
            p++;
            switch (*p) {
            case 'a': outc(&out1, '\a'); p++; break;
            case 'b': outc(&out1, '\b'); p++; break;
            case 'c': return 0;
            case 'f': outc(&out1, '\f'); p++; break;
            case 'n': outc(&out1, '\n'); p++; break;
            case 'r': outc(&out1, '\r'); p++; break;
            case 't': outc(&out1, '\t'); p++; break;
            case 'v': outc(&out1, '\v'); p++; break;
            case '\\': outc(&out1, '\\'); p++; break;
            case '0': {
                int v = 0, k;
                p++;
                for (k = 0; k < 3 && *p >= '0' && *p <= '7'; k++)
                    v = v * 8 + (*p++ - '0');
                outc(&out1, v);
                break;
            }
            case 0:
                outc(&out1, '\\');
                break;
            default:
                outc(&out1, '\\');
            }
        }
    }
    outc(&out1, '\n');
    return 0;
}

/* ------------------------------------------------------------------ */
/* printf                                                             */
/* ------------------------------------------------------------------ */

static int pf_err;
static int pf_stop;     /* \c in %b */

static void pf_bad(const char *arg, const char *what)
{
    sh_warn("%s: %s", arg, what);
    pf_err = 1;
}

static intmax_t pf_int(const char *s)
{
    char *end;
    intmax_t v;
    if (!s || !*s)
        return 0;
    if (s[0] == '\'' || s[0] == '"') {
        wchar_t wc;
        mbstate_t st;
        size_t k;
        if (!s[1])
            return 0;
        memset(&st, 0, sizeof st);
        k = mbrtowc(&wc, s + 1, strlen(s + 1), &st);
        if (k == (size_t)-1 || k == (size_t)-2 || k == 0)
            return (unsigned char)s[1];
        return (intmax_t)wc;
    }
    errno = 0;
    if (s[0] == '-' || s[0] == '+' || isdigit((unsigned char)s[0]) || isspace((unsigned char)s[0])) {
        const char *q = s;
        while (isspace((unsigned char)*q)) q++;
        if (*q == '-')
            v = strtoimax(s, &end, 0);
        else {
            uintmax_t u = strtoumax(s, &end, 0);
            v = (intmax_t)u;
        }
    } else {
        pf_bad(s, "expected numeric value");
        return 0;
    }
    if (end == s) {
        pf_bad(s, "expected numeric value");
        return 0;
    }
    if (*end) {
        pf_bad(s, "not completely converted");
    } else if (errno == ERANGE)
        pf_bad(s, "Result too large");
    return v;
}

static double pf_float(const char *s)
{
    char *end;
    double d;
    if (!s || !*s)
        return 0;
    if (s[0] == '\'' || s[0] == '"')
        return (double)(unsigned char)s[1];
    errno = 0;
    d = strtod(s, &end);
    if (end == s)
        pf_bad(s, "expected numeric value");
    else if (*end)
        pf_bad(s, "not completely converted");
    return d;
}

/* process a backslash escape in the format; returns chars consumed */
static size_t fmt_escape(const char *p, StrBuf *o)
{
    switch (*p) {
    case '\\': sb_addc(o, '\\'); return 1;
    case 'a': sb_addc(o, '\a'); return 1;
    case 'b': sb_addc(o, '\b'); return 1;
    case 'f': sb_addc(o, '\f'); return 1;
    case 'n': sb_addc(o, '\n'); return 1;
    case 'r': sb_addc(o, '\r'); return 1;
    case 't': sb_addc(o, '\t'); return 1;
    case 'v': sb_addc(o, '\v'); return 1;
    case '"': sb_addc(o, '"'); return 1;
    case '\'': sb_addc(o, '\''); return 1;
    case 0: sb_addc(o, '\\'); return 0;
    }
    if (*p >= '0' && *p <= '7') {
        int v = 0, k;
        for (k = 0; k < 3 && p[k] >= '0' && p[k] <= '7'; k++)
            v = v * 8 + (p[k] - '0');
        sb_addc(o, v);
        return (size_t)k;
    }
    sb_addc(o, '\\');
    sb_addc(o, *p);
    return 1;
}

/* %b argument expansion */
static void b_expand(const char *p, StrBuf *o)
{
    while (*p) {
        if (*p != '\\') {
            sb_addc(o, *p++);
            continue;
        }
        p++;
        if (*p == 'c') {
            pf_stop = 1;
            return;
        }
        if (*p == '0') {
            int v = 0, k;
            p++;
            for (k = 0; k < 3 && *p >= '0' && *p <= '7'; k++)
                v = v * 8 + (*p++ - '0');
            sb_addc(o, v);
            continue;
        }
        if (*p >= '1' && *p <= '7') {
            int v = 0, k;
            for (k = 0; k < 3 && *p >= '0' && *p <= '7'; k++)
                v = v * 8 + (*p++ - '0');
            sb_addc(o, v);
            continue;
        }
        if (*p == '\\' || strchr("abfnrtv", *p)) {
            p += fmt_escape(p, o);
            continue;
        }
        sb_addc(o, '\\');
        if (*p)
            sb_addc(o, *p++);
    }
}

int bi_printf(int argc, char **argv)
{
    const char *fmt;
    char **args;
    int nargs, ai = 0, maxused = 0, numbered = 0;
    StrBuf o;
    if (argc < 2) {
        sh_warn("usage: printf format [arg...]");
        return 2;
    }
    fmt = argv[1];
    if (strcmp(fmt, "--") == 0) {
        if (argc < 3) {
            sh_warn("usage: printf format [arg...]");
            return 2;
        }
        fmt = argv[2];
        args = argv + 3;
        nargs = argc - 3;
    } else {
        args = argv + 2;
        nargs = argc - 2;
    }
    pf_err = 0;
    pf_stop = 0;
    sb_init(&o);
    for (;;) {
        const char *p = fmt;
        int consumed = 0;
        int base = ai;
        while (*p && !pf_stop) {
            char spec[64];
            size_t sl = 0;
            const char *arg;
            int conv, width = 0, prec = -1, haveprec = 0, havewidth = 0;
            int argn = -1;
            if (*p == '\\') {
                p++;
                p += fmt_escape(p, &o);
                continue;
            }
            if (*p != '%') {
                sb_addc(&o, *p++);
                continue;
            }
            if (p[1] == '%') {
                sb_addc(&o, '%');
                p += 2;
                continue;
            }
            {
                /* %n$ */
                const char *q = p + 1;
                int n = 0;
                while (isdigit((unsigned char)*q))
                    n = n * 10 + (*q++ - '0');
                if (*q == '$' && n > 0) {
                    argn = n;
                    numbered = 1;
                    p = q;
                }
            }
            spec[sl++] = '%';
            p++;
            while (*p && strchr("-+ #0", *p) && sl < 40)
                spec[sl++] = *p++;
            if (*p == '*') {
                const char *wa;
                p++;
                if (argn > 0) {
                    /* *m$ */
                    int m = 0;
                    while (isdigit((unsigned char)*p)) m = m * 10 + (*p++ - '0');
                    if (*p == '$') p++;
                    wa = (base + m - 1) < nargs ? args[base + m - 1] : NULL;
                    if (base + m > maxused) maxused = base + m;
                } else {
                    wa = ai < nargs ? args[ai++] : NULL;
                    consumed = 1;
                }
                width = (int)pf_int(wa);
                havewidth = 1;
            } else {
                while (isdigit((unsigned char)*p)) {
                    width = width * 10 + (*p - '0');
                    havewidth = 1;
                    p++;
                }
            }
            if (*p == '.') {
                p++;
                haveprec = 1;
                prec = 0;
                if (*p == '*') {
                    const char *pa;
                    p++;
                    if (argn > 0) {
                        int m = 0;
                        while (isdigit((unsigned char)*p)) m = m * 10 + (*p++ - '0');
                        if (*p == '$') p++;
                        pa = (base + m - 1) < nargs ? args[base + m - 1] : NULL;
                        if (base + m > maxused) maxused = base + m;
                    } else {
                        pa = ai < nargs ? args[ai++] : NULL;
                        consumed = 1;
                    }
                    prec = (int)pf_int(pa);
                    if (prec < 0) haveprec = 0;
                } else
                    while (isdigit((unsigned char)*p))
                        prec = prec * 10 + (*p++ - '0');
            }
            while (*p && strchr("hlLqjzt", *p))
                p++;
            conv = (unsigned char)*p;
            if (!conv) {
                sh_warn("%s: missing format character", fmt);
                pf_err = 1;
                break;
            }
            p++;
            if (argn > 0) {
                int idx = base + argn - 1;
                arg = idx < nargs ? args[idx] : NULL;
                if (base + argn > maxused) maxused = base + argn;
                if (!arg) {
                    sh_warn("%%%d$: missing argument", argn);
                    pf_err = 1;
                }
            } else {
                arg = ai < nargs ? args[ai++] : NULL;
                consumed = 1;
                if (ai > maxused) maxused = ai;
            }
            if (havewidth)
                sl += (size_t)snprintf(spec + sl, sizeof spec - sl, "%d", width);
            if (haveprec)
                sl += (size_t)snprintf(spec + sl, sizeof spec - sl, ".%d", prec);
            spec[sl] = 0;
            switch (conv) {
            case 'd': case 'i': {
                intmax_t v = pf_int(arg);
                strcat(spec, "jd");
                sb_printf(&o, spec, v);
                break;
            }
            case 'o': case 'u': case 'x': case 'X': {
                intmax_t v = pf_int(arg);
                size_t k = strlen(spec);
                spec[k] = 'j';
                spec[k + 1] = (char)conv;
                spec[k + 2] = 0;
                sb_printf(&o, spec, (uintmax_t)v);
                break;
            }
            case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A': {
                double d = pf_float(arg);
                size_t k = strlen(spec);
                spec[k] = (char)conv;
                spec[k + 1] = 0;
                sb_printf(&o, spec, d);
                break;
            }
            case 'c': {
                char cs[2] = { arg && *arg ? arg[0] : 0, 0 };
                strcat(spec, "s");
                if (arg && *arg)
                    sb_printf(&o, spec, cs);
                else
                    sb_printf(&o, spec, "");
                break;
            }
            case 's':
                strcat(spec, "s");
                sb_printf(&o, spec, arg ? arg : "");
                break;
            case 'b': {
                StrBuf t;
                sb_init(&t);
                b_expand(arg ? arg : "", &t);
                strcat(spec, "s");
                {
                    /* precision/width apply to bytes; handle embedded NUL */
                    size_t len = t.len;
                    size_t outlen = haveprec && (size_t)prec < len ? (size_t)prec : len;
                    int pad = havewidth && (size_t)width > outlen ? width - (int)outlen : 0;
                    int left = strchr(spec, '-') != NULL;
                    if (!left) while (pad-- > 0) sb_addc(&o, ' ');
                    if (outlen) sb_add(&o, t.s, outlen);
                    if (left) while (pad-- > 0) sb_addc(&o, ' ');
                }
                sb_free(&t);
                break;
            }
            default:
                sh_warn("%%%c: invalid directive", conv);
                pf_err = 1;
                p = "";
                break;
            }
        }
        if (pf_stop)
            break;
        if (numbered) {
            if (maxused <= base || maxused >= nargs)
                break;
            ai = maxused;
            continue;
        }
        if (!consumed || ai >= nargs)
            break;
    }
    outn(&out1, o.s ? o.s : "", o.len);
    sb_free(&o);
    return pf_err;
}

/* ------------------------------------------------------------------ */
/* test                                                               */
/* ------------------------------------------------------------------ */

static int t_err;

static int t_int(const char *s, intmax_t *v)
{
    char *end;
    const char *q = s;
    while (isspace((unsigned char)*q)) q++;
    if (!*q) goto bad;
    errno = 0;
    *v = strtoimax(q, &end, 10);
    if (end == q) goto bad;
    while (isspace((unsigned char)*end)) end++;
    if (*end || errno) goto bad;
    return 1;
bad:
    sh_warn("%s: bad number", s);
    t_err = 1;
    return 0;
}

static int is_unary(const char *s)
{
    return s[0] == '-' && s[1] && !s[2] && strchr("bcdefghLnprSstuwxz", s[1]);
}

static int is_binary(const char *s)
{
    static const char *ops[] = { "=", "!=", "<", ">", "-eq", "-ne", "-gt", "-ge",
                                 "-lt", "-le", "-ef", "-nt", "-ot", NULL };
    int i;
    for (i = 0; ops[i]; i++)
        if (strcmp(s, ops[i]) == 0)
            return 1;
    return 0;
}

static int unary(const char *op, const char *a)
{
    struct stat st;
    switch (op[1]) {
    case 'n': return *a != 0;
    case 'z': return *a == 0;
    case 't': {
        intmax_t fd;
        if (!t_int(a, &fd)) return 0;
        return fd >= 0 && fd <= INT_MAX && isatty((int)fd);
    }
    case 'h': case 'L':
        return lstat(a, &st) == 0 && S_ISLNK(st.st_mode);
    }
    if (stat(a, &st) < 0)
        return 0;
    switch (op[1]) {
    case 'b': return S_ISBLK(st.st_mode);
    case 'c': return S_ISCHR(st.st_mode);
    case 'd': return S_ISDIR(st.st_mode);
    case 'e': return 1;
    case 'f': return S_ISREG(st.st_mode);
    case 'g': return (st.st_mode & S_ISGID) != 0;
    case 'p': return S_ISFIFO(st.st_mode);
    case 'r': return access(a, R_OK) == 0;
    case 'S': return S_ISSOCK(st.st_mode);
    case 's': return st.st_size > 0;
    case 'u': return (st.st_mode & S_ISUID) != 0;
    case 'w': return access(a, W_OK) == 0;
    case 'x': return access(a, X_OK) == 0;
    }
    return 0;
}

static int mtime_cmp(const struct stat *a, const struct stat *b)
{
    if (a->st_mtim.tv_sec != b->st_mtim.tv_sec)
        return a->st_mtim.tv_sec < b->st_mtim.tv_sec ? -1 : 1;
    if (a->st_mtim.tv_nsec != b->st_mtim.tv_nsec)
        return a->st_mtim.tv_nsec < b->st_mtim.tv_nsec ? -1 : 1;
    return 0;
}

static int binary(const char *a, const char *op, const char *b)
{
    intmax_t x, y;
    if (strcmp(op, "=") == 0) return strcmp(a, b) == 0;
    if (strcmp(op, "!=") == 0) return strcmp(a, b) != 0;
    if (strcmp(op, "<") == 0) return strcoll(a, b) < 0;
    if (strcmp(op, ">") == 0) return strcoll(a, b) > 0;
    if (strcmp(op, "-ef") == 0) {
        struct stat s1, s2;
        return stat(a, &s1) == 0 && stat(b, &s2) == 0 && s1.st_dev == s2.st_dev &&
               s1.st_ino == s2.st_ino;
    }
    if (strcmp(op, "-nt") == 0 || strcmp(op, "-ot") == 0) {
        struct stat s1, s2;
        int e1 = stat(a, &s1) == 0, e2 = stat(b, &s2) == 0;
        if (op[1] == 'n')
            return e1 && (!e2 || mtime_cmp(&s1, &s2) > 0);
        return e2 && (!e1 || mtime_cmp(&s1, &s2) < 0);
    }
    if (!t_int(a, &x) || !t_int(b, &y))
        return 0;
    if (strcmp(op, "-eq") == 0) return x == y;
    if (strcmp(op, "-ne") == 0) return x != y;
    if (strcmp(op, "-gt") == 0) return x > y;
    if (strcmp(op, "-ge") == 0) return x >= y;
    if (strcmp(op, "-lt") == 0) return x < y;
    if (strcmp(op, "-le") == 0) return x <= y;
    return 0;
}

/* general recursive parser (used for more than four arguments) */
static char **tv;
static int tn, tp;

static int t_or(void);

static int t_prim(void)
{
    if (tp >= tn) {
        sh_warn("argument expected");
        t_err = 1;
        return 0;
    }
    if (strcmp(tv[tp], "!") == 0) {
        tp++;
        return !t_prim();
    }
    if (strcmp(tv[tp], "(") == 0) {
        int r;
        tp++;
        r = t_or();
        if (tp >= tn || strcmp(tv[tp], ")") != 0) {
            sh_warn("')' expected");
            t_err = 1;
            return 0;
        }
        tp++;
        return r;
    }
    if (tp + 2 < tn && is_binary(tv[tp + 1])) {
        int r = binary(tv[tp], tv[tp + 1], tv[tp + 2]);
        tp += 3;
        return r;
    }
    if (is_unary(tv[tp]) && tp + 1 < tn) {
        int r = unary(tv[tp], tv[tp + 1]);
        tp += 2;
        return r;
    }
    return *tv[tp++] != 0;
}

static int t_and(void)
{
    int r = t_prim();
    while (tp < tn && strcmp(tv[tp], "-a") == 0) {
        int s;
        tp++;
        s = t_prim();
        r = r && s;
    }
    return r;
}

static int t_or(void)
{
    int r = t_and();
    while (tp < tn && strcmp(tv[tp], "-o") == 0) {
        int s;
        tp++;
        s = t_and();
        r = r || s;
    }
    return r;
}

static int test_n(char **a, int n);

static int test_n(char **a, int n)
{
    switch (n) {
    case 0:
        return 0;
    case 1:
        return *a[0] != 0;
    case 2:
        if (strcmp(a[0], "!") == 0)
            return *a[1] == 0;
        if (is_unary(a[0]))
            return unary(a[0], a[1]);
        break;
    case 3:
        if (is_binary(a[1]))
            return binary(a[0], a[1], a[2]);
        if (strcmp(a[0], "!") == 0)
            return !test_n(a + 1, 2);
        if (strcmp(a[0], "(") == 0 && strcmp(a[2], ")") == 0)
            return *a[1] != 0;
        break;
    case 4:
        if (strcmp(a[0], "!") == 0)
            return !test_n(a + 1, 3);
        if (strcmp(a[0], "(") == 0 && strcmp(a[3], ")") == 0)
            return test_n(a + 1, 2);
        break;
    }
    tv = a;
    tn = n;
    tp = 0;
    {
        int r = t_or();
        if (tp < tn && !t_err) {
            sh_warn("%s: unexpected operator", tv[tp]);
            t_err = 1;
        }
        return r;
    }
}

int bi_test(int argc, char **argv)
{
    int r;
    t_err = 0;
    if (strcmp(argv[0], "[") == 0) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            sh_warn("missing ]");
            return 2;
        }
        argc--;
    }
    r = test_n(argv + 1, argc - 1);
    if (t_err)
        return 2;
    return !r;
}
