/*
 * var.c - macros: definition with the precedence of their sources, and
 * expansion, including $(name:old=new), pattern substitutions and the
 * internal macros with their D and F forms.
 */
#include "make.h"

#define VHASH 1024
static Var *vtab[VHASH];
int opt_e;
Auto *cur_auto;

static unsigned hash(const char *s)
{
    unsigned h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h % VHASH;
}

Var *var_get(const char *name)
{
    for (Var *v = vtab[hash(name)]; v; v = v->next)
        if (strcmp(v->name, name) == 0)
            return v;
    return NULL;
}

static Var *var_new(const char *name)
{
    unsigned h = hash(name);
    Var *v = xcalloc(1, sizeof *v);
    v->name = xstrdup(name);
    v->next = vtab[h];
    vtab[h] = v;
    return v;
}

/* may a definition from source 'src' replace one from v->source? */
static int may_override(Var *v, int src)
{
    if (!v)
        return 1;
    if (src == SRC_BUILTIN)
        return v->source == SRC_BUILTIN;
    if (src == SRC_FILE) {
        if (v->source == SRC_CMDLINE || v->source == SRC_MAKEFLAGS)
            return 0;
        if (v->source == SRC_ENV && opt_e)
            return 0;
        return 1;
    }
    /* command line, MAKEFLAGS, environment: not over a lower-numbered source */
    return v->source >= src;
}

int var_set(const char *name, const char *value, int delayed, int source)
{
    Var *v = var_get(name);
    if (!may_override(v, source))
        return 0;
    if (!v)
        v = var_new(name);
    free(v->value);
    v->value = xstrdup(value);
    v->delayed = delayed;
    v->source = source;
    if (source == SRC_ENV)
        v->from_env = 1;
    return 1;
}

void var_append(const char *name, const char *value, int source)
{
    Var *v = var_get(name);
    Buf b = { 0 };
    if (!v) {
        var_set(name, value, 1, source);
        return;
    }
    if (!may_override(v, source))
        return;
    buf_adds(&b, v->value);
    if (v->delayed) {
        if (b.len)
            buf_addc(&b, ' ');
        buf_adds(&b, value);
    } else {
        char *e = expand(value);
        if (b.len)
            buf_addc(&b, ' ');
        buf_adds(&b, e);
        free(e);
    }
    free(v->value);
    v->value = buf_take(&b);
    v->source = source;
}

void var_set_cond(const char *name, const char *value, int source)
{
    if (!var_get(name))
        var_set(name, value, 1, source);
}

void var_shell(const char *name, const char *cmd, int source)
{
    char *e = expand(cmd);
    Buf out = { 0 };
    int pfd[2];
    pid_t pid;
    char buf[4096];
    ssize_t n;
    fflush(stdout);
    if (pipe(pfd) < 0) {
        diag("pipe: %s", strerror(errno));
        free(e);
        return;
    }
    pid = fork();
    if (pid < 0) {
        diag("fork: %s", strerror(errno));
        close(pfd[0]);
        close(pfd[1]);
        free(e);
        return;
    }
    if (pid == 0) {
        char **env = child_environ(0);
        dup2(pfd[1], 1);
        close(pfd[0]);
        close(pfd[1]);
        char *sh = shell_path();
        execle(sh, sh, "-c", e, (char *)NULL, env);
        _exit(127);
    }
    close(pfd[1]);
    while ((n = read(pfd[0], buf, sizeof buf)) != 0) {
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        buf_add(&out, buf, (size_t)n);
    }
    close(pfd[0]);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
        ;
    {
        char *s = out.s ? out.s : "";
        size_t l;
        while (*s && isspace((unsigned char)*s))
            s++;
        l = strlen(s);
        if (l && s[l - 1] == '\n')
            l--;
        {
            char *v = xstrndup(s, l);
            for (char *p = v; *p; p++)
                if (*p == '\n')
                    *p = ' ';
            var_set(name, v, 1, source);
            free(v);
        }
    }
    buf_free(&out);
    free(e);
}

void var_foreach(void (*fn)(Var *v))
{
    for (int i = 0; i < VHASH; i++)
        for (Var *v = vtab[i]; v; v = v->next)
            fn(v);
}

/* ---------- expansion ---------- */

static void add_final(Buf *out, const char *s, int keep_dd)
{
    /* text that is not rescanned: under :::= its '$' must survive */
    if (!keep_dd) {
        buf_adds(out, s);
        return;
    }
    for (; *s; s++) {
        if (*s == '$')
            buf_addc(out, '$');
        buf_addc(out, *s);
    }
}

/* the directory or file part of each word of a list */
static void df_parts(Buf *out, const char *list, int which)
{
    Words w = { 0 };
    split_words(list, &w, 0);
    for (int i = 0; i < w.n; i++) {
        char *s = w.v[i], *sl = strrchr(s, '/');
        if (i)
            buf_addc(out, ' ');
        if (which == 'F')
            buf_adds(out, sl ? sl + 1 : s);
        else if (!sl)
            buf_addc(out, '.');
        else if (sl == s)
            buf_addc(out, '/');
        else
            buf_add(out, s, (size_t)(sl - s));
    }
    words_free(&w);
}

/* the value of an internal macro, or NULL if name is not one */
static int auto_value(const char *name, Buf *out)
{
    const char *v;
    int which = 0;
    if (!name[0] || (name[1] && (name[2] || (name[1] != 'D' && name[1] != 'F'))))
        return 0;
    switch (name[0]) {
    case '@':
        v = cur_auto ? cur_auto->at : NULL;
        break;
    case '%':
        v = cur_auto ? cur_auto->pct : NULL;
        break;
    case '<':
        v = cur_auto ? cur_auto->lt : NULL;
        break;
    case '*':
        v = cur_auto ? cur_auto->star : NULL;
        break;
    case '?':
        v = cur_auto ? cur_auto->q : NULL;
        break;
    case '^':
        v = cur_auto ? cur_auto->hat : NULL;
        break;
    case '+':
        v = cur_auto ? cur_auto->plus : NULL;
        break;
    default:
        return 0;
    }
    which = name[1];
    if (!v)
        v = "";
    if (which)
        df_parts(out, v, which);
    else
        buf_adds(out, v);
    return 1;
}

/* $(name:lhs=rhs) */
static void substitute(Buf *out, const char *val, const char *lhs, const char *rhs)
{
    Words w = { 0 };
    const char *pct = strchr(lhs, '%');
    size_t ll = strlen(lhs);
    split_words(val, &w, 0);
    for (int i = 0; i < w.n; i++) {
        const char *s = w.v[i];
        size_t sl = strlen(s);
        if (i)
            buf_addc(out, ' ');
        if (pct) {
            size_t pl = (size_t)(pct - lhs), xl = ll - pl - 1;
            if (sl >= pl + xl && strncmp(s, lhs, pl) == 0 && strcmp(s + sl - xl, pct + 1) == 0) {
                const char *rp = strchr(rhs, '%');
                if (rp) {
                    buf_add(out, rhs, (size_t)(rp - rhs));
                    buf_add(out, s + pl, sl - pl - xl);
                    buf_adds(out, rp + 1);
                } else
                    buf_adds(out, rhs);
            } else
                buf_adds(out, s);
        } else if (sl >= ll && strcmp(s + sl - ll, lhs) == 0) {
            buf_add(out, s, sl - ll);
            buf_adds(out, rhs);
        } else
            buf_adds(out, s);
    }
    words_free(&w);
}

static void expand_name(Buf *out, const char *name, const char *mod, int keep_dd)
{
    Buf val = { 0 };
    if (!auto_value(name, &val)) {
        Var *v = var_get(name);
        if (v && v->value) {
            if (v->delayed) {
                if (v->expanding)
                    fatal("Recursive variable '%s' references itself (eventually)", name);
                v->expanding = 1;
                expand_into(&val, v->value, keep_dd);
                v->expanding = 0;
            } else
                add_final(&val, v->value, keep_dd);
        }
    } else if (keep_dd) {
        char *t = buf_take(&val);
        add_final(&val, t, 1);
        free(t);
    }
    if (mod) {
        const char *eq = strchr(mod, '=');
        if (eq) {
            char *lhs = xstrndup(mod, (size_t)(eq - mod));
            if (val.s)
                substitute(out, val.s, lhs, eq + 1);
            free(lhs);
        }
    } else if (val.s)
        buf_adds(out, val.s);
    buf_free(&val);
}

void expand_into(Buf *out, const char *s, int keep_dd)
{
    while (*s) {
        const char *d = strchr(s, '$');
        if (!d) {
            buf_adds(out, s);
            return;
        }
        buf_add(out, s, (size_t)(d - s));
        s = d + 1;
        if (!*s)
            return;
        if (*s == '$') {
            buf_adds(out, keep_dd ? "$$" : "$");
            s++;
        } else if (*s == '(' || *s == '{') {
            char open = *s, close = open == '(' ? ')' : '}';
            const char *p = s + 1, *colon = NULL;
            int depth = 1;
            /* find the matching close and a top-level ':' */
            for (; *p; p++) {
                if (*p == '$' && (p[1] == '(' || p[1] == '{')) {
                    /* a nested reference of either kind */
                    char o2 = p[1], c2 = o2 == '(' ? ')' : '}';
                    int d2 = 1;
                    p += 2;
                    for (; *p && d2; p++) {
                        if (*p == o2)
                            d2++;
                        else if (*p == c2)
                            d2--;
                    }
                    p--;
                    if (!*p)
                        break;
                    continue;
                }
                if (*p == open)
                    depth++;
                else if (*p == close && --depth == 0)
                    break;
                else if (*p == ':' && depth == 1 && !colon)
                    colon = p;
            }
            if (!*p) {
                /* unterminated: take the rest literally */
                buf_addc(out, '$');
                buf_adds(out, s);
                return;
            }
            {
                char *rawname = xstrndup(s + 1, (size_t)((colon ? colon : p) - s - 1));
                Buf name = { 0 }, mod = { 0 };
                expand_into(&name, rawname, 0);
                if (colon) {
                    char *rawmod = xstrndup(colon + 1, (size_t)(p - colon - 1));
                    expand_into(&mod, rawmod, 0);
                    free(rawmod);
                    if (!mod.s)
                        buf_adds(&mod, "");
                    if (!strchr(mod.s, '=')) {
                        /* no '=': a macro named "name:mod" */
                        buf_addc(&name, ':');
                        buf_adds(&name, mod.s);
                        buf_free(&mod);
                    }
                }
                expand_name(out, name.s ? name.s : "", mod.s, keep_dd);
                buf_free(&name);
                buf_free(&mod);
                free(rawname);
            }
            s = p + 1;
        } else {
            char nm[2] = { *s, 0 };
            expand_name(out, nm, NULL, keep_dd);
            s++;
        }
    }
}

char *expand(const char *s)
{
    Buf b = { 0 };
    expand_into(&b, s, 0);
    return buf_take(&b);
}

char *expand_keep(const char *s)
{
    Buf b = { 0 };
    expand_into(&b, s, 1);
    return buf_take(&b);
}
