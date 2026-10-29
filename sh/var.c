/*
 * var.c - shell variables, positional parameters, aliases, functions.
 */
#include "sh.h"

#define VHASH 512
static Var *vtab[VHASH];

Params posparams;
char *arg_zero;
int exitstatus;
pid_t shell_pid;
pid_t last_bgpid;
int last_bgpid_set;

extern void getopts_reset(void);

static unsigned vhash(const char *s)
{
    unsigned h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h & (VHASH - 1);
}

Var *var_lookup(const char *name)
{
    Var *v;
    for (v = vtab[vhash(name)]; v; v = v->next)
        if (strcmp(v->name, name) == 0)
            return v;
    return NULL;
}

static Var *var_create(const char *name)
{
    unsigned h = vhash(name);
    Var *v = xcalloc(1, sizeof *v);
    v->name = xstrdup(name);
    v->next = vtab[h];
    vtab[h] = v;
    return v;
}

const char *var_get(const char *name)
{
    Var *v = var_lookup(name);
    return v ? v->val : NULL;
}

/* LANG and LC_* determine the locale (XBD 8.2 precedence) */
static void locale_update(void)
{
    static const struct { int cat; const char *name; } cats[] = {
        { LC_COLLATE, "LC_COLLATE" }, { LC_CTYPE, "LC_CTYPE" },
        { LC_MESSAGES, "LC_MESSAGES" }, { LC_NUMERIC, "LC_NUMERIC" },
        { LC_MONETARY, "LC_MONETARY" }, { LC_TIME, "LC_TIME" }
    };
    const char *all = var_get("LC_ALL"), *lang = var_get("LANG");
    size_t i;
    for (i = 0; i < sizeof cats / sizeof cats[0]; i++) {
        const char *v = all && *all ? all : NULL;
        if (!v) {
            const char *c = var_get(cats[i].name);
            v = c && *c ? c : (lang && *lang ? lang : "C");
        }
        if (!setlocale(cats[i].cat, v))
            setlocale(cats[i].cat, "C");
    }
}

int locale_init_done;

static void var_changed(const char *name)
{
    if (strcmp(name, "PATH") == 0)
        hash_clear();
    else if (strcmp(name, "OPTIND") == 0)
        getopts_reset();
    else if (locale_init_done && (strcmp(name, "LANG") == 0 || strncmp(name, "LC_", 3) == 0))
        locale_update();
}

int var_set(const char *name, const char *val, int flags)
{
    Var *v = var_lookup(name);
    if (v && (v->flags & V_READONLY))
        return -1;
    if (!v)
        v = var_create(name);
    if (val != v->val) {
        char *nv = val ? xstrdup(val) : NULL;
        free(v->val);
        v->val = nv;
    }
    v->flags |= flags;
    if (opts[O_ALLEXPORT])
        v->flags |= V_EXPORT;
    var_changed(name);
    return 0;
}

void var_set_or_die(const char *name, const char *val)
{
    if (var_set(name, val, 0) < 0)
        sh_error("%s: is read only", name);
}

void var_set_int(const char *name, long v)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%ld", v);
    var_set(name, buf, 0);
}

int var_unset(const char *name)
{
    Var *v = var_lookup(name);
    if (!v)
        return 0;
    if (v->flags & V_READONLY)
        return -1;
    free(v->val);
    v->val = NULL;
    v->flags = 0;
    var_changed(name);
    return 0;
}

void var_export(const char *name)
{
    Var *v = var_lookup(name);
    if (!v)
        v = var_create(name);
    v->flags |= V_EXPORT;
}

const char *ifs_value(void)
{
    const char *s = var_get("IFS");
    return s ? s : " \t\n";
}

int ifs_isset(void) { return var_get("IFS") != NULL; }

char **var_environ(void)
{
    int n = 0, i, k = 0;
    Var *v;
    char **env;
    for (i = 0; i < VHASH; i++)
        for (v = vtab[i]; v; v = v->next)
            if ((v->flags & V_EXPORT) && v->val)
                n++;
    env = xmalloc((size_t)(n + 1) * sizeof *env);
    for (i = 0; i < VHASH; i++)
        for (v = vtab[i]; v; v = v->next)
            if ((v->flags & V_EXPORT) && v->val) {
                size_t a = strlen(v->name), b = strlen(v->val);
                char *e = xmalloc(a + b + 2);
                memcpy(e, v->name, a);
                e[a] = '=';
                memcpy(e + a + 1, v->val, b + 1);
                env[k++] = e;
            }
    env[k] = NULL;
    return env;
}

static int var_cmp(const void *a, const void *b)
{
    return strcoll((*(Var *const *)a)->name, (*(Var *const *)b)->name);
}

/* list variables with all bits of flagmask set (0: all set variables) */
void var_list(Out *o, int flagmask, const char *prefix)
{
    int n = 0, i, k = 0;
    Var *v, **arr;
    StackMark m;
    for (i = 0; i < VHASH; i++)
        for (v = vtab[i]; v; v = v->next)
            if ((v->flags & flagmask) == flagmask && (flagmask || v->val))
                n++;
    arr = xmalloc((size_t)(n + 1) * sizeof *arr);
    for (i = 0; i < VHASH; i++)
        for (v = vtab[i]; v; v = v->next)
            if ((v->flags & flagmask) == flagmask && (flagmask || v->val))
                arr[k++] = v;
    qsort(arr, (size_t)n, sizeof *arr, var_cmp);
    setstackmark(&m);
    for (i = 0; i < n; i++) {
        v = arr[i];
        if (prefix)
            outfmt(o, "%s ", prefix);
        if (v->val)
            outfmt(o, "%s=%s\n", v->name, quote_for_reinput(v->val));
        else
            outfmt(o, "%s\n", v->name);
    }
    popstackmark(&m);
    free(arr);
}

VarSave *var_save(const char *name)
{
    VarSave *s = xcalloc(1, sizeof *s);
    Var *v = var_lookup(name);
    s->name = xstrdup(name);
    if (v) {
        s->existed = 1;
        s->val = v->val ? xstrdup(v->val) : NULL;
        s->flags = v->flags;
    }
    return s;
}

void var_restore(VarSave *s)
{
    while (s) {
        VarSave *n = s->next;
        Var *v = var_lookup(s->name);
        if (!v)
            v = var_create(s->name);
        if (!(v->flags & V_READONLY) || s->existed) {
            free(v->val);
            v->val = s->val;
            s->val = NULL;
            v->flags = s->existed ? s->flags : 0;
            var_changed(s->name);
        }
        free(s->val);
        free(s->name);
        free(s);
        s = n;
    }
}

void var_init(char **envp)
{
    char **e;
    for (e = envp; e && *e; e++) {
        char *eq = strchr(*e, '=');
        char *name;
        if (!eq)
            continue;
        name = xstrndup(*e, (size_t)(eq - *e));
        if (is_name(name))
            var_set(name, eq + 1, V_EXPORT);
        free(name);
    }
    var_set("IFS", " \t\n", 0);
    if (!var_get("PS1"))
        var_set("PS1", geteuid() == 0 ? "# " : "$ ", 0);
    if (!var_get("PS2"))
        var_set("PS2", "> ", 0);
    if (!var_get("PS4"))
        var_set("PS4", "+ ", 0);
    var_set("OPTIND", "1", 0);
    var_set_int("PPID", (long)getppid());
    locale_init_done = 1;
}

/* ------------------------------------------------------------------ */
/* positional parameters                                              */
/* ------------------------------------------------------------------ */

void params_free(Params *p)
{
    int i;
    if (p->malloced) {
        for (i = 0; i < p->n; i++)
            free(p->v[i]);
        free(p->v);
    }
    p->v = NULL;
    p->n = 0;
    p->malloced = 0;
}

void params_set(char **argv, int n)
{
    Params np;
    int i;
    np.n = n;
    np.v = xmalloc((size_t)(n + 1) * sizeof(char *));
    for (i = 0; i < n; i++)
        np.v[i] = xstrdup(argv[i]);
    np.v[n] = NULL;
    np.malloced = 1;
    params_free(&posparams);
    posparams = np;
}

/* ------------------------------------------------------------------ */
/* aliases                                                            */
/* ------------------------------------------------------------------ */

static Alias *aliases;

Alias *alias_lookup(const char *name)
{
    Alias *a;
    for (a = aliases; a; a = a->next)
        if (strcmp(a->name, name) == 0)
            return a;
    return NULL;
}

void alias_set(const char *name, const char *val)
{
    Alias *a = alias_lookup(name);
    if (a) {
        if (a->inuse) {
            /* keep the old text alive while it is being read */
            a->name = xstrdup(name);
        } else
            free(a->val);
        a->val = xstrdup(val);
        return;
    }
    a = xcalloc(1, sizeof *a);
    a->name = xstrdup(name);
    a->val = xstrdup(val);
    /* keep insertion order for listing */
    if (!aliases)
        aliases = a;
    else {
        Alias *p = aliases;
        while (p->next) p = p->next;
        p->next = a;
    }
}

int alias_unset(const char *name)
{
    Alias **pp, *a;
    for (pp = &aliases; (a = *pp); pp = &a->next)
        if (strcmp(a->name, name) == 0) {
            *pp = a->next;
            if (!a->inuse) {
                free(a->name);
                free(a->val);
                free(a);
            }
            return 0;
        }
    return -1;
}

void alias_unset_all(void)
{
    while (aliases)
        alias_unset(aliases->name);
}

void alias_print(Alias *a)
{
    StackMark m;
    setstackmark(&m);
    outfmt(&out1, "%s=%s\n", a->name, quote_for_reinput(a->val));
    popstackmark(&m);
}

static int alias_cmp(const void *x, const void *y)
{
    return strcoll((*(Alias *const *)x)->name, (*(Alias *const *)y)->name);
}

void alias_print_all(void)
{
    int n = 0, i;
    Alias *a, **arr;
    for (a = aliases; a; a = a->next) n++;
    arr = xmalloc((size_t)(n + 1) * sizeof *arr);
    for (a = aliases, i = 0; a; a = a->next) arr[i++] = a;
    qsort(arr, (size_t)n, sizeof *arr, alias_cmp);
    for (i = 0; i < n; i++)
        alias_print(arr[i]);
    free(arr);
}

/* ------------------------------------------------------------------ */
/* functions                                                          */
/* ------------------------------------------------------------------ */

static Func *functab[128];

static unsigned fhash(const char *s) { return vhash(s) & 127; }

Func *func_lookup(const char *name)
{
    Func *f;
    for (f = functab[fhash(name)]; f; f = f->next)
        if (strcmp(f->name, name) == 0)
            return f;
    return NULL;
}

void func_define(const char *name, Node *body)
{
    Func *f = func_lookup(name);
    Node *copy = copy_tree(body);
    if (f) {
        /* the old body may still be executing; leak it rather than free */
        f->body = copy;
        return;
    }
    f = xcalloc(1, sizeof *f);
    f->name = xstrdup(name);
    f->body = copy;
    f->next = functab[fhash(name)];
    functab[fhash(name)] = f;
}

int func_unset(const char *name)
{
    Func **pp, *f;
    for (pp = &functab[fhash(name)]; (f = *pp); pp = &f->next)
        if (strcmp(f->name, name) == 0) {
            *pp = f->next;
            free(f->name);
            free(f);
            return 0;
        }
    return -1;
}

void func_clear_all(void)
{
    int i;
    for (i = 0; i < 128; i++)
        functab[i] = NULL;
}

/* used by env re-initialization for scripts run via ENOEXEC */
void var_clear_nonexported(void)
{
    int i;
    Var *v;
    for (i = 0; i < VHASH; i++)
        for (v = vtab[i]; v; v = v->next)
            if (!(v->flags & V_EXPORT)) {
                free(v->val);
                v->val = NULL;
                v->flags = 0;
            } else
                v->flags &= ~V_READONLY;
}
