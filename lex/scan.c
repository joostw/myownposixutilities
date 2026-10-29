/*
 * scan.c - reading the lex source: the definitions section (code,
 * substitutions, start conditions, %array/%pointer, table sizes), the
 * rules (patterns and actions) and the user subroutines.
 */
#include "lex.h"

Code *defcode;
int ndefcode;
Code *precode;
int nprecode;
Code *midcode;
int nmidcode;
int *midcode_after;
char *subroutines;
int subroutines_line;
int yytext_array = 0;
int opt_noyywrap;
int table_sizes_given;
int any_reject, any_yymore;

static const char *src, *src_end;
static int lineno;

static void add_code(Code **v, int *n, const char *text, size_t len, int line)
{
    *v = xrealloc(*v, (size_t)(*n + 1) * sizeof **v);
    (*v)[*n].text = xstrndup(text, len);
    (*v)[*n].line = line;
    (*n)++;
}

/* the current line: [*ls, *le) without the newline; returns 0 at the end */
static int get_line(const char **ls, const char **le, const char *p)
{
    if (p >= src_end)
        return 0;
    *ls = p;
    while (p < src_end && *p != '\n')
        p++;
    *le = p;
    return 1;
}

static const char *next_line(const char *le)
{
    lineno++;
    return le < src_end ? le + 1 : le;
}

/* is the line exactly s, with optional trailing blanks? */
static int line_is(const char *ls, const char *le, const char *s)
{
    size_t n = strlen(s);
    if ((size_t)(le - ls) < n || strncmp(ls, s, n))
        return 0;
    for (ls += n; ls < le; ls++)
        if (*ls != ' ' && *ls != '\t' && *ls != '\r')
            return 0;
    return 1;
}

static int blank(int c)
{
    return c == ' ' || c == '\t';
}

/* lines up to one that is only "%}"; p is after the "%{" line */
static const char *copy_block(const char *p, Code **v, int *n, int line)
{
    const char *start = p, *ls, *le;
    int l = lineno;
    while (get_line(&ls, &le, p)) {
        if (line_is(ls, le, "%}")) {
            add_code(v, n, start, (size_t)(ls - start), line + 1);
            return next_line(le);
        }
        p = next_line(le);
    }
    error_at(l, "unterminated %%{ block");
    add_code(v, n, start, (size_t)(src_end - start), line + 1);
    return src_end;
}

static void add_sconds(const char *p, const char *e, int exclusive, int line)
{
    int any = 0;
    for (;;) {
        const char *s;
        while (p < e && blank(*p))
            p++;
        if (p >= e)
            break;
        s = p;
        while (p < e && !blank(*p))
            p++;
        any = 1;
        if (!(isalpha((unsigned char)*s) || *s == '_')) {
            error_at(line, "bad start condition name %.*s", (int)(p - s), s);
            continue;
        }
        for (const char *q = s; q < p; q++)
            if (!(isalnum((unsigned char)*q) || *q == '_' || *q == '-')) {
                error_at(line, "bad start condition name %.*s", (int)(p - s), s);
                goto next;
            }
        if (find_scond(s, (size_t)(p - s)) >= 0) {
            error_at(line, "start condition %.*s declared twice", (int)(p - s), s);
            continue;
        }
        sconds = xrealloc(sconds, (size_t)(nsconds + 1) * sizeof *sconds);
        sconds[nsconds].name = xstrndup(s, (size_t)(p - s));
        sconds[nsconds].exclusive = exclusive;
        nsconds++;
    next:;
    }
    if (!any)
        warning_at(line, "no start condition names after %%%c", exclusive ? 'x' : 's');
}

static void option(const char *p, const char *e, int line)
{
    for (;;) {
        const char *s;
        size_t n;
        while (p < e && blank(*p))
            p++;
        if (p >= e)
            return;
        s = p;
        while (p < e && !blank(*p))
            p++;
        n = (size_t)(p - s);
#define IS(w) (n == strlen(w) && !strncmp(s, w, n))
        if (IS("noyywrap"))
            opt_noyywrap = 1;
        else if (IS("yywrap"))
            opt_noyywrap = 0;
        else if (IS("array"))
            yytext_array = 1;
        else if (IS("pointer"))
            yytext_array = 0;
        else if (IS("yylineno") || IS("noinput") || IS("nounput") || IS("noyymore") || IS("yymore") ||
                 IS("noyy_top_state") || IS("posix") || IS("lex-compat") || IS("8bit") || IS("default") ||
                 IS("nodefault") || IS("never-interactive") || IS("always-interactive") || IS("interactive") ||
                 IS("batch") || IS("warn"))
            ;
        else
            warning_at(line, "%%option %.*s is not supported", (int)n, s);
#undef IS
    }
}

static void definitions(const char **pp)
{
    const char *p = *pp, *ls, *le;
    while (get_line(&ls, &le, p)) {
        int line = lineno;
        if (line_is(ls, le, "%%")) {
            *pp = next_line(le);
            return;
        }
        if (ls == le || line_is(ls, le, "")) {
            p = next_line(le);
            continue;
        }
        if (blank(*ls)) {
            add_code(&defcode, &ndefcode, ls, (size_t)(le - ls + (le < src_end)), line);
            p = next_line(le);
            continue;
        }
        if (line_is(ls, le, "%{")) {
            p = copy_block(next_line(le), &defcode, &ndefcode, line);
            continue;
        }
        if (le - ls >= 2 && ls[0] == '/' && ls[1] == '*') {
            /* a comment at the start of a line: copied as code */
            const char *e = ls + 2;
            int l = line;
            while (e + 1 < src_end && !(e[0] == '*' && e[1] == '/'))
                e++;
            if (e + 1 >= src_end) {
                error_at(l, "unterminated comment");
                *pp = src_end;
                return;
            }
            e += 2;
            while (e < src_end && *e != '\n')
                e++;
            add_code(&defcode, &ndefcode, ls, (size_t)(e - ls + (e < src_end)), line);
            for (const char *q = ls; q < e; q++)
                if (*q == '\n')
                    lineno++;
            p = next_line(e);
            continue;
        }
        if (*ls == '%') {
            const char *w = ls + 1, *we = w;
            size_t n;
            while (we < le && isalnum((unsigned char)*we))
                we++;
            n = (size_t)(we - w);
            if (n == 0)
                error_at(line, "unknown directive %.*s", (int)(le - ls), ls);
            else if (n == 5 && !strncmp(w, "array", 5))
                yytext_array = 1;
            else if (n == 7 && !strncmp(w, "pointer", 7))
                yytext_array = 0;
            else if (n == 6 && !strncmp(w, "option", 6))
                option(we, le, line);
            else if (*w == 's' || *w == 'S')
                add_sconds(we, le, 0, line);
            else if (*w == 'x' || *w == 'X')
                add_sconds(we, le, 1, line);
            else if (strchr("pnaekoPNAEKO", *w) && n == 1) {
                const char *q = we;
                while (q < le && blank(*q))
                    q++;
                if (q == we || q >= le || !isdigit((unsigned char)*q))
                    error_at(line, "%%%c must be followed by a number", *w);
                else
                    table_sizes_given = 1;
            } else
                error_at(line, "unknown directive %%%.*s", (int)n, w);
            p = next_line(le);
            continue;
        }
        /* name substitute */
        {
            const char *n = ls, *ne = ls, *s, *se = le;
            while (ne < le && (isalnum((unsigned char)*ne) || *ne == '_' || *ne == '-'))
                ne++;
            if (ne == n || !(isalpha((unsigned char)*n) || *n == '_') || (ne < le && !blank(*ne))) {
                error_at(line, "bad definition (a name and its substitute expected): %.*s", (int)(le - ls), ls);
            } else {
                s = ne;
                while (s < le && blank(*s))
                    s++;
                while (se > s && (blank(se[-1]) || se[-1] == '\r'))
                    se--;
                if (s == se)
                    error_at(line, "definition %.*s has no substitute", (int)(ne - n), n);
                else {
                    char *name = xstrndup(n, (size_t)(ne - n)), *sub = xstrndup(s, (size_t)(se - s));
                    define(name, sub, line);
                    free(name);
                    free(sub);
                }
            }
            p = next_line(le);
        }
    }
    *pp = src_end;
    error_at(lineno, "end of input in the definitions section (missing %%%%)");
}

/* does the action use the word w (outside strings and comments, roughly)? */
static int uses_word(const char *a, const char *w)
{
    size_t n = strlen(w);
    for (const char *q = a; (q = strstr(q, w)); q++)
        if ((q == a || !(isalnum((unsigned char)q[-1]) || q[-1] == '_')) &&
            !(isalnum((unsigned char)q[n]) || q[n] == '_'))
            return 1;
    return 0;
}

/*
 * An action: the rest of the line, continued on later lines while
 * braces are open or a comment is unfinished.  p is at its first
 * character; returns the end (at a newline or the end of input).
 */
static const char *action_end(const char *p, int line)
{
    int depth = 0;
    while (p < src_end) {
        char c = *p;
        if (c == '\n') {
            if (depth <= 0)
                return p;
            lineno++;
            p++;
            continue;
        }
        if (c == '"' || c == '\'') {
            char q = c;
            p++;
            while (p < src_end && *p != q && *p != '\n') {
                if (*p == '\\' && p + 1 < src_end) {
                    if (p[1] == '\n')
                        lineno++;
                    p++;
                }
                p++;
            }
            if (p < src_end && *p == q)
                p++;
            continue;
        }
        if (c == '/' && p + 1 < src_end && p[1] == '*') {
            p += 2;
            while (p + 1 < src_end && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\n')
                    lineno++;
                p++;
            }
            if (p + 1 >= src_end) {
                error_at(line, "unterminated comment in action");
                return src_end;
            }
            p += 2;
            continue;
        }
        if (c == '/' && p + 1 < src_end && p[1] == '/') {
            while (p < src_end && *p != '\n')
                p++;
            continue;
        }
        if (c == '{')
            depth++;
        else if (c == '}')
            depth--;
        p++;
    }
    if (depth > 0)
        error_at(line, "unterminated action (unbalanced braces)");
    return src_end;
}

static void rules_section(const char **pp)
{
    const char *p = *pp, *ls, *le;
    int crules = 0;
    while (get_line(&ls, &le, p)) {
        int line = lineno;
        if (line_is(ls, le, "%%")) {
            p = next_line(le);
            subroutines = xstrndup(p, (size_t)(src_end - p));
            subroutines_line = lineno;
            *pp = src_end;
            return;
        }
        if (ls == le || line_is(ls, le, "")) {
            p = next_line(le);
            continue;
        }
        if (blank(*ls) || line_is(ls, le, "%{")) {
            Code **v = nrules ? &midcode : &precode;
            int *n = nrules ? &nmidcode : &nprecode;
            int before = *n;
            if (blank(*ls)) {
                /* an indented action-like block may span lines with braces */
                const char *e = action_end(ls, line);
                add_code(v, n, ls, (size_t)(e - ls + (e < src_end)), line);
                p = next_line(e);
            } else
                p = copy_block(next_line(le), v, n, line);
            if (nrules) {
                midcode_after = xrealloc(midcode_after, (size_t)nmidcode * sizeof(int));
                for (int i = before; i < nmidcode; i++)
                    midcode_after[i] = nrules - 1;
            }
            continue;
        }
        /* a rule */
        {
            Rule r;
            const char *q = ls;
            memset(&r, 0, sizeof r);
            if (!parse_rule_pattern(&q, &r, line)) {
                const char *e = action_end(ls, line);
                p = next_line(e);
                continue;
            }
            while (q < le && blank(*q))
                q++;
            if (q >= le || *q == '\r') {
                warning_at(line, "rule without an action; the matched text is discarded");
                r.action = xstrdup(";");
                r.action_line = line;
                p = next_line(le);
            } else if (*q == '|' && line_is(q + 1, le, "")) {
                r.action = NULL;
                p = next_line(le);
            } else {
                const char *e = action_end(q, line);
                r.action = xstrndup(q, (size_t)(e - q));
                r.action_line = line;
                if (uses_word(r.action, "REJECT"))
                    r.uses_reject = any_reject = 1;
                if (uses_word(r.action, "yymore"))
                    any_yymore = 1;
                p = next_line(e);
            }
            if (nrules == crules) {
                crules = crules ? crules * 2 : 64;
                rules = xrealloc(rules, (size_t)crules * sizeof *rules);
            }
            rules[nrules++] = r;
        }
    }
    *pp = src_end;
}

void read_source(const char *text, size_t len)
{
    const char *p = text;
    src = text;
    src_end = text + len;
    lineno = 1;
    sconds = xcalloc(1, sizeof *sconds);
    sconds[0].name = xstrdup("INITIAL");
    sconds[0].exclusive = 0;
    nsconds = 1;
    definitions(&p);
    if (p < src_end || nerrors == 0)
        rules_section(&p);
    /* "|" on the last rule has no next action */
    if (nrules && !rules[nrules - 1].action) {
        error_at(rules[nrules - 1].line, "the last rule's action is |");
        rules[nrules - 1].action = xstrdup(";");
    }
}
