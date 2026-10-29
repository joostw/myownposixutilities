/*
 * re.c - regular expressions with ed's delimiter rules, and the s
 * command's replacement and substitution.
 */
#include "ed.h"

regex_t *last_re;
static Buf repl, prev_repl;
static int have_prev_repl;

/*
 * Translate a delimited RE into a BRE for regcomp: an escaped delimiter
 * becomes that literal character, and the delimiter has no special
 * meaning inside a bracket expression.
 */
static int scan_re(const char **pp, int delim, Buf *out, int *closed)
{
    const char *p = *pp;
    *closed = 0;
    while (*p) {
        if (*p == delim) {
            p++;
            *closed = 1;
            break;
        }
        if (*p == '\\') {
            if (!p[1])
                return fail("Trailing backslash (\\)");
            if (p[1] == delim) {
                if (strchr(".*[]^$\\", delim))
                    buf_addc(out, '\\');
                buf_addc(out, delim);
            } else {
                buf_addc(out, '\\');
                buf_addc(out, p[1]);
            }
            p += 2;
            continue;
        }
        if (*p == '[') {
            const char *q = p + 1;
            if (*q == '^')
                q++;
            if (*q == ']')
                q++;
            while (*q && *q != ']') {
                if (*q == '[' && (q[1] == ':' || q[1] == '.' || q[1] == '=')) {
                    char kind = q[1];
                    const char *e = q + 2;
                    while (*e && !(e[0] == kind && e[1] == ']'))
                        e++;
                    if (!*e)
                        return fail("Unbalanced brackets ([])");
                    q = e + 2;
                    continue;
                }
                q++;
            }
            if (*q != ']')
                return fail("Unbalanced brackets ([])");
            buf_add(out, p, (size_t)(q - p + 1));
            p = q + 1;
            continue;
        }
        buf_addc(out, *p++);
    }
    *pp = p;
    return 0;
}

regex_t *get_re(const char **pp, int delim, int *closed)
{
    Buf b = { 0 };
    regex_t *re;
    int rc;
    if (delim == ' ' || delim == '\n' || delim == '\\' || delim == 0) {
        fail("Invalid pattern delimiter");
        return NULL;
    }
    if (scan_re(pp, delim, &b, closed) < 0) {
        free(b.s);
        return NULL;
    }
    if (!b.len) {
        free(b.s);
        if (!last_re) {
            fail("No previous regular expression");
            return NULL;
        }
        return last_re;
    }
    re = xmalloc(sizeof *re);
    rc = regcomp(re, b.s, 0);
    free(b.s);
    if (rc) {
        static char msg[256];
        regerror(rc, re, msg, sizeof msg);
        free(re);
        fail(msg);
        return NULL;
    }
    if (last_re) {
        regfree(last_re);
        free(last_re);
    }
    last_re = re;
    return re;
}

int re_match(regex_t *re, Line *l, regmatch_t *m, size_t nm, int notbol, size_t start)
{
    m[0].rm_so = (regoff_t)start;
    m[0].rm_eo = (regoff_t)l->len;
    return regexec(re, l->s, nm, m, REG_STARTEND | (notbol ? REG_NOTBOL : 0)) == 0;
}

/*
 * The replacement, stored with escapes: 0x01 c is the literal byte c,
 * '&' is the matched text, '\' d a back-reference, '\' c the byte c.
 * *split is set when it contains an escaped newline.
 */
int parse_replacement(const char **pp, int delim, int *closed, int *split)
{
    const char *p = *pp;
    Buf r = { 0 };
    *closed = 0;
    *split = 0;
    if (p[0] == '%' && (p[1] == delim || p[1] == 0)) {
        if (!have_prev_repl)
            return fail("No previous substitution");
        buf_clear(&repl);
        buf_add(&repl, prev_repl.s ? prev_repl.s : "", prev_repl.len);
        p++;
        if (*p == delim) {
            p++;
            *closed = 1;
        }
        *pp = p;
        for (size_t i = 0; i < repl.len; i++)
            if (repl.s[i] == '\n')
                *split = 1;
        {
            size_t nsub = last_re ? last_re->re_nsub : 0;
            for (size_t i = 0; i + 1 < repl.len; i++) {
                if (repl.s[i] == 0x01) {
                    i++;
                    continue;
                }
                if (repl.s[i] == '\\') {
                    char d = repl.s[i + 1];
                    if (d >= '1' && d <= '9' && (size_t)(d - '0') > nsub)
                        return fail("Invalid back reference");
                    i++;
                }
            }
        }
        return 0;
    }
    while (*p && *p != delim) {
        if (*p == '\\') {
            if (!p[1]) {
                /* an escaped newline: the caller supplies the rest */
                buf_addc(&r, 0x01);
                buf_addc(&r, '\n');
                *split = 1;
                p++;
                continue;
            }
            if (p[1] == delim) {
                buf_addc(&r, 0x01);
                buf_addc(&r, delim);
            } else if (p[1] == '\n') {
                buf_addc(&r, 0x01);
                buf_addc(&r, '\n');
                *split = 1;
            } else {
                buf_addc(&r, '\\');
                buf_addc(&r, p[1]);
            }
            p += 2;
            continue;
        }
        if (*p == 0x01) {
            buf_addc(&r, 0x01);
            buf_addc(&r, 0x01);
            p++;
            continue;
        }
        buf_addc(&r, *p++);
    }
    if (*p == delim) {
        p++;
        *closed = 1;
    }
    *pp = p;
    {
        /* a back-reference to a subexpression the RE does not have */
        size_t nsub = last_re ? last_re->re_nsub : 0;
        for (size_t i = 0; i + 1 < r.len; i++) {
            if (r.s[i] == 0x01) {
                i++;
                continue;
            }
            if (r.s[i] == '\\') {
                char d = r.s[i + 1];
                if (d >= '1' && d <= '9' && (size_t)(d - '0') > nsub) {
                    free(r.s);
                    return fail("Invalid back reference");
                }
                i++;
            }
        }
    }
    buf_clear(&repl);
    buf_add(&repl, r.s ? r.s : "", r.len);
    buf_clear(&prev_repl);
    buf_add(&prev_repl, r.s ? r.s : "", r.len);
    have_prev_repl = 1;
    free(r.s);
    return 0;
}

static void apply_repl(Buf *out, Line *l, regmatch_t *m)
{
    for (size_t i = 0; i < repl.len; i++) {
        unsigned char c = (unsigned char)repl.s[i];
        if (c == 0x01 && i + 1 < repl.len) {
            buf_addc(out, repl.s[++i]);
        } else if (c == '&') {
            buf_add(out, l->s + m[0].rm_so, (size_t)(m[0].rm_eo - m[0].rm_so));
        } else if (c == '\\' && i + 1 < repl.len) {
            char d = repl.s[++i];
            if (d >= '1' && d <= '9') {
                int k = d - '0';
                if (m[k].rm_so >= 0)
                    buf_add(out, l->s + m[k].rm_so, (size_t)(m[k].rm_eo - m[k].rm_so));
            } else
                buf_addc(out, d);
        } else
            buf_addc(out, (char)c);
    }
}

static size_t char_len(Line *l, size_t pos)
{
    mbstate_t st;
    size_t n;
    memset(&st, 0, sizeof st);
    n = mbrlen(l->s + pos, l->len - pos, &st);
    if (n == (size_t)-1 || n == (size_t)-2 || n == 0)
        return 1;
    return n;
}

/* substitute in one line; returns 1 if a substitution was made */
static int subst_line(Line *l, regex_t *re, int gflag, long count, Buf *out)
{
    regmatch_t m[10];
    size_t pos = 0;
    long nth = 0;
    int did = 0;
    long prev_end = -1;
    buf_clear(out);
    while (pos <= l->len) {
        if (!re_match(re, l, m, 10, pos > 0, pos))
            break;
        if (m[0].rm_so == m[0].rm_eo && (long)m[0].rm_so == prev_end) {
            /* an empty match right after the previous match */
            if ((size_t)m[0].rm_so >= l->len)
                break;
            {
                size_t cl = char_len(l, (size_t)m[0].rm_so);
                buf_add(out, l->s + pos, (size_t)m[0].rm_so - pos + cl);
                pos = (size_t)m[0].rm_so + cl;
            }
            continue;
        }
        nth++;
        buf_add(out, l->s + pos, (size_t)m[0].rm_so - pos);
        if (gflag || nth == count) {
            apply_repl(out, l, m);
            did = 1;
        } else
            buf_add(out, l->s + m[0].rm_so, (size_t)(m[0].rm_eo - m[0].rm_so));
        prev_end = m[0].rm_eo;
        if (m[0].rm_eo == m[0].rm_so) {
            if ((size_t)m[0].rm_eo >= l->len) {
                pos = l->len + 1;
                break;
            }
            {
                size_t cl = char_len(l, (size_t)m[0].rm_eo);
                buf_add(out, l->s + m[0].rm_eo, cl);
                pos = (size_t)m[0].rm_eo + cl;
            }
        } else
            pos = (size_t)m[0].rm_eo;
        if (!gflag && nth >= count)
            break;
        if (interrupted)
            break;
    }
    if (pos < l->len)
        buf_add(out, l->s + pos, l->len - pos);
    return did;
}

int substitute(long from, long to, regex_t *re, int gflag, long count, int in_global)
{
    Buf out = { 0 };
    long last = 0;
    for (long i = from; i <= to; i++) {
        if (interrupted) {
            free(out.s);
            return fail("Interrupt");
        }
        if (!subst_line(lines[i], re, gflag, count, &out))
            continue;
        {
            /* the new text, split at newlines */
            Line **v = NULL;
            long nv = 0;
            size_t st = 0;
            const char *s = out.s ? out.s : "";
            for (size_t k = 0; k <= out.len; k++)
                if (k == out.len || s[k] == '\n') {
                    v = xrealloc(v, (size_t)(nv + 1) * sizeof(Line *));
                    v[nv++] = line_new(s + st, k - st);
                    st = k + 1;
                }
            buf_replace(i, v[0]);
            if (nv > 1) {
                buf_insert(i, v + 1, nv - 1);
                i += nv - 1;
                to += nv - 1;
            }
            last = i;
            free(v);
        }
    }
    free(out.s);
    if (!last) {
        if (in_global)
            return 0;
        return fail("No match");
    }
    cur = last;
    return 1;
}
