/*
 * astdump.c - testing only: print the parsed program, so that two
 * builds of the parser can be compared tree for tree.
 *
 * Link it into a build whose main.c is compiled with
 * -Dparse_program=dump_parse_program; with AWK_AST_DUMP set in the
 * environment, the program is parsed, printed and not run.
 */
#include "../awk.h"

static void str(const Str *s)
{
    size_t i;
    if (!s) {
        fputs("-", stdout);
        return;
    }
    putchar('"');
    for (i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->s[i];
        if (c == '"' || c == '\\')
            printf("\\%c", c);
        else if (c < 32 || c >= 127)
            printf("\\%03o", c);
        else
            putchar(c);
    }
    putchar('"');
}

static void node(const Node *n, int depth)
{
    for (; n; n = n->next) {
        printf("%*s(%d op=%d idx=%d nargs=%d num=%.17g str=", depth * 2, "", n->type, n->op,
               n->idx, n->nargs, n->num);
        str(n->str);
        printf(" re=%d", n->re != NULL);
        if (n->type == N_GVAR && n->idx >= 0 && n->idx < ngvars)
            printf(" g=%s", gnames[n->idx]);
        if (n->type == N_CALL && n->idx >= 0 && n->idx < nfuncs)
            printf(" f=%s", funcs[n->idx]->name);
        putchar('\n');
        if (n->a) { printf("%*sa:\n", depth * 2 + 1, ""); node(n->a, depth + 1); }
        if (n->b) { printf("%*sb:\n", depth * 2 + 1, ""); node(n->b, depth + 1); }
        if (n->c) { printf("%*sc:\n", depth * 2 + 1, ""); node(n->c, depth + 1); }
        if (n->d) { printf("%*sd:\n", depth * 2 + 1, ""); node(n->d, depth + 1); }
        printf("%*s)\n", depth * 2, "");
    }
}

void dump_parse_program(const char *src, size_t len)
{
    int i, j;
    parse_program(src, len);
    if (!getenv("AWK_AST_DUMP"))
        return;
    printf("globals %d\n", ngvars);
    for (i = 0; i < ngvars; i++)
        printf("  %d %s\n", i, gnames[i]);
    printf("functions %d\n", nfuncs);
    for (i = 0; i < nfuncs; i++) {
        Func *f = funcs[i];
        printf("function %s defined=%d nparams=%d", f->name, f->defined, f->nparams);
        for (j = 0; j < f->nparams; j++)
            printf(" %s", f->params[j]);
        putchar('\n');
        node(f->body, 1);
    }
    printf("begin %d\n", nbegin);
    for (i = 0; i < nbegin; i++)
        node(begin_actions[i], 1);
    printf("end %d\n", nend);
    for (i = 0; i < nend; i++)
        node(end_actions[i], 1);
    printf("rules %d\n", nrules);
    for (i = 0; i < nrules; i++) {
        printf("rule %d\n pat1:\n", i);
        node(rules[i].pat1, 1);
        printf(" pat2:\n");
        node(rules[i].pat2, 1);
        printf(" action:\n");
        node(rules[i].action, 1);
    }
    fflush(stdout);
    exit(0);
}
