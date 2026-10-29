/*
 * parse.c - the symbol tables, the keywords, and the program's parts.
 * The program is read by the scanner in awk.l and the grammar in awk.y.
 */
#include "awk.h"
#include "parse.h"

/* ------------------------------------------------------------------ */
/* symbol tables                                                      */
/* ------------------------------------------------------------------ */

Val **gvars;
char **gnames;
int ngvars;
static int gvcap;
Func **funcs;
int nfuncs;
static int fcap;

Node **begin_actions, **end_actions;
int nbegin, nend;
static int begincap, endcap;
Rule *rules;
int nrules;
static int rulecap;

#define SYMHASH 1024
typedef struct Sym { char *name; int kind; int idx; struct Sym *next; } Sym;
enum { SYM_VAR, SYM_FUNC };
static Sym *symtab[SYMHASH];

static Sym *sym_find(const char *name, int kind)
{
    Sym *s;
    for (s = symtab[str_hash(name, strlen(name)) & (SYMHASH - 1)]; s; s = s->next)
        if (s->kind == kind && strcmp(s->name, name) == 0)
            return s;
    return NULL;
}

static Sym *sym_add(const char *name, int kind, int idx)
{
    unsigned long h = str_hash(name, strlen(name)) & (SYMHASH - 1);
    Sym *s = xmalloc(sizeof *s);
    s->name = xstrdup(name);
    s->kind = kind;
    s->idx = idx;
    s->next = symtab[h];
    symtab[h] = s;
    return s;
}

int global_lookup(const char *name, int create)
{
    Sym *s = sym_find(name, SYM_VAR);
    if (s)
        return s->idx;
    if (!create)
        return -1;
    if (ngvars == gvcap) {
        gvcap = gvcap ? gvcap * 2 : 64;
        gvars = xrealloc(gvars, (size_t)gvcap * sizeof *gvars);
        gnames = xrealloc(gnames, (size_t)gvcap * sizeof *gnames);
    }
    gvars[ngvars] = xmalloc(sizeof(Val));
    *gvars[ngvars] = val_uninit();
    gnames[ngvars] = xstrdup(name);
    sym_add(name, SYM_VAR, ngvars);
    return ngvars++;
}

int func_lookup(const char *name)
{
    Sym *s = sym_find(name, SYM_FUNC);
    Func *f;
    if (s)
        return s->idx;
    if (nfuncs == fcap) {
        fcap = fcap ? fcap * 2 : 16;
        funcs = xrealloc(funcs, (size_t)fcap * sizeof *funcs);
    }
    f = xcalloc(1, sizeof *f);
    f->name = xstrdup(name);
    funcs[nfuncs] = f;
    sym_add(name, SYM_FUNC, nfuncs);
    return nfuncs++;
}

/* ------------------------------------------------------------------ */
/* keywords and built-in functions                                    */
/* ------------------------------------------------------------------ */

static const struct { const char *w; int t; } keywords[] = {
    {"BEGIN", TK_BEGIN}, {"END", TK_END}, {"break", TK_BREAK},
    {"continue", TK_CONTINUE}, {"delete", TK_DELETE}, {"do", TK_DO},
    {"else", TK_ELSE}, {"exit", TK_EXIT}, {"for", TK_FOR},
    {"function", TK_FUNCTION}, {"getline", TK_GETLINE}, {"if", TK_IF},
    {"in", TK_IN}, {"next", TK_NEXT}, {"nextfile", TK_NEXTFILE},
    {"print", TK_PRINT}, {"printf", TK_PRINTF}, {"return", TK_RETURN},
    {"while", TK_WHILE}, {NULL, 0}
};

static const struct { const char *w; int b; } builtins[] = {
    {"atan2", B_ATAN2}, {"close", B_CLOSE}, {"cos", B_COS}, {"exp", B_EXP},
    {"fflush", B_FFLUSH}, {"gsub", B_GSUB}, {"index", B_INDEX},
    {"int", B_INT}, {"length", B_LENGTH}, {"log", B_LOG},
    {"match", B_MATCH}, {"rand", B_RAND}, {"sin", B_SIN},
    {"split", B_SPLIT}, {"sprintf", B_SPRINTF}, {"sqrt", B_SQRT},
    {"srand", B_SRAND}, {"sub", B_SUB}, {"substr", B_SUBSTR},
    {"system", B_SYSTEM}, {"tolower", B_TOLOWER}, {"toupper", B_TOUPPER},
    {NULL, 0}
};

int is_reserved_word(const char *name)
{
    int i;
    for (i = 0; keywords[i].w; i++)
        if (strcmp(keywords[i].w, name) == 0)
            return 1;
    for (i = 0; builtins[i].w; i++)
        if (strcmp(builtins[i].w, name) == 0)
            return 1;
    return 0;
}

/* the token for a keyword or built-in function name, or 0 */
int word_token(const char *w, int *bi)
{
    int i;
    for (i = 0; keywords[i].w; i++)
        if (strcmp(keywords[i].w, w) == 0)
            return keywords[i].t;
    for (i = 0; builtins[i].w; i++)
        if (strcmp(builtins[i].w, w) == 0) {
            *bi = builtins[i].b;
            return TK_BUILTIN;
        }
    return 0;
}

/* ------------------------------------------------------------------ */
/* the program's parts, in the order they appear                      */
/* ------------------------------------------------------------------ */

void add_rule(Node *p1, Node *p2, Node *act)
{
    if (nrules == rulecap) {
        rulecap = rulecap ? rulecap * 2 : 16;
        rules = xrealloc(rules, (size_t)rulecap * sizeof *rules);
    }
    rules[nrules].pat1 = p1;
    rules[nrules].pat2 = p2;
    rules[nrules].action = act;
    rules[nrules].inrange = 0;
    nrules++;
}

void add_begin(Node *act)
{
    if (nbegin == begincap) {
        begincap = begincap ? begincap * 2 : 8;
        begin_actions = xrealloc(begin_actions, (size_t)begincap * sizeof(Node *));
    }
    begin_actions[nbegin++] = act;
}

void add_end(Node *act)
{
    if (nend == endcap) {
        endcap = endcap ? endcap * 2 : 8;
        end_actions = xrealloc(end_actions, (size_t)endcap * sizeof(Node *));
    }
    end_actions[nend++] = act;
}

void parse_program(const char *text, size_t len)
{
    int i;
    lex_begin(text, len);
    lex_reset();
    parser_reset();
    if (yyparse() != 0)
        syntax_error_at(lex_la, "syntax error");     /* not reached: the error rules report */
    for (i = 0; i < nfuncs; i++) {
        if (!funcs[i]->defined) {
            fprintf(stderr, "%s: calling undefined function %s\n", progname, funcs[i]->name);
            exit(2);
        }
        if (sym_find(funcs[i]->name, SYM_VAR)) {
            fprintf(stderr, "%s: function name %s used as a variable\n", progname,
                    funcs[i]->name);
            exit(2);
        }
    }
}
