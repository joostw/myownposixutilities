/*
 * var.c - internal variables, aliases, alternates and the lists of
 * discarded and retained header fields.
 */
#include "mailx.h"

typedef struct Var {
    char *name, *value;
    struct Var *next;
} Var;
static Var *vars;

typedef struct Alias {
    char *name;
    Words addrs;
    struct Alias *next;
} Alias;
static Alias *aliases;

Words alternates, ignored, retained;

static const char *canon(const char *name)
{
    return strcmp(name, "ask") == 0 ? "asksub" : name;
}

static Var *find(const char *name)
{
    name = canon(name);
    for (Var *v = vars; v; v = v->next)
        if (strcmp(v->name, name) == 0)
            return v;
    return NULL;
}

const char *var_get(const char *name)
{
    Var *v = find(name);
    return v ? v->value : NULL;
}

int var_isset(const char *name)
{
    return find(name) != NULL;
}

void var_set(const char *name, const char *value)
{
    Var *v = find(name), **pp;
    name = canon(name);
    if (!v) {
        v = xcalloc(1, sizeof *v);
        v->name = xstrdup(name);
        /* keep the list sorted for set without arguments */
        for (pp = &vars; *pp && strcmp((*pp)->name, name) < 0; pp = &(*pp)->next)
            ;
        v->next = *pp;
        *pp = v;
    } else
        free(v->value);
    v->value = xstrdup(value ? value : "");
}

void var_unset(const char *name)
{
    Var **pp;
    name = canon(name);
    for (pp = &vars; *pp; pp = &(*pp)->next)
        if (strcmp((*pp)->name, name) == 0) {
            Var *v = *pp;
            *pp = v->next;
            free(v->name);
            free(v->value);
            free(v);
            return;
        }
}

void var_list(FILE *fp)
{
    for (Var *v = vars; v; v = v->next) {
        if (*v->value)
            fprintf(fp, "%s=\"%s\"\n", v->name, v->value);
        else
            fprintf(fp, "%s\n", v->name);
    }
}

int var_num(const char *name, int dflt)
{
    const char *v = var_get(name);
    char *end;
    long n;
    if (!v || !*v)
        return dflt;
    n = strtol(v, &end, 10);
    if (end == v)
        return dflt;
    return (int)n;
}

int escape_char(void)
{
    const char *e = var_get("escape");
    if (!e)
        return '~';
    if (!*e)
        return -1;
    return (unsigned char)*e;
}

/* ---------- aliases ---------- */

static Alias *alias_find(const char *name)
{
    for (Alias *a = aliases; a; a = a->next)
        if (strcmp(a->name, name) == 0)
            return a;
    return NULL;
}

void alias_add(const char *name, Words *addrs)
{
    Alias *a = alias_find(name);
    if (!a) {
        Alias **pp;
        a = xcalloc(1, sizeof *a);
        a->name = xstrdup(name);
        for (pp = &aliases; *pp && strcmp((*pp)->name, name) < 0; pp = &(*pp)->next)
            ;
        a->next = *pp;
        *pp = a;
    }
    for (int i = 0; i < addrs->n; i++)
        if (!words_has(&a->addrs, addrs->v[i], 0))
            words_add(&a->addrs, addrs->v[i]);
}

Words *alias_get(const char *name)
{
    Alias *a = alias_find(name);
    return a ? &a->addrs : NULL;
}

void alias_remove(const char *name)
{
    Alias **pp;
    for (pp = &aliases; *pp; pp = &(*pp)->next)
        if (strcmp((*pp)->name, name) == 0) {
            Alias *a = *pp;
            *pp = a->next;
            free(a->name);
            words_free(&a->addrs);
            free(a);
            return;
        }
}

static void alias_print(FILE *fp, Alias *a)
{
    fprintf(fp, "%s\t", a->name);
    for (int i = 0; i < a->addrs.n; i++)
        fprintf(fp, "%s%s", i ? " " : "", a->addrs.v[i]);
    fputc('\n', fp);
}

void alias_list(FILE *fp, const char *name)
{
    if (name) {
        Alias *a = alias_find(name);
        if (a)
            alias_print(fp, a);
        else
            err("%s: no such alias", name);
        return;
    }
    for (Alias *a = aliases; a; a = a->next)
        alias_print(fp, a);
}

/* ---------- header fields ---------- */

int header_shown(const char *field)
{
    if (retained.n)
        return words_has(&retained, field, 1);
    return !words_has(&ignored, field, 1);
}
