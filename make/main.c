/*
 * main.c - options, MAKEFLAGS, the sources of macros, the built-in
 * rules, the makefiles and the goals.
 */
#include "make.h"

extern char **environ;
char *makeflags_value;

static Words makefiles, goals, cmd_macros;
static int jobs_from_cmdline, jobs_from_makeflags = -1;
static int inherited_r = -1, inherited_w = -1;

static void usage(void)
{
    fprintf(stderr, "usage: %s [-einpqrst] [-f makefile]... [-j maxjobs] [-k|-S] [macro=value...] [target...]\n",
            progname);
    exit(2);
}

/* ---------- built-in rules ---------- */

static const char builtin_rules[] =
    ".SUFFIXES: .o .c .y .l .a .sh .c~ .y~ .l~ .sh~\n"
    ".SCCS_GET:\n"
    "\tsccs $(SCCSFLAGS) get $(SCCSGETFLAGS) $@\n"
    ".c:\n"
    "\t$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<\n"
    ".sh:\n"
    "\tcp $< $@\n"
    "\tchmod a+x $@\n"
    ".c~:\n"
    "\t$(GET) $(GFLAGS) -p $< > $*.c\n"
    "\t$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $*.c\n"
    ".sh~:\n"
    "\t$(GET) $(GFLAGS) -p $< > $*.sh\n"
    "\tcp $*.sh $@\n"
    "\tchmod a+x $@\n"
    ".c.o:\n"
    "\t$(CC) $(CFLAGS) -c $<\n"
    ".y.o:\n"
    "\t$(YACC) $(YFLAGS) $<\n"
    "\t$(CC) $(CFLAGS) -c y.tab.c\n"
    "\trm -f y.tab.c\n"
    "\tmv y.tab.o $@\n"
    ".l.o:\n"
    "\t$(LEX) $(LFLAGS) $<\n"
    "\t$(CC) $(CFLAGS) -c lex.yy.c\n"
    "\trm -f lex.yy.c\n"
    "\tmv lex.yy.o $@\n"
    ".y.c:\n"
    "\t$(YACC) $(YFLAGS) $<\n"
    "\tmv y.tab.c $@\n"
    ".l.c:\n"
    "\t$(LEX) $(LFLAGS) $<\n"
    "\tmv lex.yy.c $@\n"
    ".c~.o:\n"
    "\t$(GET) $(GFLAGS) -p $< > $*.c\n"
    "\t$(CC) $(CFLAGS) -c $*.c\n"
    ".y~.o:\n"
    "\t$(GET) $(GFLAGS) -p $< > $*.y\n"
    "\t$(YACC) $(YFLAGS) $*.y\n"
    "\t$(CC) $(CFLAGS) -c y.tab.c\n"
    "\trm -f y.tab.c\n"
    "\tmv y.tab.o $@\n"
    ".l~.o:\n"
    "\t$(GET) $(GFLAGS) -p $< > $*.l\n"
    "\t$(LEX) $(LFLAGS) $*.l\n"
    "\t$(CC) $(CFLAGS) -c lex.yy.c\n"
    "\trm -f lex.yy.c\n"
    "\tmv lex.yy.o $@\n"
    ".y~.c:\n"
    "\t$(GET) $(GFLAGS) -p $< > $*.y\n"
    "\t$(YACC) $(YFLAGS) $*.y\n"
    "\tmv y.tab.c $@\n"
    ".l~.c:\n"
    "\t$(GET) $(GFLAGS) -p $< > $*.l\n"
    "\t$(LEX) $(LFLAGS) $*.l\n"
    "\tmv lex.yy.c $@\n"
    ".c.a:\n"
    "\t$(CC) -c $(CFLAGS) $<\n"
    "\t$(AR) $(ARFLAGS) $@ $*.o\n"
    "\trm -f $*.o\n";

static int in_path(const char *prog)
{
    const char *path = getenv("PATH");
    Buf b = { 0 };
    if (!path)
        path = "/usr/bin:/bin";
    while (*path) {
        const char *c = strchr(path, ':');
        size_t l = c ? (size_t)(c - path) : strlen(path);
        buf_reset(&b);
        buf_add(&b, l ? path : ".", l ? l : 1);
        buf_addc(&b, '/');
        buf_adds(&b, prog);
        if (access(b.s, X_OK) == 0) {
            buf_free(&b);
            return 1;
        }
        if (!c)
            break;
        path = c + 1;
    }
    buf_free(&b);
    return 0;
}

static void builtin_macros(void)
{
    static const char *const m[][2] = {
        { "AR", "ar" },     { "ARFLAGS", "-rv" }, { "YACC", "yacc" },       { "YFLAGS", "" },
        { "LEX", "lex" },   { "LFLAGS", "" },     { "LDFLAGS", "" },        { "GET", "get" },
        { "GFLAGS", "" },   { "SCCSFLAGS", "" },  { "SCCSGETFLAGS", "-s" }, { "SHELL", "/bin/sh" },
    };
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++)
        var_set(m[i][0], m[i][1], 1, SRC_BUILTIN);
    if (in_path("c17")) {
        var_set("CC", "c17", 1, SRC_BUILTIN);
        var_set("CFLAGS", "-O 1", 1, SRC_BUILTIN);
    } else {
        var_set("CC", "cc", 1, SRC_BUILTIN);
        var_set("CFLAGS", "-O1", 1, SRC_BUILTIN);
    }
}

/* ---------- options ---------- */

static int opt_S_seen;

static void set_jobs(const char *arg, int from_mf)
{
    char *end;
    long v = strtol(arg, &end, 10);
    if (end == arg || *end || v <= 0) {
        if (from_mf)
            return;
        diag("-j %s: invalid number of jobs", arg);
        usage();
    }
    max_jobs = (int)(v > 4096 ? 4096 : v);
    if (from_mf)
        jobs_from_makeflags = max_jobs;
    else
        jobs_from_cmdline = 1;
}

/* one option letter; returns 1 if it takes an argument */
static int option(int c, const char *arg, int from_mf)
{
    switch (c) {
    case 'e':
        opt_e = 1;
        break;
    case 'i':
        opt_i = 1;
        break;
    case 'k':
        opt_k = 1;
        break;
    case 'S':
        opt_k = 0;
        opt_S_seen = 1;
        break;
    case 'n':
        opt_n = 1;
        break;
    case 'p':
        if (!from_mf)
            opt_p = 1;
        break;
    case 'q':
        opt_q = 1;
        break;
    case 'r':
        opt_r = 1;
        break;
    case 's':
        opt_s = 1;
        break;
    case 't':
        opt_t = 1;
        break;
    case 'j':
        if (!arg)
            return -1;
        set_jobs(arg, from_mf);
        return 1;
    case 'f':
        if (from_mf)
            return arg ? 1 : 0;
        if (!arg)
            return -1;
        words_add(&makefiles, xstrdup(arg));
        return 1;
    default:
        if (from_mf)
            return 0;   /* unknown letters from other makes are ignored */
        diag("illegal option -- %c", c);
        usage();
    }
    return 0;
}

/* a macro operand: name=value, name::=value, name:::=value */
static int macro_operand(const char *s, int source)
{
    const char *eq = strchr(s, '=');
    size_t nl;
    int op = 0;
    char *name;
    if (!eq || eq == s)
        return 0;
    nl = (size_t)(eq - s);
    if (nl >= 3 && strncmp(eq - 3, ":::", 3) == 0) {
        op = 2;
        nl -= 3;
    } else if (nl >= 2 && strncmp(eq - 2, "::", 2) == 0) {
        op = 1;
        nl -= 2;
    } else if (nl >= 1 && eq[-1] == ':') {
        op = 1;
        nl -= 1;
    }
    name = xstrndup(s, nl);
    {
        char *t = name;
        while (*t && !is_blank((unsigned char)*t))
            t++;
        if (*t || !*name) {
            free(name);
            return 0;
        }
    }
    if (op == 0)
        var_set(name, eq + 1, 1, source);
    else if (op == 1) {
        char *e = expand(eq + 1);
        var_set(name, e, 0, source);
        free(e);
    } else {
        char *e = expand_keep(eq + 1);
        var_set(name, e, 1, source);
        free(e);
    }
    if (source == SRC_CMDLINE && strcmp(name, "MAKEFLAGS") != 0 && strcmp(name, "SHELL") != 0)
        setenv(name, eq + 1, 1);
    free(name);
    return 1;
}

/* split MAKEFLAGS into words, with backslash escapes */
static void mf_words(const char *s, Words *w)
{
    while (*s) {
        Buf b = { 0 };
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s)
            break;
        while (*s && *s != ' ' && *s != '\t') {
            if (*s == '\\' && s[1]) {
                buf_addc(&b, s[1]);
                s += 2;
                continue;
            }
            buf_addc(&b, *s++);
        }
        words_add(w, buf_take(&b));
    }
}

static void parse_makeflags(const char *mf)
{
    Words w = { 0 };
    int i = 0, opts_done = 0;
    mf_words(mf, &w);
    if (w.n && w.v[0][0] != '-' && !strchr(w.v[0], '=')) {
        /* letters without '-' */
        const char *p = w.v[0];
        for (; *p; p++) {
            if (*p == 'j' || *p == 'f') {
                if (i + 1 < w.n) {
                    option(*p, w.v[i + 1], 1);
                    i++;
                }
                continue;
            }
            option(*p, NULL, 1);
        }
        i++;
    }
    for (; i < w.n; i++) {
        const char *a = w.v[i];
        if (!opts_done && strcmp(a, "--") == 0) {
            opts_done = 1;
            continue;
        }
        if (!opts_done && strncmp(a, "--jobserver-auth=", 17) == 0) {
            int r, wr;
            if (sscanf(a + 17, "%d,%d", &r, &wr) == 2) {
                inherited_r = r;
                inherited_w = wr;
            }
            continue;
        }
        if (!opts_done && a[0] == '-' && a[1] == '-')
            continue;    /* long options of other makes */
        if (!opts_done && a[0] == '-' && a[1]) {
            for (const char *p = a + 1; *p; p++) {
                if (*p == 'j' || *p == 'f') {
                    const char *arg = p[1] ? p + 1 : (i + 1 < w.n ? w.v[++i] : NULL);
                    if (arg)
                        option(*p, arg, 1);
                    break;
                }
                option(*p, NULL, 1);
            }
            continue;
        }
        macro_operand(a, SRC_MAKEFLAGS);
    }
    words_free(&w);
}

static void parse_args(int argc, char **argv)
{
    int i, ddash = 0;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!ddash && strcmp(a, "--") == 0) {
            ddash = 1;
            continue;
        }
        if (!ddash && a[0] == '-' && a[1]) {
            for (const char *p = a + 1; *p; p++) {
                if (*p == 'f' || *p == 'j') {
                    const char *arg = p[1] ? p + 1 : (i + 1 < argc ? argv[++i] : NULL);
                    if (!arg) {
                        diag("option requires an argument -- %c", *p);
                        usage();
                    }
                    option(*p, arg, 0);
                    break;
                }
                option(*p, NULL, 0);
            }
            continue;
        }
        if (strchr(a, '=') && macro_operand(a, SRC_CMDLINE)) {
            words_add(&cmd_macros, xstrdup(a));
            continue;
        }
        words_add(&goals, xstrdup(a));
    }
}

static void add_escaped(Buf *b, const char *s)
{
    for (; *s; s++) {
        if (*s == ' ' || *s == '\t' || *s == '\\' || *s == '\n')
            buf_addc(b, '\\');
        buf_addc(b, *s);
    }
}

static void build_makeflags(const char *inherited)
{
    Buf b = { 0 };
    char letters[16], *l = letters;
    if (opt_e)
        *l++ = 'e';
    if (opt_i)
        *l++ = 'i';
    if (opt_k)
        *l++ = 'k';
    if (opt_n)
        *l++ = 'n';
    if (opt_q)
        *l++ = 'q';
    if (opt_r)
        *l++ = 'r';
    if (opt_s)
        *l++ = 's';
    if (opt_t)
        *l++ = 't';
    if (!opt_k && opt_S_seen)
        *l++ = 'S';
    *l = 0;
    if (*letters) {
        buf_addc(&b, '-');
        buf_adds(&b, letters);
    }
    if (max_jobs > 1) {
        char t[64];
        snprintf(t, sizeof t, "%s-j %d", b.len ? " " : "", max_jobs);
        buf_adds(&b, t);
        if (jobserver_r >= 0) {
            snprintf(t, sizeof t, " --jobserver-auth=%d,%d", jobserver_r, jobserver_w);
            buf_adds(&b, t);
        }
    }
    /* macros: those from MAKEFLAGS, then the command line */
    {
        Words mw = { 0 };
        int any = 0;
        if (inherited) {
            Words w = { 0 };
            int opts_done = 0;
            mf_words(inherited, &w);
            for (int i = 0; i < w.n; i++) {
                if (strcmp(w.v[i], "--") == 0) {
                    opts_done = 1;
                    continue;
                }
                if ((opts_done || w.v[i][0] != '-') && strchr(w.v[i], '=') && w.v[i][0] != '=')
                    words_add(&mw, xstrdup(w.v[i]));
            }
            words_free(&w);
        }
        for (int i = 0; i < cmd_macros.n; i++)
            words_add(&mw, xstrdup(cmd_macros.v[i]));
        for (int i = 0; i < mw.n; i++) {
            if (!any) {
                buf_adds(&b, b.len ? " --" : "--");
                any = 1;
            }
            buf_addc(&b, ' ');
            add_escaped(&b, mw.v[i]);
        }
        words_free(&mw);
    }
    makeflags_value = buf_take(&b);
    var_set("MAKEFLAGS", makeflags_value, 0, SRC_BUILTIN);
    setenv("MAKEFLAGS", makeflags_value, 1);
}

/* ---------- the environment of commands ---------- */

static char **envv;
static int envn, envcap;

static void env_put(const char *name, const char *value)
{
    size_t nl = strlen(name);
    Buf b = { 0 };
    buf_adds(&b, name);
    buf_addc(&b, '=');
    buf_adds(&b, value);
    for (int i = 0; i < envn; i++)
        if (strncmp(envv[i], name, nl) == 0 && envv[i][nl] == '=') {
            envv[i] = buf_take(&b);
            return;
        }
    if (envn + 2 > envcap) {
        envcap = envcap ? envcap * 2 : 64;
        envv = xrealloc(envv, (size_t)envcap * sizeof(char *));
    }
    envv[envn++] = buf_take(&b);
    envv[envn] = NULL;
}

static void export_var(Var *v)
{
    if (v->from_env && v->source != SRC_ENV && strcmp(v->name, "SHELL") != 0 &&
        strcmp(v->name, "MAKEFLAGS") != 0) {
        char *e = v->delayed ? expand(v->value) : xstrdup(v->value);
        env_put(v->name, e);
        free(e);
    }
}

char **child_environ(int recursive)
{
    Var *mf;
    (void)recursive;
    envv = NULL;
    envn = envcap = 0;
    for (char **e = environ; *e; e++) {
        char *eq = strchr(*e, '=');
        if (!eq)
            continue;
        if (envn + 2 > envcap) {
            envcap = envcap ? envcap * 2 : 64;
            envv = xrealloc(envv, (size_t)envcap * sizeof(char *));
        }
        envv[envn++] = *e;
        envv[envn] = NULL;
    }
    var_foreach(export_var);
    mf = var_get("MAKEFLAGS");
    if (mf) {
        char *e = mf->delayed ? expand(mf->value) : xstrdup(mf->value);
        env_put("MAKEFLAGS", e);
        free(e);
    }
    if (!envv) {
        envv = xcalloc(1, sizeof(char *));
    }
    return envv;
}

/* ---------- -p ---------- */

static void print_var(Var *v)
{
    static const char *const src[] = { "", "command line", "MAKEFLAGS", "environment", "makefile", "default" };
    printf("# %s\n%s %s %s\n", src[v->source], v->name, v->delayed ? "=" : "::=", v->value);
}

static void print_cmds(Cmds *c)
{
    if (!c)
        return;
    for (int i = 0; i < c->n; i++) {
        printf("\t");
        for (const char *p = c->v[i]; *p; p++) {
            putchar(*p);
            if (*p == '\n')
                putchar('\t');
        }
        putchar('\n');
    }
}

static void print_node(Node *n)
{
    if (!n->is_target && !n->cmds)
        return;
    if (n->dcolon) {
        for (DRule *d = n->drules; d; d = d->next) {
            printf("%s::", n->name);
            for (int i = 0; i < d->npre; i++)
                printf(" %s", d->pre[i]->name);
            putchar('\n');
            print_cmds(d->cmds);
            putchar('\n');
        }
        return;
    }
    printf("%s:", n->name);
    for (int i = 0; i < n->npre; i++)
        printf(" %s", n->pre[i]->name);
    putchar('\n');
    print_cmds(n->cmds);
    putchar('\n');
}

static void print_infer(Infer *r)
{
    printf("%s:\n", r->name);
    print_cmds(r->cmds);
    putchar('\n');
}

void print_database(void)
{
    printf("# Macros\n\n");
    var_foreach(print_var);
    printf("\n# Special targets\n\n.SUFFIXES:");
    for (int i = 0; i < suffixes.n; i++)
        printf(" %s", suffixes.v[i]);
    printf("\n\n");
    if (default_cmds) {
        printf(".DEFAULT:\n");
        print_cmds(default_cmds);
        putchar('\n');
    }
    if (sccs_get_cmds) {
        printf(".SCCS_GET:\n");
        print_cmds(sccs_get_cmds);
        putchar('\n');
    }
    printf("# Inference rules\n\n");
    infer_foreach(print_infer);
    for (Pattern *p = patterns; p; p = p->next) {
        printf("%s:", p->target);
        for (int i = 0; i < p->npre; i++)
            printf(" %s", p->pre[i]);
        putchar('\n');
        print_cmds(p->cmds);
        putchar('\n');
    }
    printf("# Target rules\n\n");
    node_foreach(print_node);
    fflush(stdout);
}

/* ---------- makefiles ---------- */

/* get an SCCS makefile into a temporary file */
static char *sccs_makefile(const char *sfile)
{
    char tmpl[] = "/tmp/makeXXXXXX";
    int fd = mkstemp(tmpl);
    pid_t pid;
    int status;
    if (fd < 0)
        return NULL;
    pid = fork();
    if (pid == 0) {
        dup2(fd, 1);
        execlp("get", "get", "-s", "-p", sfile, (char *)NULL);
        _exit(127);
    }
    close(fd);
    if (pid < 0 || waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status)) {
        unlink(tmpl);
        return NULL;
    }
    return xstrdup(tmpl);
}

static void read_default_makefile(void)
{
    static const char *const names[] = { "makefile", "Makefile" };
    static const char *const snames[] = { "s.makefile", "SCCS/s.makefile", "s.Makefile", "SCCS/s.Makefile" };
    for (size_t i = 0; i < 2; i++)
        if (access(names[i], F_OK) == 0) {
            read_makefile(names[i], 1);
            return;
        }
    for (size_t i = 0; i < 4; i++)
        if (access(snames[i], F_OK) == 0) {
            char *t = sccs_makefile(snames[i]);
            if (t) {
                read_makefile(t, 1);
                unlink(t);
                free(t);
                return;
            }
        }
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    const char *mf = getenv("MAKEFLAGS");
    int r;
    setlocale(LC_ALL, "");
    progname = "make";
    /* sources of macros: command line, MAKEFLAGS, environment, built-in */
    if (mf)
        parse_makeflags(mf);
    parse_args(argc, argv);
    for (char **e = environ; *e; e++) {
        char *eq = strchr(*e, '=');
        char *name;
        if (!eq || eq == *e)
            continue;
        name = xstrndup(*e, (size_t)(eq - *e));
        if (strcmp(name, "MAKEFLAGS") && strcmp(name, "SHELL") && (strcmp(name, "CURDIR") || opt_e)) {
            Var *v;
            var_set(name, eq + 1, 1, SRC_ENV);
            if ((v = var_get(name)) != NULL)
                v->from_env = 1;   /* exported with its value as a macro */
        }
        free(name);
    }
    {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof cwd)) {
            var_set("CURDIR", cwd, 1, SRC_BUILTIN);
            if (!opt_e) {
                const char *ec = getenv("CURDIR");
                if (ec && strcmp(ec, cwd) != 0)
                    setenv("CURDIR", cwd, 1);
            }
        }
    }
    if (!getenv("MAKE")) {
        const char *a0 = argv[0];
        if (strchr(a0, '/') && a0[0] != '/') {
            char cwd[PATH_MAX];
            Buf b = { 0 };
            if (getcwd(cwd, sizeof cwd)) {
                buf_adds(&b, cwd);
                buf_addc(&b, '/');
            }
            buf_adds(&b, a0);
            var_set("MAKE", b.s, 1, SRC_BUILTIN);
            buf_free(&b);
        } else
            var_set("MAKE", a0, 1, SRC_BUILTIN);
    }
    builtin_macros();
    /* the token pool for -j */
    if (max_jobs > 1) {
        int shared = 0;
        if (inherited_r >= 0 && jobs_from_makeflags == max_jobs && !jobs_from_cmdline &&
            fcntl(inherited_r, F_GETFD) >= 0 && fcntl(inherited_w, F_GETFD) >= 0) {
            jobserver_r = inherited_r;
            jobserver_w = inherited_w;
            shared = 1;
        }
        if (!shared) {
            int p[2];
            if (pipe(p) == 0) {
                jobserver_r = p[0];
                jobserver_w = p[1];
                for (int i = 0; i < max_jobs - 1; i++)
                    (void)!write(jobserver_w, "+", 1);
            }
        }
        if (jobserver_r >= 0) {
            fcntl(jobserver_r, F_SETFL, fcntl(jobserver_r, F_GETFL) | O_NONBLOCK);
            fcntl(jobserver_r, F_SETFD, FD_CLOEXEC);
            fcntl(jobserver_w, F_SETFD, FD_CLOEXEC);
        }
    }
    build_makeflags(mf);
    setup_signals();
    if (!opt_r)
        parse_string(builtin_rules, "(built-in)");
    if (makefiles.n)
        for (int i = 0; i < makefiles.n; i++)
            read_makefile(makefiles.v[i], 1);
    else
        read_default_makefile();
    if (opt_p)
        print_database();
    if (!goals.n) {
        if (!first_target) {
            if (opt_p)
                return 0;
            if (!makefiles.n && access("makefile", F_OK) && access("Makefile", F_OK))
                fatal("No targets specified and no makefile found");
            fatal("No targets");
        }
        words_add(&goals, xstrdup(first_target->name));
    }
    r = build_goals(&goals, !opt_s && !all_silent);
    fflush(stdout);
    return r;
}
