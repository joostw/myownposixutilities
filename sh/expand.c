/*
 * expand.c - word expansions (2.6): tilde, parameter, command substitution,
 * arithmetic, field splitting, pathname expansion, quote removal; and the
 * pattern matching notation (2.14).
 */
#include "sh.h"
#include <dirent.h>
#include <pwd.h>

int cmdsub_status_set, cmdsub_status;

/* per-byte flags of expansion output */
#define F_Q    1    /* quoted: literal for patterns, never split */
#define F_EXP  2    /* result of an unquoted expansion: subject to splitting */
#define F_MARK 4    /* zero-width marker for quoting characters */

typedef struct XBuf {
    char *c;
    unsigned char *f;
    size_t len, cap;
} XBuf;

typedef struct XState {
    XBuf cur;
    XBuf *fields;
    int nf, fcap;
    int mode;           /* EXP_* */
    Word *w;            /* for command substitution lookup */
    int dq_had_at;      /* "$@" seen in the current double-quoted region */
} XState;

static void xb_add(XBuf *b, int c, int f)
{
    if (b->len + 1 >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        b->c = xrealloc(b->c, b->cap);
        b->f = xrealloc(b->f, b->cap);
    }
    b->c[b->len] = (char)c;
    b->f[b->len] = (unsigned char)f;
    b->len++;
}

static void xb_adds(XBuf *b, const char *s, size_t n, int f)
{
    while (n--)
        xb_add(b, (unsigned char)*s++, f);
}

static void xs_break(XState *x)
{
    if (x->nf == x->fcap) {
        x->fcap = x->fcap ? x->fcap * 2 : 8;
        x->fields = xrealloc(x->fields, (size_t)x->fcap * sizeof(XBuf));
    }
    x->fields[x->nf++] = x->cur;
    memset(&x->cur, 0, sizeof x->cur);
}

static void xs_free(XState *x)
{
    int i;
    for (i = 0; i < x->nf; i++) {
        free(x->fields[i].c);
        free(x->fields[i].f);
    }
    free(x->fields);
    free(x->cur.c);
    free(x->cur.f);
}

/* ------------------------------------------------------------------ */
/* encoded string navigation                                          */
/* ------------------------------------------------------------------ */

/* p points just after CTLVAR; return pointer after the matching CTLENDVAR */
static const char *skip_var(const char *p)
{
    int depth = 1;
    while (*p) {
        char c = *p++;
        if (c == CTLESC) {
            if (*p) p++;
        } else if (c == CTLVAR) {
            p++;            /* subtype */
            while (*p && *p != '=') p++;
            if (*p) p++;
            depth++;
        } else if (c == CTLENDVAR) {
            if (--depth == 0)
                return p;
        }
    }
    return p;
}

static const char *skip_ari(const char *p)
{
    int depth = 1;
    while (*p) {
        char c = *p++;
        if (c == CTLESC) {
            if (*p) p++;
        } else if (c == CTLVAR) {
            p++;
            while (*p && *p != '=') p++;
            if (*p) p++;
        } else if (c == CTLARI)
            depth++;
        else if (c == CTLENDARI) {
            if (--depth == 0)
                return p;
        }
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* parameters                                                         */
/* ------------------------------------------------------------------ */

static char numbuf[64];

/* value of a parameter, or NULL if unset; *isat set for @ and * */
static const char *param_value(const char *name, int *special)
{
    *special = 0;
    if (isdigit((unsigned char)name[0])) {
        long n = strtol(name, NULL, 10);
        if (n == 0)
            return arg_zero;
        if (n > posparams.n)
            return NULL;
        return posparams.v[n - 1];
    }
    if (!name[1] && !isalpha((unsigned char)name[0]) && name[0] != '_') {
        switch (name[0]) {
        case '@': case '*':
            *special = name[0];
            return posparams.n ? "" : NULL;
        case '#':
            snprintf(numbuf, sizeof numbuf, "%d", posparams.n);
            return numbuf;
        case '?':
            snprintf(numbuf, sizeof numbuf, "%d", exitstatus);
            return numbuf;
        case '-':
            return optflags_string();
        case '$':
            snprintf(numbuf, sizeof numbuf, "%ld", (long)shell_pid);
            return numbuf;
        case '!':
            if (!last_bgpid_set)
                return NULL;
            snprintf(numbuf, sizeof numbuf, "%ld", (long)last_bgpid);
            return numbuf;
        }
        return NULL;
    }
    if (strcmp(name, "LINENO") == 0) {
        Var *v = var_lookup("LINENO");
        if (v && v->val)
            return v->val;
    }
    return var_get(name);
}

static size_t mb_count(const char *s)
{
    size_t n = 0, i = 0, len = strlen(s);
    mbstate_t st;
    memset(&st, 0, sizeof st);
    while (i < len) {
        size_t k = mbrtowc(NULL, s + i, len - i, &st);
        if (k == (size_t)-1 || k == (size_t)-2 || k == 0) {
            memset(&st, 0, sizeof st);
            k = 1;
        }
        i += k;
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* command substitution                                               */
/* ------------------------------------------------------------------ */

static char *run_cmdsub(Node *n)
{
    int pfd[2];
    pid_t pid;
    StrBuf b;
    char buf[4096];
    int st;
    flushall();
    if (pipe(pfd) < 0)
        sh_error("cannot create pipe: %s", strerror(errno));
    pid = fork();
    if (pid < 0)
        sh_error("cannot fork: %s", strerror(errno));
    if (pid == 0) {
        close(pfd[0]);
        enter_subshell(1);
        if (pfd[1] != 1) {
            dup2(pfd[1], 1);
            close(pfd[1]);
        }
        eval_tree(n, EV_EXIT);
        exitshell(exitstatus);
    }
    close(pfd[1]);
    sb_init(&b);
    for (;;) {
        ssize_t k = read(pfd[0], buf, sizeof buf);
        if (k < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (k == 0)
            break;
        sb_add(&b, buf, (size_t)k);
    }
    close(pfd[0]);
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
        ;
    cmdsub_status = wait_status_to_exit(st);
    cmdsub_status_set = 1;
    exitstatus = cmdsub_status;
    while (b.len && b.s[b.len - 1] == '\n')
        b.s[--b.len] = 0;
    {
        /* drop NUL bytes (unspecified) */
        size_t i, j = 0;
        for (i = 0; i < b.len; i++)
            if (b.s[i]) b.s[j++] = b.s[i];
        b.len = j;
        if (b.s) b.s[j] = 0;
    }
    return sb_stack(&b);
}

/* ------------------------------------------------------------------ */
/* the expansion walk                                                 */
/* ------------------------------------------------------------------ */

static void expand_range(XState *x, const char *p, const char *end, int quoted, int litflag);
static char *expand_sub_string(XState *outer, const char *p, const char *end, int mode, int quoted);
char *pattern_from_buf(XBuf *b);

static void add_result(XState *x, const char *s, int quoted)
{
    xb_adds(&x->cur, s, strlen(s), quoted ? F_Q : F_EXP);
}

/* tilde expansion at p (pointing at '~'); returns pointer after prefix */
static const char *do_tilde(XState *x, const char *p, const char *end, int assign)
{
    const char *q = p + 1;
    StrBuf user;
    const char *home = NULL;
    sb_init(&user);
    while (q < end && *q && *q != '/' && !(assign && *q == ':')) {
        if (ISCTL(*q) || !(isalnum((unsigned char)*q) || strchr("._-", *q))) {
            sb_free(&user);
            return p;   /* not a portable login name: no expansion */
        }
        sb_addc(&user, *q);
        q++;
    }
    if (!user.len) {
        home = var_get("HOME");
        if (!home) {
            sb_free(&user);
            return p;
        }
    } else {
        struct passwd *pw = getpwnam(user.s);
        if (!pw) {
            sb_free(&user);
            return p;
        }
        home = pw->pw_dir;
    }
    sb_free(&user);
    {
        size_t n = strlen(home);
        if (q < end && *q == '/' && n > 1 && home[n - 1] == '/')
            n--;
        xb_adds(&x->cur, home, n, F_Q);
        if (n == 0)
            xb_add(&x->cur, 0, F_MARK);
    }
    return q;
}

/* the word of ${x-word}: tilde expansion at its start, then the rest */
static void expand_varword(XState *x, const char *p, const char *end, int quoted)
{
    if (!quoted && p < end && *p == '~')
        p = do_tilde(x, p, end, 0);
    expand_range(x, p, end, quoted, quoted ? 0 : F_EXP);
}

static void param_error(const char *name, const char *msg)
{
    sh_error("%s: %s", name, msg);
}

static void join_params(XState *x, int sep, int quoted)
{
    int i;
    for (i = 0; i < posparams.n; i++) {
        if (i && sep)
            xb_add(&x->cur, sep, quoted ? F_Q : F_EXP);
        add_result(x, posparams.v[i], quoted);
    }
}

static void expand_at(XState *x, int which, int quoted)
{
    int i;
    int split_ctx = (x->mode & EXP_FULL) != 0;
    if (quoted && which == '@') {
        x->dq_had_at = 1;
        if (!split_ctx) {
            join_params(x, ' ', 1);
            return;
        }
        for (i = 0; i < posparams.n; i++) {
            if (i)
                xs_break(x);
            xb_add(&x->cur, 0, F_MARK);     /* each parameter is a field */
            add_result(x, posparams.v[i], 1);
        }
        return;
    }
    if (quoted || !split_ctx) {
        const char *ifs = var_get("IFS");
        int sep = which == '@' ? ' ' : (!ifs ? ' ' : (unsigned char)ifs[0]);
        join_params(x, sep, quoted);
        return;
    }
    /* unquoted in a splitting context: one field per parameter */
    for (i = 0; i < posparams.n; i++) {
        if (i)
            xs_break(x);
        add_result(x, posparams.v[i], 0);
    }
}

/* ${...}; p points after CTLVAR, returns pointer after CTLENDVAR */
static const char *expand_var(XState *x, const char *p, int quoted)
{
    int sub = (unsigned char)*p++;
    int colon = 0;
    char name[256];
    size_t nl = 0;
    const char *word, *wend, *after;
    const char *val;
    int special;
    if (sub >= VS_NORMAL + VS_COLON_OFF) {
        colon = 1;
        sub -= VS_COLON_OFF;
    }
    while (*p && *p != '=' && nl < sizeof name - 1)
        name[nl++] = *p++;
    name[nl] = 0;
    if (*p == '=') p++;
    word = p;
    after = skip_var(p);
    wend = after - 1;           /* the CTLENDVAR */
    val = param_value(name, &special);

    if (sub == VS_LENGTH) {
        if (!val && opts[O_NOUNSET] && !special)
            param_error(name, "parameter not set");
        if (special) {
            /* unspecified: length of "$*" */
            size_t tot = 0;
            int i;
            for (i = 0; i < posparams.n; i++)
                tot += mb_count(posparams.v[i]) + (i ? 1 : 0);
            snprintf(numbuf, sizeof numbuf, "%zu", tot);
        } else
            snprintf(numbuf, sizeof numbuf, "%zu", val ? mb_count(val) : (size_t)0);
        add_result(x, numbuf, quoted);
        return after;
    }

    {
        int set = val != NULL;
        int isnull = !val || (!special ? !*val : posparams.n == 0 ||
                     (posparams.n == 1 && !*posparams.v[0] && special == '*') );
        int use_default;
        if (special == '@' && colon)
            isnull = posparams.n == 0;
        use_default = colon ? isnull : !set;

        switch (sub) {
        case VS_NORMAL:
            if (!set && opts[O_NOUNSET] && !special)
                param_error(name, "parameter not set");
            break;
        case VS_MINUS:
            if (use_default) {
                expand_varword(x, word, wend, quoted);
                return after;
            }
            break;
        case VS_PLUS:
            if (!use_default)
                expand_varword(x, word, wend, quoted);
            return after;
        case VS_ASSIGN:
            if (use_default) {
                char *s;
                if (special || isdigit((unsigned char)name[0]) || !is_name(name))
                    sh_error("%s: cannot assign in this way", name);
                s = expand_sub_string(x, word, wend, EXP_NOSPLIT | EXP_TILDE, 0);
                if (var_set(name, s, 0) < 0)
                    sh_error("%s: is read only", name);
                val = var_get(name);
                special = 0;
            }
            break;
        case VS_QUESTION:
            if (use_default) {
                char *msg = expand_sub_string(x, word, wend, EXP_NOSPLIT | EXP_TILDE, 0);
                if (!*msg)
                    msg = (char *)(set ? "parameter null" : "parameter not set");
                param_error(name, msg);
            }
            break;
        default: {
            /* pattern removal */
            char *pat;
            const char *v;
            size_t len, i, best;
            int found = 0;
            if (!set && opts[O_NOUNSET] && !special)
                param_error(name, "parameter not set");
            if (special) {
                /* unspecified: operate on "$*" */
                XState t;
                memset(&t, 0, sizeof t);
                t.mode = EXP_NOSPLIT;
                join_params(&t, ' ', 1);
                xb_add(&t.cur, 0, F_Q);
                v = ststrdup(t.cur.c);
                xs_free(&t);
            } else
                v = val ? val : "";
            pat = expand_sub_string(x, word, wend, EXP_PATTERN | EXP_TILDE, 0);
            len = strlen(v);
            best = 0;
            if (sub == VS_TRIMLEFT || sub == VS_TRIMLEFTMAX) {
                char *tmp = stalloc(len + 1);
                if (sub == VS_TRIMLEFT) {
                    for (i = 0; i <= len; i++) {
                        memcpy(tmp, v, i); tmp[i] = 0;
                        if (pattern_match(pat, tmp)) { best = i; found = 1; break; }
                    }
                } else {
                    for (i = len + 1; i-- > 0; ) {
                        memcpy(tmp, v, i); tmp[i] = 0;
                        if (pattern_match(pat, tmp)) { best = i; found = 1; break; }
                    }
                }
                add_result(x, found ? v + best : v, quoted);
            } else {
                if (sub == VS_TRIMRIGHT) {
                    for (i = len + 1; i-- > 0; )
                        if (pattern_match(pat, v + i)) { best = i; found = 1; break; }
                } else {
                    for (i = 0; i <= len; i++)
                        if (pattern_match(pat, v + i)) { best = i; found = 1; break; }
                }
                if (found) {
                    char *r = ststrndup(v, best);
                    add_result(x, r, quoted);
                } else
                    add_result(x, v, quoted);
            }
            return after;
        }
        }
    }
    if (special) {
        expand_at(x, special, quoted);
        return after;
    }
    if (val)
        add_result(x, val, quoted);
    return after;
}

/* expand a nested range to a single string (no field splitting) */
static char *expand_sub_string(XState *outer, const char *p, const char *end, int mode, int quoted)
{
    XState t;
    char *r;
    memset(&t, 0, sizeof t);
    t.mode = mode;
    t.w = outer->w;
    if ((mode & EXP_TILDE) && p < end && *p == '~' && !quoted)
        p = do_tilde(&t, p, end, 0);
    expand_range(&t, p, end, quoted, 0);
    if (mode & EXP_PATTERN)
        r = pattern_from_buf(&t.cur);
    else {
        StrBuf b;
        size_t i;
        sb_init(&b);
        for (i = 0; i < t.cur.len; i++)
            if (!(t.cur.f[i] & F_MARK))
                sb_addc(&b, t.cur.c[i]);
        r = sb_stack(&b);
    }
    xs_free(&t);
    return r;
}

static void expand_range(XState *x, const char *p, const char *end, int quoted, int litflag)
{
    while (p < end && *p) {
        char c = *p++;
        switch (c) {
        case CTLESC:
            if (p < end && *p)
                xb_add(&x->cur, (unsigned char)*p++, F_Q);
            break;
        case CTLQNULL:
            xb_add(&x->cur, 0, F_MARK);
            break;
        case CTLQUOTE: {
            int save_at = x->dq_had_at;
            x->dq_had_at = 0;
            /* find the matching CTLENDQ */
            {
                const char *q = p;
                int depth = 1;
                while (q < end && *q) {
                    char d = *q++;
                    if (d == CTLESC) { if (*q) q++; }
                    else if (d == CTLQUOTE) depth++;
                    else if (d == CTLENDQ) { if (--depth == 0) break; }
                    else if (d == CTLVAR) {
                        q++;
                        while (*q && *q != '=') q++;
                        if (*q) q++;
                        q = skip_var(q);
                    } else if (d == CTLARI)
                        q = skip_ari(q);
                }
                expand_range(x, p, depth == 0 ? q - 1 : q, 1, 0);
                p = q;
            }
            if (!x->dq_had_at)
                xb_add(&x->cur, 0, F_MARK);
            x->dq_had_at = save_at;
            break;
        }
        case CTLENDQ:
            break;
        case CTLVAR:
            p = expand_var(x, p, quoted);
            break;
        case CTLBACKQ: {
            int idx = atoi(p);
            char *out;
            while (*p && *p != ';') p++;
            if (*p) p++;
            if (!x->w || idx >= x->w->nsubs)
                break;
            out = run_cmdsub(x->w->subs[idx]);
            add_result(x, out, quoted);
            break;
        }
        case CTLARI: {
            const char *e = skip_ari(p);
            char *expr = expand_sub_string(x, p, e - 1, EXP_NOSPLIT, 1);
            long v = arith_eval(expr);
            snprintf(numbuf, sizeof numbuf, "%ld", v);
            add_result(x, numbuf, quoted);
            p = e;
            break;
        }
        case CTLENDVAR: case CTLENDARI:
            break;
        default:
            if (!quoted && (x->mode & EXP_ASSIGN) && c == ':' && p < end && *p == '~') {
                xb_add(&x->cur, ':', litflag);
                p = do_tilde(x, p, end, 1);
                break;
            }
            xb_add(&x->cur, (unsigned char)c, quoted ? F_Q : litflag);
        }
    }
}

/* ------------------------------------------------------------------ */
/* field splitting (2.6.5)                                            */
/* ------------------------------------------------------------------ */

static int is_ifs_ws(int c, const char *ifs)
{
    return (c == ' ' || c == '\t' || c == '\n') && strchr(ifs, c);
}

static void split_field(XBuf *in, XBuf **out, int *nout, int *outcap)
{
    const char *ifs = ifs_value();
    XBuf cand;
    size_t i = 0;
    int any_exp = 0, has_content = 0;
    for (i = 0; i < in->len; i++) {
        if (in->f[i] & F_EXP) any_exp = 1;
        has_content = 1;
    }
#define EMIT(b) do { \
        if (*nout == *outcap) { *outcap = *outcap ? *outcap * 2 : 8; \
            *out = xrealloc(*out, (size_t)*outcap * sizeof(XBuf)); } \
        (*out)[(*nout)++] = (b); } while (0)
    if (!has_content) {
        free(in->c);
        free(in->f);
        return;
    }
    if (!any_exp || (ifs_isset() && !*ifs)) {
        EMIT(*in);
        return;
    }
    memset(&cand, 0, sizeof cand);
    i = 0;
    while (i < in->len) {
        int seen_nonws = 0;
        unsigned char ch = (unsigned char)in->c[i];
        int f = in->f[i];
        if (!(f & F_EXP) || !strchr(ifs, ch) || ch == 0) {
            xb_add(&cand, ch, f);
            i++;
            continue;
        }
        /* an IFS character from an expansion */
        while (i < in->len && (in->f[i] & F_EXP) && is_ifs_ws((unsigned char)in->c[i], ifs))
            i++;
        if (i < in->len && (in->f[i] & F_EXP) && in->c[i] &&
            strchr(ifs, (unsigned char)in->c[i]) && !is_ifs_ws((unsigned char)in->c[i], ifs)) {
            seen_nonws = 1;
            i++;
            /* IFS white space after a non-white-space delimiter is part of it */
            while (i < in->len && (in->f[i] & F_EXP) && is_ifs_ws((unsigned char)in->c[i], ifs))
                i++;
        }
        if (cand.len || seen_nonws) {
            EMIT(cand);
            memset(&cand, 0, sizeof cand);
        }
    }
    if (cand.len)
        EMIT(cand);
    else {
        free(cand.c);
        free(cand.f);
    }
    free(in->c);
    free(in->f);
#undef EMIT
}

/* ------------------------------------------------------------------ */
/* pattern conversion                                                 */
/* ------------------------------------------------------------------ */

/* build a backslash-escaped pattern from expansion output */
char *pattern_from_buf(XBuf *b)
{
    StrBuf s;
    size_t i;
    sb_init(&s);
    for (i = 0; i < b->len; i++) {
        int f = b->f[i];
        char c = b->c[i];
        if (f & F_MARK)
            continue;
        if ((f & F_Q) && (strchr("*?[]\\!^-", c) && c))
            sb_addc(&s, '\\');
        sb_addc(&s, c);
    }
    return sb_stack(&s);
}

static int buf_has_glob(XBuf *b)
{
    size_t i;
    for (i = 0; i < b->len; i++)
        if (!(b->f[i] & (F_Q | F_MARK)) && (b->c[i] == '*' || b->c[i] == '?' || b->c[i] == '['))
            return 1;
    return 0;
}

static char *buf_literal(XBuf *b)
{
    StrBuf s;
    size_t i;
    sb_init(&s);
    for (i = 0; i < b->len; i++)
        if (!(b->f[i] & F_MARK))
            sb_addc(&s, b->c[i]);
    return sb_stack(&s);
}

/* ------------------------------------------------------------------ */
/* pattern matching                                                   */
/* ------------------------------------------------------------------ */

static size_t getwc_at(const char *s, wchar_t *wc)
{
    mbstate_t st;
    size_t k;
    if (!*s) { *wc = 0; return 0; }
    if ((unsigned char)*s < 0x80) { *wc = (unsigned char)*s; return 1; }
    memset(&st, 0, sizeof st);
    k = mbrtowc(wc, s, strlen(s), &st);
    if (k == (size_t)-1 || k == (size_t)-2 || k == 0) {
        *wc = (unsigned char)*s;
        return 1;
    }
    return k;
}

static int wc_coll(wchar_t a, wchar_t b)
{
    wchar_t x[2] = { a, 0 }, y[2] = { b, 0 };
    return wcscoll(x, y);
}

/*
 * Match a bracket expression at p (just after '['), against wc.
 * Returns the pointer after ']' or NULL if p does not start a valid
 * bracket expression.  *matched receives the result.
 */
static const char *match_bracket(const char *p, wchar_t wc, int *matched, int pathname)
{
    int neg = 0, ok = 0, first = 1;
    if (*p == '!' || *p == '^') {
        neg = 1;
        p++;
    }
    for (;;) {
        wchar_t lo, hi;
        size_t k;
        if (!*p)
            return NULL;
        if (*p == ']' && !first)
            break;
        first = 0;
        if (pathname && *p == '/')
            return NULL;
        if (p[0] == '[' && (p[1] == ':' || p[1] == '.' || p[1] == '=')) {
            char d = p[1];
            const char *q = p + 2, *e = q;
            while (*e && !(e[0] == d && e[1] == ']'))
                e++;
            if (!*e)
                return NULL;
            if (d == ':') {
                char cls[32];
                size_t n = (size_t)(e - q);
                if (n >= sizeof cls) n = sizeof cls - 1;
                memcpy(cls, q, n);
                cls[n] = 0;
                {
                    wctype_t t = wctype(cls);
                    if (t && iswctype((wint_t)wc, t))
                        ok = 1;
                }
                p = e + 2;
                continue;
            }
            getwc_at(q, &lo);
            p = e + 2;
            goto range;
        }
        if (*p == '\\' && p[1])
            p++;
        k = getwc_at(p, &lo);
        p += k;
range:
        if (p[0] == '-' && p[1] && p[1] != ']') {
            p++;
            if (p[0] == '[' && (p[1] == '.' || p[1] == '=')) {
                char d = p[1];
                const char *q = p + 2, *e = q;
                while (*e && !(e[0] == d && e[1] == ']'))
                    e++;
                if (!*e)
                    return NULL;
                getwc_at(q, &hi);
                p = e + 2;
            } else {
                if (*p == '\\' && p[1])
                    p++;
                p += getwc_at(p, &hi);
            }
            if (wc_coll(lo, wc) <= 0 && wc_coll(wc, hi) <= 0)
                ok = 1;
        } else if (lo == wc)
            ok = 1;
    }
    *matched = ok ^ neg;
    return p + 1;
}

static int pmatch(const char *pat, const char *s, int pathname, int period)
{
    const char *star_p = NULL, *star_s = NULL;
    const char *start = s;
    for (;;) {
        if (!*pat) {
            if (!*s)
                return 1;
            goto backtrack;
        }
        if (*pat == '*') {
            if (period && s == start && *s == '.')
                return 0;
            while (*pat == '*')
                pat++;
            star_p = pat;
            star_s = s;
            continue;
        }
        if (!*s)
            return 0;
        {
            wchar_t wc;
            size_t sk = getwc_at(s, &wc);
            if (*pat == '?') {
                if (pathname && *s == '/') goto backtrack;
                if (period && s == start && *s == '.') return 0;
                pat++;
                s += sk;
                continue;
            }
            if (*pat == '[') {
                int m = 0;
                const char *np = match_bracket(pat + 1, wc, &m, pathname);
                if (np) {
                    if (period && s == start && *s == '.')
                        return 0;
                    if (!m || (pathname && *s == '/'))
                        goto backtrack;
                    pat = np;
                    s += sk;
                    continue;
                }
                /* not a bracket expression: literal '[' */
            }
            {
                const char *pp = pat;
                wchar_t pc;
                size_t pk;
                if (*pp == '\\' && pp[1])
                    pp++;
                pk = getwc_at(pp, &pc);
                if (pc != wc)
                    goto backtrack;
                pat = pp + pk;
                s += sk;
                continue;
            }
        }
backtrack:
        if (!star_p || !*star_s)
            return 0;
        if (pathname && *star_s == '/')
            return 0;
        {
            wchar_t wc;
            star_s += getwc_at(star_s, &wc);
        }
        pat = star_p;
        s = star_s;
    }
}

int pattern_match(const char *pat, const char *s)
{
    return pmatch(pat, s, 0, 0);
}

int has_glob_chars(const char *pat)
{
    for (; *pat; pat++) {
        if (*pat == '\\' && pat[1]) { pat++; continue; }
        if (*pat == '*' || *pat == '?' || *pat == '[')
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* pathname expansion                                                 */
/* ------------------------------------------------------------------ */

static void unescape_into(StrBuf *b, const char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] == '\\' && i + 1 < n)
            i++;
        sb_addc(b, p[i]);
    }
}

static int comp_has_glob(const char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] == '\\' && i + 1 < n) { i++; continue; }
        if (p[i] == '*' || p[i] == '?')
            return 1;
        if (p[i] == '[') {
            /* only a valid bracket expression counts */
            size_t j = i + 1;
            if (j < n && (p[j] == '!' || p[j] == '^')) j++;
            if (j < n && p[j] == ']') j++;
            while (j < n && p[j] != ']') {
                if (p[j] == '[' && j + 1 < n && (p[j+1] == ':' || p[j+1] == '.' || p[j+1] == '=')) {
                    char d = p[j+1];
                    j += 2;
                    while (j + 1 < n && !(p[j] == d && p[j+1] == ']')) j++;
                    j += 2;
                    continue;
                }
                j++;
            }
            if (j < n)
                return 1;
        }
    }
    return 0;
}

static void glob_rec(const char *dir, const char *pat, ArgList *res)
{
    const char *slash = strchr(pat, '/');
    size_t clen = slash ? (size_t)(slash - pat) : strlen(pat);
    const char *rest = slash ? slash + 1 : NULL;
    StrBuf path;

    /* collapse multiple slashes */
    while (rest && *rest == '/')
        rest++;

    if (!comp_has_glob(pat, clen)) {
        struct stat st;
        sb_init(&path);
        sb_adds(&path, dir);
        unescape_into(&path, pat, clen);
        if (rest) {
            if (*rest == 0) {
                sb_addc(&path, '/');
                if (stat(path.s, &st) == 0 && S_ISDIR(st.st_mode))
                    arglist_add(res, sb_stack(&path));
                else
                    sb_free(&path);
                return;
            }
            sb_addc(&path, '/');
            glob_rec(path.s, rest, res);
            sb_free(&path);
        } else {
            if (lstat(path.s, &st) == 0)
                arglist_add(res, sb_stack(&path));
            else
                sb_free(&path);
        }
        return;
    }
    {
        char *cpat = ststrndup(pat, clen);
        DIR *d = opendir(*dir ? dir : ".");
        struct dirent *de;
        if (!d)
            return;
        while ((de = readdir(d))) {
            const char *nm = de->d_name;
            if (nm[0] == '.' && (!nm[1] || (nm[1] == '.' && !nm[2])))
                continue;
            if (nm[0] == '.' && cpat[0] != '.')
                continue;
            if (!pmatch(cpat, nm, 1, 1))
                continue;
            sb_init(&path);
            sb_adds(&path, dir);
            sb_adds(&path, nm);
            if (rest) {
                struct stat st;
                if (*rest == 0) {
                    if (stat(path.s, &st) == 0 && S_ISDIR(st.st_mode)) {
                        sb_addc(&path, '/');
                        arglist_add(res, sb_stack(&path));
                    } else
                        sb_free(&path);
                    continue;
                }
                sb_addc(&path, '/');
                glob_rec(path.s, rest, res);
                sb_free(&path);
            } else
                arglist_add(res, sb_stack(&path));
        }
        closedir(d);
    }
}

static int path_cmp(const void *a, const void *b)
{
    const char *x = *(char *const *)a, *y = *(char *const *)b;
    int r = strcoll(x, y);
    return r ? r : strcmp(x, y);
}

static int glob_pattern(const char *pat, ArgList *out)
{
    ArgList res = { NULL, 0, 0 };
    int i;
    if (pat[0] == '/') {
        const char *p = pat;
        while (*p == '/') p++;
        glob_rec("/", p, &res);
    } else
        glob_rec("", pat, &res);
    if (res.n == 0) {
        free(res.v);
        return 0;
    }
    qsort(res.v, (size_t)res.n, sizeof(char *), path_cmp);
    for (i = 0; i < res.n; i++)
        arglist_add(out, res.v[i]);
    free(res.v);
    return 1;
}

/* ------------------------------------------------------------------ */
/* public entry points                                                */
/* ------------------------------------------------------------------ */

void arglist_add(ArgList *l, char *s)
{
    if (l->n + 1 >= l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = xrealloc(l->v, (size_t)l->cap * sizeof(char *));
    }
    l->v[l->n++] = s;
    l->v[l->n] = NULL;
}

static void expand_one(Word *w, int mode, XState *x)
{
    const char *p = w->s, *end = p + strlen(p);
    memset(x, 0, sizeof *x);
    x->mode = mode;
    x->w = w;
    if ((mode & (EXP_TILDE | EXP_ASSIGN)) && *p == '~')
        p = do_tilde(x, p, end, (mode & EXP_ASSIGN) != 0);
    expand_range(x, p, end, (mode & EXP_HEREDOC) != 0, 0);
    xs_break(x);
}

/* full expansion of one word, appending fields to out */
void expand_word_full(Word *w, ArgList *out)
{
    XState x;
    XBuf *flds = NULL;
    int nfl = 0, cap = 0, i;
    expand_one(w, EXP_FULL | EXP_TILDE, &x);
    for (i = 0; i < x.nf; i++)
        split_field(&x.fields[i], &flds, &nfl, &cap);
    x.nf = 0;
    for (i = 0; i < nfl; i++) {
        XBuf *b = &flds[i];
        if (!opts[O_NOGLOB] && buf_has_glob(b)) {
            char *pat = pattern_from_buf(b);
            if (!glob_pattern(pat, out))
                arglist_add(out, buf_literal(b));
        } else
            arglist_add(out, buf_literal(b));
        free(b->c);
        free(b->f);
    }
    free(flds);
    xs_free(&x);
}

void expand_words(Word *w, ArgList *out)
{
    for (; w; w = w->next)
        expand_word_full(w, out);
}

/* expansion to a single string without splitting or globbing */
char *expand_word_str(Word *w, int flags)
{
    XState x;
    StrBuf b;
    int i;
    char *r;
    if (!(flags & (EXP_NOSPLIT | EXP_PATTERN)))
        flags |= EXP_NOSPLIT;
    expand_one(w, flags, &x);
    if (flags & EXP_PATTERN) {
        /* join all pieces */
        XBuf all;
        memset(&all, 0, sizeof all);
        for (i = 0; i < x.nf; i++) {
            size_t k;
            if (i)
                xb_add(&all, ' ', F_Q);
            for (k = 0; k < x.fields[i].len; k++)
                xb_add(&all, (unsigned char)x.fields[i].c[k], x.fields[i].f[k]);
        }
        r = pattern_from_buf(&all);
        free(all.c);
        free(all.f);
        xs_free(&x);
        return r;
    }
    sb_init(&b);
    for (i = 0; i < x.nf; i++) {
        size_t k;
        if (i)
            sb_addc(&b, ' ');
        for (k = 0; k < x.fields[i].len; k++)
            if (!(x.fields[i].f[k] & F_MARK))
                sb_addc(&b, x.fields[i].c[k]);
    }
    xs_free(&x);
    return sb_stack(&b);
}

char *expand_heredoc(Word *w)
{
    return expand_word_str(w, EXP_NOSPLIT | EXP_HEREDOC);
}

/* PS1/PS2/PS4: parameter expansion (via the parser) and '!' expansion */
char *expand_prompt(const char *ps)
{
    StrBuf b;
    const char *p;
    char *r;
    struct jmploc jl, *saved = handler;
    StackMark m;
    char *encoded;
    sb_init(&b);
    /* exclamation-mark expansion first */
    for (p = ps; *p; p++) {
        if (*p == '!') {
            if (p[1] == '!') {
                sb_addc(&b, '!');
                p++;
            } else
                sb_printf(&b, "%d", history_next_number());
        } else
            sb_addc(&b, *p);
    }
    encoded = sb_stack(&b);
    setstackmark(&m);
    if (setjmp(jl.loc)) {
        handler = saved;
        popstackmark(&m);
        return ststrdup(ps);
    }
    handler = &jl;
    {
        extern Word *parse_prompt_word(const char *s);
        Word *w = parse_prompt_word(encoded);
        r = expand_word_str(w, EXP_NOSPLIT | EXP_HEREDOC);
    }
    handler = saved;
    {
        char *keep = xstrdup(r);
        popstackmark(&m);
        r = ststrdup(keep);
        free(keep);
    }
    return r;
}

char *expand_prompt_params(const char *s)
{
    Word *w = parse_prompt_word(s);
    return expand_word_str(w, EXP_NOSPLIT | EXP_HEREDOC);
}
