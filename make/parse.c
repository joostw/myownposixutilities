/*
 * parse.c - reading makefiles: logical lines, comments, macro
 * definitions, include lines, target rules, inference rules and the
 * special targets.
 */
#include "make.h"

Words suffixes;
Cmds *default_cmds, *sccs_get_cmds;
int all_silent, all_ignore, all_precious, not_parallel, posix_mode;
Node *first_target;
Pattern *patterns;

typedef struct Src {
    FILE *fp;
    const char *text;     /* or a string */
    size_t tpos;
    const char *name;
    int line;
    int depth;
} Src;

/* the rule that command lines are added to */
static struct {
    int active;
    Node **targets;
    int ntargets;
    char *infer;          /* inference rule name */
    Pattern **pats;
    int npats;
    int special;          /* 1 .DEFAULT, 2 .SCCS_GET, 3 other specials (no commands) */
    Cmds *cmds;
    int dcolon;
    DRule **drules;
} cur;

static int include_depth;

int is_special_name(const char *s)
{
    if (s[0] != '.' || !isupper((unsigned char)s[1]))
        return 0;
    for (s++; *s; s++)
        if (!isupper((unsigned char)*s) && *s != '_')
            return 0;
    return 1;
}

static int is_suffix(const char *s, size_t n)
{
    for (int i = 0; i < suffixes.n; i++)
        if (strlen(suffixes.v[i]) == n && strncmp(suffixes.v[i], s, n) == 0)
            return 1;
    return 0;
}

int is_inference_name(const char *s)
{
    size_t l = strlen(s);
    if (s[0] != '.' || strchr(s, '/') || strchr(s, '%'))
        return 0;
    if (is_suffix(s, l))
        return 1;
    for (size_t i = 1; i < l; i++)
        if (s[i] == '.' && is_suffix(s, i) && is_suffix(s + i, l - i))
            return 1;
    return 0;
}

/* ---------- reading physical lines ---------- */

static char *read_phys(Src *src)
{
    Buf b = { 0 };
    int c;
    if (src->fp) {
        while ((c = getc(src->fp)) != EOF) {
            if (c == '\n')
                break;
            if (c)
                buf_addc(&b, c);
        }
        if (c == EOF && !b.s)
            return NULL;
    } else {
        if (!src->text[src->tpos])
            return NULL;
        while (src->text[src->tpos] && src->text[src->tpos] != '\n')
            buf_addc(&b, src->text[src->tpos++]);
        if (src->text[src->tpos] == '\n')
            src->tpos++;
    }
    src->line++;
    return buf_take(&b);
}

static int ends_escaped(const char *s)
{
    size_t l = strlen(s), k = 0;
    while (k < l && s[l - 1 - k] == '\\')
        k++;
    return k % 2 == 1;
}

/* ---------- finishing a rule ---------- */

static Cmds *new_cmds(const char *file, int line)
{
    Cmds *c = xcalloc(1, sizeof *c);
    c->file = xstrdup(file);
    c->line = line;
    return c;
}

static void cmds_add(Cmds *c, const char *s)
{
    c->v = xrealloc(c->v, (size_t)(c->n + 1) * sizeof(char *));
    c->v[c->n++] = xstrdup(s);
}

static void end_rule(void)
{
    if (!cur.active)
        return;
    if (cur.cmds) {
        if (cur.infer)
            infer_set(cur.infer, cur.cmds);
        else if (cur.special == 1)
            default_cmds = cur.cmds;
        else if (cur.special == 2)
            sccs_get_cmds = cur.cmds;
        /* commands of .PHONY and the like are ignored */
        for (int i = 0; i < cur.npats; i++)
            cur.pats[i]->cmds = cur.cmds;
        for (int i = 0; i < cur.ntargets; i++) {
            Node *n = cur.targets[i];
            if (cur.dcolon) {
                cur.drules[i]->cmds = cur.cmds;
                continue;
            }
            if (n->cmds && n->cmds != cur.cmds)
                file_warn(cur.cmds->file, cur.cmds->line, "overriding commands for target '%s'", n->name);
            n->cmds = cur.cmds;
        }
    }
    free(cur.targets);
    free(cur.infer);
    free(cur.pats);
    free(cur.drules);
    memset(&cur, 0, sizeof cur);
}

/* ---------- classification helpers ---------- */

/* skip a $(...) or ${...} starting at p (p[0]=='$'); returns the position after it */
static const char *skip_ref(const char *p)
{
    char o = p[1], c = o == '(' ? ')' : '}';
    int d = 1;
    p += 2;
    while (*p && d) {
        if (*p == o)
            d++;
        else if (*p == c)
            d--;
        p++;
    }
    return p;
}

/* first '#' (comment) */
static char *find_comment(char *s)
{
    return strchr(s, '#');
}

/* the first top-level ':' or '=' (outside macro references) */
static const char *find_sep(const char *s)
{
    for (const char *p = s; *p; p++) {
        if (*p == '$' && (p[1] == '(' || p[1] == '{')) {
            p = skip_ref(p) - 1;
            continue;
        }
        if (*p == '$' && p[1]) {
            p++;
            continue;
        }
        if (*p == ':' || *p == '=')
            return p;
    }
    return NULL;
}

static const char *find_semicolon(const char *s)
{
    for (const char *p = s; *p; p++) {
        if (*p == '$' && (p[1] == '(' || p[1] == '{')) {
            p = skip_ref(p) - 1;
            continue;
        }
        if (*p == '$' && p[1]) {
            p++;
            continue;
        }
        if (*p == ';')
            return p;
    }
    return NULL;
}

static char *trim(char *s)
{
    size_t l;
    while (is_blank((unsigned char)*s))
        s++;
    l = strlen(s);
    while (l && is_blank((unsigned char)s[l - 1]))
        s[--l] = 0;
    return s;
}

/* "lib(a.o b.o)" -> lib(a.o) lib(b.o) */
static void expand_members(Words *in, Words *out)
{
    for (int i = 0; i < in->n; i++) {
        char *w = in->v[i], *lp = strchr(w, '(');
        size_t l = strlen(w);
        if (lp && lp != w && w[l - 1] == ')') {
            Words m = { 0 };
            char *inner = xstrndup(lp + 1, l - (size_t)(lp - w) - 2);
            split_words(inner, &m, 0);
            for (int k = 0; k < m.n; k++) {
                Buf b = { 0 };
                buf_add(&b, w, (size_t)(lp - w) + 1);
                buf_adds(&b, m.v[k]);
                buf_addc(&b, ')');
                words_add(out, buf_take(&b));
            }
            words_free(&m);
            free(inner);
        } else
            words_add(out, xstrdup(w));
    }
}

/* ---------- rules ---------- */

static void special_target(const char *t, Words *pre)
{
    if (strcmp(t, ".PHONY") == 0) {
        for (int i = 0; i < pre->n; i++)
            node_get(pre->v[i])->phony = 1;
    } else if (strcmp(t, ".PRECIOUS") == 0) {
        if (!pre->n)
            all_precious = 1;
        for (int i = 0; i < pre->n; i++)
            node_get(pre->v[i])->precious = 1;
    } else if (strcmp(t, ".SILENT") == 0) {
        if (!pre->n)
            all_silent = 1;
        for (int i = 0; i < pre->n; i++)
            node_get(pre->v[i])->silent = 1;
    } else if (strcmp(t, ".IGNORE") == 0) {
        if (!pre->n)
            all_ignore = 1;
        for (int i = 0; i < pre->n; i++)
            node_get(pre->v[i])->ignore = 1;
    } else if (strcmp(t, ".SUFFIXES") == 0) {
        if (!pre->n)
            words_free(&suffixes);
        for (int i = 0; i < pre->n; i++) {
            int dup = 0;
            for (int k = 0; k < suffixes.n; k++)
                if (strcmp(suffixes.v[k], pre->v[i]) == 0)
                    dup = 1;
            if (!dup)
                words_add(&suffixes, xstrdup(pre->v[i]));
        }
    } else if (strcmp(t, ".POSIX") == 0) {
        posix_mode = 1;
    } else if (strcmp(t, ".NOTPARALLEL") == 0) {
        not_parallel = 1;
    }
}

static int known_special(const char *t)
{
    static const char *const sp[] = { ".DEFAULT", ".IGNORE", ".NOTPARALLEL", ".PHONY", ".POSIX",
                                       ".PRECIOUS", ".SCCS_GET", ".SILENT", ".SUFFIXES", ".WAIT", NULL };
    for (int i = 0; sp[i]; i++)
        if (strcmp(sp[i], t) == 0)
            return 1;
    return 0;
}

static void rule_line(Src *src, char *lhs, char *rhs, const char *cmd, int dcolon)
{
    Words tw0 = { 0 }, pw0 = { 0 }, tw = { 0 }, pw = { 0 };
    char *el = expand(lhs), *er = expand(rhs);
    split_words(el, &tw0, 1);
    split_words(er, &pw0, 1);
    expand_members(&tw0, &tw);
    expand_members(&pw0, &pw);
    words_free(&tw0);
    words_free(&pw0);
    free(el);
    free(er);
    end_rule();
    if (!tw.n)
        file_fatal(src->name, src->line, "missing target");
    cur.active = 1;
    for (int i = 0; i < tw.n; i++) {
        const char *t = tw.v[i];
        if (known_special(t)) {
            if (strcmp(t, ".DEFAULT") == 0)
                cur.special = 1;
            else if (strcmp(t, ".SCCS_GET") == 0)
                cur.special = 2;
            else {
                if (strcmp(t, ".WAIT") != 0)
                    special_target(t, &pw);
                if (!cur.special)
                    cur.special = 3;
            }
            continue;
        }
        if (tw.n == 1 && pw.n == 0 && is_inference_name(t) && !dcolon) {
            cur.infer = xstrdup(t);
            continue;
        }
        if (strchr(t, '%')) {
            Pattern *p = xcalloc(1, sizeof *p), **pp;
            p->target = xstrdup(t);
            for (int k = 0; k < pw.n; k++) {
                if (strcmp(pw.v[k], ".WAIT") == 0)
                    continue;
                p->pre = xrealloc(p->pre, (size_t)(p->npre + 1) * sizeof(char *));
                p->pre[p->npre++] = xstrdup(pw.v[k]);
            }
            /* a later rule for the same patterns replaces the earlier one */
            for (pp = &patterns; *pp; pp = &(*pp)->next)
                ;
            *pp = p;
            cur.pats = xrealloc(cur.pats, (size_t)(cur.npats + 1) * sizeof(Pattern *));
            cur.pats[cur.npats++] = p;
            continue;
        }
        {
            Node *n = node_get(t);
            n->is_target = 1;
            if (dcolon) {
                DRule *d = xcalloc(1, sizeof *d), **dp;
                if (n->cmds)
                    file_fatal(src->name, src->line, "target '%s' has both : and :: entries", t);
                n->dcolon = 1;
                for (dp = &n->drules; *dp; dp = &(*dp)->next)
                    ;
                *dp = d;
                for (int k = 0; k < pw.n; k++) {
                    Node *p = strcmp(pw.v[k], ".WAIT") == 0 ? wait_node : node_get(pw.v[k]);
                    d->pre = xrealloc(d->pre, (size_t)(d->npre + 1) * sizeof(Node *));
                    d->pre[d->npre++] = p;
                    node_add_pre(n, p);
                }
                cur.drules = xrealloc(cur.drules, (size_t)(cur.ntargets + 1) * sizeof(DRule *));
                cur.drules[cur.ntargets] = d;
            } else {
                if (n->dcolon)
                    file_fatal(src->name, src->line, "target '%s' has both : and :: entries", t);
                for (int k = 0; k < pw.n; k++)
                    node_add_pre(n, strcmp(pw.v[k], ".WAIT") == 0 ? wait_node : node_get(pw.v[k]));
            }
            cur.targets = xrealloc(cur.targets, (size_t)(cur.ntargets + 1) * sizeof(Node *));
            cur.targets[cur.ntargets++] = n;
            if (!first_target && !is_special_name(t))
                first_target = n;
        }
    }
    cur.dcolon = dcolon;
    if (cmd) {
        cur.cmds = new_cmds(src->name, src->line);
        while (is_blank((unsigned char)*cmd))
            cmd++;
        if (*cmd)
            cmds_add(cur.cmds, cmd);
    }
    words_free(&tw);
    words_free(&pw);
}

/* ---------- macro definitions ---------- */

enum { OP_DELAYED, OP_IMMEDIATE, OP_KEEP, OP_SHELL, OP_COND, OP_APPEND };

static int macro_line(char *line, const char *sep)
{
    int op;
    size_t nlen;
    char *name, *value;
    const char *vstart;
    if (*sep == '=') {
        nlen = (size_t)(sep - line);
        op = OP_DELAYED;
        if (nlen && strchr("+?!", line[nlen - 1])) {
            op = line[nlen - 1] == '+' ? OP_APPEND : line[nlen - 1] == '?' ? OP_COND : OP_SHELL;
            nlen--;
        }
        vstart = sep + 1;
    } else {
        nlen = (size_t)(sep - line);
        if (strncmp(sep, ":::=", 4) == 0) {
            op = OP_KEEP;
            vstart = sep + 4;
        } else if (strncmp(sep, "::=", 3) == 0) {
            op = OP_IMMEDIATE;
            vstart = sep + 3;
        } else if (strncmp(sep, ":=", 2) == 0) {
            op = OP_IMMEDIATE;     /* extension: as GNU make's := */
            vstart = sep + 2;
        } else
            return 0;
    }
    {
        char *raw = xstrndup(line, nlen);
        char *e = expand(raw);
        name = xstrdup(trim(e));
        free(e);
        free(raw);
    }
    while (is_blank((unsigned char)*vstart))
        vstart++;
    value = xstrdup(vstart);
    if (!*name) {
        free(name);
        free(value);
        return -1;
    }
    {
        switch (op) {
        case OP_DELAYED:
            var_set(name, value, 1, SRC_FILE);
            break;
        case OP_IMMEDIATE: {
            char *e = expand(value);
            var_set(name, e, 0, SRC_FILE);
            free(e);
            break;
        }
        case OP_KEEP: {
            char *e = expand_keep(value);
            var_set(name, e, 1, SRC_FILE);
            free(e);
            break;
        }
        case OP_SHELL:
            var_shell(name, value, SRC_FILE);
            break;
        case OP_COND:
            var_set_cond(name, value, SRC_FILE);
            break;
        case OP_APPEND:
            var_append(name, value, SRC_FILE);
            break;
        }
    }
    free(name);
    free(value);
    return 1;
}

/* ---------- include ---------- */

static void parse_src(Src *src);

static void include_line(Src *src, char *rest, int optional)
{
    char *e = expand(rest);
    Words w = { 0 };
    split_words(e, &w, 0);
    free(e);
    for (int i = 0; i < w.n; i++) {
        glob_t g;
        int globbed = 0;
        if (strpbrk(w.v[i], "*?[") && glob(w.v[i], 0, NULL, &g) == 0)
            globbed = 1;
        for (size_t k = 0; k < (globbed ? g.gl_pathc : 1); k++) {
            const char *path = globbed ? g.gl_pathv[k] : w.v[i];
            FILE *fp;
            make_include(path, optional);
            fp = fopen(path, "r");
            if (!fp) {
                if (optional)
                    continue;
                file_fatal(src->name, src->line, "%s: %s", path, strerror(errno));
            }
            if (include_depth >= 64)
                file_fatal(src->name, src->line, "includes nested too deeply");
            {
                Src s = { 0 };
                s.fp = fp;
                s.name = xstrdup(path);
                include_depth++;
                parse_src(&s);
                include_depth--;
                fclose(fp);
            }
        }
        if (globbed)
            globfree(&g);
    }
    words_free(&w);
}

/* is this an include line? sets *rest and *optional */
static int is_include(char *line, char **rest, int *optional)
{
    char *p = line;
    *optional = 0;
    if (*p == '-') {
        *optional = 1;
        p++;
    } else if (strncmp(p, "sinclude", 8) == 0 && (p[8] == ' ' || p[8] == '\t')) {
        *optional = 1;
        p++;
    }
    if (strncmp(p, "include", 7) != 0 || (p[7] != ' ' && p[7] != '\t'))
        return 0;
    p += 7;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '=' || (p[0] == ':' && p[1] == '=') || (strchr("+?!", *p) && *p && p[1] == '='))
        return 0;   /* a macro named include */
    *rest = p;
    return 1;
}

/* ---------- the main loop ---------- */

static void parse_src(Src *src)
{
    char *line;
    while ((line = read_phys(src))) {
        int start = src->line;
        if (cur.active && line[0] == '\t') {
            /* a command line */
            Buf b = { 0 };
            buf_adds(&b, line + 1);
            free(line);
            while (ends_escaped(b.s ? b.s : "")) {
                char *next = read_phys(src);
                if (!next)
                    break;
                buf_addc(&b, '\n');
                buf_adds(&b, next[0] == '\t' ? next + 1 : next);
                free(next);
            }
            if (!cur.cmds)
                cur.cmds = new_cmds(src->name, start);
            cmds_add(cur.cmds, b.s ? b.s : "");
            buf_free(&b);
            continue;
        }
        /* join continuation lines */
        {
            Buf b = { 0 };
            buf_adds(&b, line);
            free(line);
            while (b.s && ends_escaped(b.s)) {
                char *next = read_phys(src), *p;
                b.s[--b.len] = 0;
                if (!next)
                    break;
                p = next;
                while (*p == ' ' || *p == '\t')
                    p++;
                buf_addc(&b, ' ');
                buf_adds(&b, p);
                free(next);
            }
            line = buf_take(&b);
        }
        {
            char *hash = find_comment(line), *rest;
            const char *sep, *semi;
            int optional;
            char *t;
            /* comment lines and blank lines do not end a rule */
            for (t = line; *t == ' ' || *t == '\t'; t++)
                ;
            if (!*t || *t == '#') {
                free(line);
                continue;
            }
            semi = find_semicolon(line);
            sep = find_sep(line);
            if (hash) {
                /* the commands after a rule's ';' keep their '#' */
                int is_rule = sep && *sep == ':' && strncmp(sep, ":=", 2) && strncmp(sep, "::=", 3) &&
                              strncmp(sep, ":::=", 4);
                if (!(is_rule && semi && semi > sep && semi < hash))
                    *hash = 0;
            }
            /* a line that became blank after the comment was removed */
            for (t = line; *t == ' ' || *t == '\t'; t++)
                ;
            if (!*t) {
                free(line);
                continue;
            }
            if (is_include(line, &rest, &optional)) {
                end_rule();
                include_line(src, rest, optional);
                free(line);
                continue;
            }
            sep = find_sep(line);
            if (sep && (*sep == '=' || strncmp(sep, ":=", 2) == 0 || strncmp(sep, "::=", 3) == 0 ||
                        strncmp(sep, ":::=", 4) == 0)) {
                int r;
                end_rule();
                r = macro_line(line, sep);
                if (r < 0)
                    file_fatal(src->name, start, "empty macro name");
                free(line);
                continue;
            }
            if (sep && *sep == ':') {
                char *l = line, *colon = (char *)sep, *r;
                char *cmd = NULL;
                int dcolon = colon[1] == ':';
                semi = find_semicolon(colon);
                if (semi) {
                    cmd = xstrdup(semi + 1);
                    *(char *)semi = 0;
                }
                *colon = 0;
                r = colon + 1 + dcolon;
                rule_line(src, l, r, cmd, dcolon);
                free(cmd);
                free(line);
                continue;
            }
            if (line[0] == '\t')
                file_fatal(src->name, start, "commands commence before first target");
            file_fatal(src->name, start, "missing separator");
        }
    }
    end_rule();
}

void read_makefile(const char *name, int must_exist)
{
    Src s = { 0 };
    if (strcmp(name, "-") == 0) {
        s.fp = stdin;
        s.name = "(standard input)";
    } else {
        s.fp = fopen(name, "r");
        if (!s.fp) {
            if (must_exist)
                fatal("%s: %s", name, strerror(errno));
            return;
        }
        s.name = xstrdup(name);
    }
    parse_src(&s);
    if (s.fp != stdin)
        fclose(s.fp);
}

void parse_string(const char *text, const char *name)
{
    Src s = { 0 };
    s.text = text;
    s.name = name;
    parse_src(&s);
}
