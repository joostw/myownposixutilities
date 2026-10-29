/*
 * exec.c - command execution (2.9), redirection (2.7), command search.
 */
#include "sh.h"

int evalskip, skipcount, loopnest, funcnest, dotnest;
int errexit_suppressed;     /* inside a context where -e is ignored */
int in_subshell;
int cmd_lineno;

void exec_external_path(const char *path, char **argv, char **envp);
void clear_redir_saves_keep_fds(void) { }
extern void set_job_control_child(void);

const char *command_default_path(void)
{
    static char buf[1024];
    size_t n = confstr(_CS_PATH, buf, sizeof buf);
    if (n == 0 || n > sizeof buf)
        return "/usr/bin:/bin";
    return buf;
}

static int exec_in_place_ok(int flags)
{
    return (flags & EV_EXIT) && !trap_actions[0] && !gotsig;
}

/* ------------------------------------------------------------------ */
/* redirection                                                        */
/* ------------------------------------------------------------------ */

typedef struct RSave { int fd, saved; } RSave;
static RSave *rsaves;
static int nrsaves, rcap;

int redir_depth(void) { return nrsaves; }

static __attribute__((noinline)) int move_high(int fd)
{
    int n = fcntl(fd, F_DUPFD_CLOEXEC, 100);
    if (n < 0)
        n = fcntl(fd, F_DUPFD_CLOEXEC, 10);
    return n;
}

static void push_save(int fd)
{
    int s;
    if (nrsaves == rcap) {
        rcap = rcap ? rcap * 2 : 16;
        rsaves = xrealloc(rsaves, (size_t)rcap * sizeof *rsaves);
    }
    s = fcntl(fd, F_GETFD) >= 0 ? move_high(fd) : -1;
    rsaves[nrsaves].fd = fd;
    rsaves[nrsaves].saved = s;
    nrsaves++;
}

void undo_redirects(int depth)
{
    flushall();
    while (nrsaves > depth) {
        RSave *s = &rsaves[--nrsaves];
        if (s->saved >= 0) {
            dup2(s->saved, s->fd);
            close(s->saved);
        } else
            close(s->fd);
    }
}

void clear_redir_saves(void)
{
    while (nrsaves > 0) {
        if (rsaves[nrsaves - 1].saved >= 0)
            close(rsaves[nrsaves - 1].saved);
        nrsaves--;
    }
}

static int open_heredoc(Redir *r)
{
    const char *body;
    size_t len;
    int pfd[2];
    StackMark m;
    setstackmark(&m);
    body = r->hquoted ? r->here->s : expand_heredoc(r->here);
    len = strlen(body);
    if (len <= PIPE_BUF) {
        if (pipe(pfd) < 0) {
            popstackmark(&m);
            return -1;
        }
        if (len)
            (void)!write(pfd[1], body, len);
        close(pfd[1]);
        popstackmark(&m);
        return pfd[0];
    }
    {
        char tmpl[PATH_MAX];
        const char *td = getenv("TMPDIR");
        int fd;
        snprintf(tmpl, sizeof tmpl, "%s/shXXXXXX", td && *td ? td : "/tmp");
        fd = mkstemp(tmpl);
        if (fd < 0) {
            popstackmark(&m);
            return -1;
        }
        unlink(tmpl);
        {
            size_t off = 0;
            while (off < len) {
                ssize_t k = write(fd, body + off, len - off);
                if (k <= 0) break;
                off += (size_t)k;
            }
        }
        lseek(fd, 0, SEEK_SET);
        popstackmark(&m);
        return fd;
    }
}

/* perform redirections; returns 0, or -1 after writing a diagnostic */
int redirect(Redir *r, int save)
{
    for (; r; r = r->next) {
        int fd = r->fd, nfd = -1;
        char *fname = NULL;
        if (r->type != R_HERE && r->type != R_HERESTRIP)
            fname = expand_word_str(r->word, EXP_NOSPLIT | EXP_TILDE | EXP_REDIR);
        switch (r->type) {
        case R_IN:
            nfd = open(fname, O_RDONLY);
            break;
        case R_OUT:
            if (opts[O_NOCLOBBER]) {
                nfd = open(fname, O_WRONLY | O_CREAT | O_EXCL, 0666);
                if (nfd < 0 && errno == EEXIST) {
                    struct stat st;
                    if (stat(fname, &st) == 0 && !S_ISREG(st.st_mode))
                        nfd = open(fname, O_WRONLY);
                    else {
                        sh_warn("cannot create %s: file exists", fname);
                        return -1;
                    }
                }
                break;
            }
            /* fallthrough */
        case R_CLOBBER:
            nfd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            break;
        case R_APPEND:
            nfd = open(fname, O_WRONLY | O_CREAT | O_APPEND, 0666);
            break;
        case R_RDWR:
            nfd = open(fname, O_RDWR | O_CREAT, 0666);
            break;
        case R_DUPIN: case R_DUPOUT:
            if (strcmp(fname, "-") == 0) {
                if (save)
                    push_save(fd);
                close(fd);
                continue;
            }
            if (!is_number(fname)) {
                sh_warn("%s: bad file descriptor", fname);
                return -1;
            }
            {
                int src = atoi(fname);
                if (fcntl(src, F_GETFD) < 0) {
                    sh_warn("%s: bad file descriptor", fname);
                    return -1;
                }
                if (src == fd)
                    continue;
                if (save)
                    push_save(fd);
                if (dup2(src, fd) < 0) {
                    sh_warn("%d: %s", fd, strerror(errno));
                    return -1;
                }
                continue;
            }
        case R_HERE: case R_HERESTRIP:
            nfd = open_heredoc(r);
            if (nfd < 0) {
                sh_warn("cannot create here-document: %s", strerror(errno));
                return -1;
            }
            break;
        }
        if (nfd < 0) {
            int e = errno;
            if (r->type == R_IN)
                sh_warn("cannot open %s: %s", fname, strerror(e));
            else
                sh_warn("cannot create %s: %s", fname, strerror(e));
            return -1;
        }
        if (nfd != fd) {
            if (save)
                push_save(fd);
            if (dup2(nfd, fd) < 0) {
                sh_warn("%d: %s", fd, strerror(errno));
                close(nfd);
                return -1;
            }
            close(nfd);
        } else if (save) {
            /* opened directly onto fd: it was closed before */
            if (nrsaves == rcap) {
                rcap = rcap ? rcap * 2 : 16;
                rsaves = xrealloc(rsaves, (size_t)rcap * sizeof *rsaves);
            }
            rsaves[nrsaves].fd = fd;
            rsaves[nrsaves].saved = -1;
            nrsaves++;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* command search                                                     */
/* ------------------------------------------------------------------ */

typedef struct HashEnt { char *name, *path; struct HashEnt *next; } HashEnt;
static HashEnt *hashtab[64];

static unsigned hh(const char *s)
{
    unsigned h = 0;
    while (*s) h = h * 31 + (unsigned char)*s++;
    return h & 63;
}

void hash_clear(void)
{
    int i;
    for (i = 0; i < 64; i++) {
        HashEnt *e = hashtab[i];
        while (e) {
            HashEnt *n = e->next;
            free(e->name);
            free(e->path);
            free(e);
            e = n;
        }
        hashtab[i] = NULL;
    }
}

static void hash_put(const char *name, const char *path)
{
    HashEnt *e = xmalloc(sizeof *e);
    e->name = xstrdup(name);
    e->path = xstrdup(path);
    e->next = hashtab[hh(name)];
    hashtab[hh(name)] = e;
}

static HashEnt *hash_get(const char *name)
{
    HashEnt *e;
    for (e = hashtab[hh(name)]; e; e = e->next)
        if (strcmp(e->name, name) == 0)
            return e;
    return NULL;
}

static void hash_del(const char *name)
{
    HashEnt **pp, *e;
    for (pp = &hashtab[hh(name)]; (e = *pp); pp = &e->next)
        if (strcmp(e->name, name) == 0) {
            *pp = e->next;
            free(e->name);
            free(e->path);
            free(e);
            return;
        }
}

void hash_print(void)
{
    int i;
    HashEnt *e;
    for (i = 0; i < 64; i++)
        for (e = hashtab[i]; e; e = e->next)
            outfmt(&out1, "%s\n", e->path);
}

static int is_exec_file(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode) && access(p, X_OK) == 0;
}

/* search PATH; returns stack string or NULL */
static char *path_search(const char *name, const char *path)
{
    const char *p = path ? path : "";
    StrBuf b;
    for (;;) {
        const char *e = strchr(p, ':');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        sb_init(&b);
        if (n)
            sb_add(&b, p, n);
        else
            sb_addc(&b, '.');
        sb_addc(&b, '/');
        sb_adds(&b, name);
        if (is_exec_file(b.s))
            return sb_stack(&b);
        sb_free(&b);
        if (!e)
            break;
        p = e + 1;
    }
    return NULL;
}

char *find_command(const char *name, const char *path, int *found_dir)
{
    char *r;
    HashEnt *h;
    (void)found_dir;
    if (!path) {
        h = hash_get(name);
        if (h) {
            if (is_exec_file(h->path))
                return ststrdup(h->path);
            hash_del(name);
        }
        r = path_search(name, var_get("PATH"));
        if (r && r[0] == '/')
            hash_put(name, r);
        return r;
    }
    return path_search(name, path);
}

int hash_add(const char *name)
{
    char *p;
    if (builtin_lookup(name) && (builtin_lookup(name)->flags & (BI_SPECIAL | BI_INTRINSIC)))
        return 0;
    if (func_lookup(name))
        return 0;
    hash_del(name);
    p = find_command(name, NULL, NULL);
    if (!p) {
        sh_warn("%s: not found", name);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* external commands                                                  */
/* ------------------------------------------------------------------ */

extern void reinit_for_script(const char *path, char **argv);
extern char *pending_script, **pending_script_argv;

static int looks_binary(const char *path)
{
    char buf[512];
    int fd = open(path, O_RDONLY);
    ssize_t n, i;
    if (fd < 0)
        return 0;
    n = read(fd, buf, sizeof buf);
    close(fd);
    for (i = 0; i < n; i++) {
        if (buf[i] == '\n')
            return 0;
        if (buf[i] == 0)
            return 1;
    }
    return 0;
}

/* exec argv[0] (searching PATH); does not return */
void exec_external(char **argv, char **envp, const char *path_override)
{
    char *path;
    int e;
    if (strchr(argv[0], '/'))
        path = argv[0];
    else {
        path = find_command(argv[0], path_override, NULL);
        if (!path) {
            sh_warn("%s: not found", argv[0]);
            flushall();
            _exit(127);
        }
    }
    execve(path, argv, envp);
    e = errno;
    if (e == ENOEXEC) {
        if (looks_binary(path)) {
            sh_warn("%s: cannot execute binary file", argv[0]);
            flushall();
            _exit(126);
        }
        /* run the file as a shell script in this (child) process */
        pending_script = xstrdup(path);
        {
            int n = 0, i;
            while (argv[n]) n++;
            pending_script_argv = xmalloc((size_t)(n + 1) * sizeof(char *));
            for (i = 0; i < n; i++)
                pending_script_argv[i] = xstrdup(argv[i]);
            pending_script_argv[n] = NULL;
        }
        raise_exc(EX_EXEC);
    }
    if (e == ENOENT || e == ENOTDIR) {
        sh_warn("%s: not found", argv[0]);
        flushall();
        _exit(127);
    }
    sh_warn("%s: %s", argv[0], strerror(e));
    flushall();
    _exit(126);
}

/* ------------------------------------------------------------------ */
/* subshells                                                          */
/* ------------------------------------------------------------------ */

void enter_subshell(int keep_traps)
{
    (void)keep_traps;
    in_subshell = 1;
    interactive = 0;
    opts[O_INTERACTIVE] = 0;
    reset_traps_for_subshell();
    jobs_forget_all();
    set_job_control_child();
    clear_redir_saves_keep_fds();
}

static pid_t do_fork(Job *j, int bg)
{
    pid_t pid;
    flushall();
    pid = fork();
    if (pid < 0)
        sh_error("cannot fork: %s", strerror(errno));
    if (pid == 0) {
        if (j && j->jobctl) {
            pid_t pg = j->pgid ? j->pgid : getpid();
            setpgid(0, pg);
            if (!bg && tty_fd >= 0)
                tcsetpgrp(tty_fd, pg);
        }
        signals_child_reset(bg && !(j && j->jobctl));
        if (bg && !(j && j->jobctl)) {
            /* stdin of async lists without job control: /dev/null */
            int fd = open("/dev/null", O_RDONLY);
            if (fd >= 0 && fd != 0) {
                dup2(fd, 0);
                close(fd);
            }
        }
        enter_subshell(0);
        return 0;
    }
    if (j) {
        if (j->jobctl) {
            if (!j->pgid)
                j->pgid = pid;
            setpgid(pid, j->pgid);
        }
        job_add_proc(j, pid);
    }
    return pid;
}

/* ------------------------------------------------------------------ */
/* tracing                                                            */
/* ------------------------------------------------------------------ */

static void xtrace(Word *assigns_exp_dummy, char **assigns, int nassign, char **argv)
{
    int i;
    char *ps4;
    (void)assigns_exp_dummy;
    if (!opts[O_XTRACE])
        return;
    ps4 = expand_prompt_params(var_get("PS4") ? var_get("PS4") : "");
    outs(&out2, ps4);
    for (i = 0; i < nassign; i++) {
        char *eq = strchr(assigns[i], '=');
        if (i) outc(&out2, ' ');
        outn(&out2, assigns[i], (size_t)(eq - assigns[i] + 1));
        outs(&out2, quote_for_reinput(eq + 1));
    }
    for (i = 0; argv && argv[i]; i++) {
        if (i || nassign) outc(&out2, ' ');
        outs(&out2, quote_for_reinput(argv[i]));
    }
    outc(&out2, '\n');
    outflush(&out2);
}

/* ------------------------------------------------------------------ */
/* simple commands                                                    */
/* ------------------------------------------------------------------ */

static int is_decl_name(const char *s)
{
    return strcmp(s, "export") == 0 || strcmp(s, "readonly") == 0;
}

static char *expand_assignment(Word *w)
{
    Word tmp = *w;
    char *eq = strchr(w->s, '=');
    char *val;
    StrBuf b;
    tmp.s = eq + 1;
    tmp.next = NULL;
    val = expand_word_str(&tmp, EXP_NOSPLIT | EXP_ASSIGN);
    sb_init(&b);
    sb_add(&b, w->s, (size_t)(eq - w->s + 1));
    sb_adds(&b, val);
    return sb_stack(&b);
}

static void set_assignments(char **as, int n, int flags, VarSave **save)
{
    int i;
    for (i = 0; i < n; i++) {
        char *eq = strchr(as[i], '=');
        char *name = ststrndup(as[i], (size_t)(eq - as[i]));
        if (save) {
            VarSave *s = var_save(name);
            s->next = *save;
            *save = s;
        }
        if (var_set(name, eq + 1, flags) < 0)
            sh_error("%s: is read only", name);
    }
}

static void check_readonly(char **as, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        char *eq = strchr(as[i], '=');
        char *name = ststrndup(as[i], (size_t)(eq - as[i]));
        Var *v = var_lookup(name);
        if (v && (v->flags & V_READONLY))
            sh_error("%s: is read only", name);
    }
}

static int call_function(Func *f, int argc, char **argv, char **as, int nas, Redir *redir)
{
    Params saved = posparams;
    VarSave *vs = NULL;
    volatile int depth = redir_depth();
    int saved_loopnest = loopnest;
    struct jmploc jl, *savedh = handler;
    int st;
    Node *body = f->body;
    if (setjmp(jl.loc)) {
        handler = savedh;
        undo_redirects(depth);
        params_free(&posparams);
        posparams = saved;
        var_restore(vs);
        funcnest--;
        loopnest = saved_loopnest;
        raise_exc(exception);
    }
    handler = &jl;
    set_assignments(as, nas, 0, &vs);
    if (redirect(redir, 1) < 0) {
        handler = savedh;
        undo_redirects(depth);
        var_restore(vs);
        return 1;
    }
    posparams.n = 0;
    posparams.v = NULL;
    posparams.malloced = 0;
    params_set(argv + 1, argc - 1);
    funcnest++;
    loopnest = 0;
    st = eval_tree(body, 0);
    if (evalskip == SKIPRETURN) {
        evalskip = 0;
        st = exitstatus;
    }
    funcnest--;
    loopnest = saved_loopnest;
    handler = savedh;
    params_free(&posparams);
    posparams = saved;
    undo_redirects(depth);
    var_restore(vs);
    return st;
}

int builtin_running;

static int run_builtin_cmd(const Builtin *b, int argc, char **argv, char **as, int nas,
                           Redir *redir, int via_command)
{
    volatile int depth = redir_depth();
    VarSave *volatile vs = NULL;
    struct jmploc jl, *savedh = handler;
    int st;
    int special = (b->flags & BI_SPECIAL) && !via_command;
    char *volatile savedname = commandname;
    if (special) {
        set_assignments(as, nas, b->fn == bltin_exec_fn() ? V_EXPORT : 0, NULL);
        if (b->fn == bltin_exec_fn() && argc == 1) {
            /* exec without a command: redirections affect the shell */
            if (redirect(redir, 0) < 0) {
                if (!interactive)
                    exitshell(1);
                return 1;
            }
            return 0;
        }
    } else {
        VarSave *tmp = NULL;
        set_assignments(as, nas, V_EXPORT, &tmp);
        vs = tmp;
    }
    if (redirect(redir, 1) < 0) {
        undo_redirects(depth);
        if (vs) var_restore(vs);
        if (special && !interactive)
            exitshell(1);
        return 1;
    }
    if (setjmp(jl.loc)) {
        handler = savedh;
        commandname = savedname;
        outflush(&out1);
        undo_redirects(depth);
        if (vs) var_restore(vs);
        if (exception == EX_ERROR && !special) {
            /* errors in regular built-ins do not abort the shell */
            return exitstatus ? exitstatus : 2;
        }
        raise_exc(exception);
    }
    handler = &jl;
    commandname = argv[0];
    st = run_builtin(b, argc, argv);
    commandname = savedname;
    if (outflush(&out1) < 0 && st == 0)
        st = 1;
    out1.err = 0;
    handler = savedh;
    undo_redirects(depth);
    if (vs) var_restore(vs);
    return st;
}

/* is this builtin associated with a PATH directory (non-intrinsic)? */
static int builtin_usable(const Builtin *b, const char *name, const char *path)
{
    if (b->flags & (BI_SPECIAL | BI_INTRINSIC))
        return 1;
    /* regular built-ins are found through the PATH search; a remembered
     * location is enough, as the file itself is never executed */
    if (!path && hash_get(name))
        return 1;
    return find_command(name, path, NULL) != NULL;
}

static int eval_simple(Node *n, int flags)
{
    ArgList args = { NULL, 0, 0 };
    char **as = NULL;
    int nas = 0, i, status;
    Word *w;
    int via_command = 0;
    const char *cmdpath = NULL;

    cmd_lineno = n->lineno;
    var_set_int("LINENO", n->lineno);
    cmdsub_status_set = 0;

    /* expand words: command name first, then arguments */
    w = n->u.simple.words;
    while (w && args.n == 0) {
        expand_word_full(w, &args);
        w = w->next;
    }
    if (args.n > 0 && w) {
        /* declaration utilities: export, readonly, command [-p] export ... */
        Word *decl_from = NULL;
        if (args.n == 1 && is_decl_name(args.v[0]))
            decl_from = w;
        else if (args.n == 1 && strcmp(args.v[0], "command") == 0) {
            Word *x = w;
            char *lit;
            while (x && (lit = word_literal(x)) && lit[0] == '-' && lit[1] && strcmp(lit, "--"))
                x = x->next;
            if (x && (lit = word_literal(x)) && strcmp(lit, "--") == 0)
                x = x->next;
            if (x && (lit = word_literal(x)) && is_decl_name(lit))
                decl_from = x->next;
        }
        for (; w; w = w->next) {
            if (decl_from == w)
                decl_from = (Word *)1;
            if (decl_from == (Word *)1 && is_assignment_word(w))
                arglist_add(&args, expand_assignment(w));
            else
                expand_word_full(w, &args);
        }
    }
    for (w = n->u.simple.assigns; w; w = w->next) {
        nas++;
    }
    if (nas && args.n > 0) {
        as = stalloc((size_t)nas * sizeof(char *));
        for (w = n->u.simple.assigns, i = 0; w; w = w->next, i++)
            as[i] = expand_assignment(w);
    }
    if (!args.v)
        arglist_add(&args, NULL), args.n = 0;

    if (args.n > 0)
        xtrace(NULL, as, nas, args.v);

    /* no command name: assignments are performed in order */
    if (args.n == 0) {
        int depth = redir_depth();
        int st;
        for (w = n->u.simple.assigns; w; w = w->next) {
            char *a = expand_assignment(w);
            xtrace(NULL, &a, 1, NULL);
            set_assignments(&a, 1, 0, NULL);
        }
        st = cmdsub_status_set ? cmdsub_status : 0;
        if (redirect(n->redir, 1) < 0)
            st = 1;
        undo_redirects(depth);
        free(args.v);
        return st;
    }

    /* "command" handled here so that it can suppress function lookup */
    {
        int argc = args.n;
        char **argv = args.v;
        const char *name = argv[0];
        const Builtin *b;
        Func *f;

        while (strcmp(name, "command") == 0) {
            int i = 1, pflag = 0, vflag = 0, bad = 0;
            while (i < argc && argv[i][0] == '-' && argv[i][1]) {
                const char *o;
                if (strcmp(argv[i], "--") == 0) {
                    i++;
                    break;
                }
                for (o = argv[i] + 1; *o; o++) {
                    if (*o == 'p') pflag = 1;
                    else if (*o == 'v' || *o == 'V') vflag = 1;
                    else bad = 1;
                }
                i++;
            }
            if (vflag || bad) {
                status = run_builtin_cmd(builtin_lookup("command"), argc, argv, as, nas,
                                         n->redir, 0);
                free(args.v);
                return status;
            }
            argv += i;
            argc -= i;
            via_command = 1;
            if (argc == 0) {
                free(args.v);
                return 0;
            }
            name = argv[0];
            cmdpath = pflag ? command_default_path() : NULL;
        }

        if (!strchr(name, '/')) {
            b = builtin_lookup(name);
            if (b && (b->flags & BI_SPECIAL)) {
                status = run_builtin_cmd(b, argc, argv, as, nas, n->redir, via_command);
                free(args.v);
                return status;
            }
            if (!via_command && (f = func_lookup(name))) {
                status = call_function(f, argc, argv, as, nas, n->redir);
                free(args.v);
                return status;
            }
            if (b && builtin_usable(b, name, cmdpath)) {
                check_readonly(as, nas);
                status = run_builtin_cmd(b, argc, argv, as, nas, n->redir, via_command);
                free(args.v);
                return status;
            }
        }

        /* external utility */
        check_readonly(as, nas);
        {
            char *path = strchr(name, '/') ? (char *)name : find_command(name, cmdpath, NULL);
            if (!path) {
                int depth = redir_depth();
                if (redirect(n->redir, 1) == 0)
                    sh_warn("%s: not found", name);
                undo_redirects(depth);
                free(args.v);
                return 127;
            }
            if (exec_in_place_ok(flags)) {
                char **env;
                if (redirect(n->redir, 0) < 0)
                    sh_exit_now(1);
                set_assignments(as, nas, V_EXPORT, NULL);
                env = var_environ();
                exec_external_path(path, argv, env);
            }
            {
                Job *j = job_new(1, 0);
                pid_t pid;
                if (j->jobctl)
                    j->cmd = node_text(n);
                pid = do_fork(j, 0);
                if (pid == 0) {
                    char **env;
                    if (redirect(n->redir, 0) < 0)
                        sh_exit_now(1);
                    set_assignments(as, nas, V_EXPORT, NULL);
                    env = var_environ();
                    exec_external_path(path, argv, env);
                }
                status = job_wait(j);
                free(args.v);
                return status;
            }
        }
    }
}

/* exec a resolved path keeping argv[0] as given */
void exec_external_path(const char *path, char **argv, char **envp)
{
    int e;
    execve(path, argv, envp);
    e = errno;
    if (e == ENOEXEC) {
        int n = 0, i;
        if (looks_binary(path)) {
            sh_warn("%s: cannot execute binary file", argv[0]);
            sh_exit_now(126);
        }
        pending_script = xstrdup(path);
        while (argv[n]) n++;
        pending_script_argv = xmalloc((size_t)(n + 1) * sizeof(char *));
        for (i = 0; i < n; i++)
            pending_script_argv[i] = xstrdup(argv[i]);
        pending_script_argv[n] = NULL;
        raise_exc(EX_EXEC);
    }
    if (e == ENOENT || e == ENOTDIR) {
        sh_warn("%s: not found", argv[0]);
        sh_exit_now(127);
    }
    sh_warn("%s: %s", argv[0], strerror(e));
    sh_exit_now(126);
}

/* ------------------------------------------------------------------ */
/* pipelines and lists                                                */
/* ------------------------------------------------------------------ */

extern char *node_text(Node *n);

static int eval_pipe(Node *n, int flags)
{
    int i, nc = n->u.pipe.n, prev = -1, pfd[2];
    Job *j = job_new(nc, 0);
    j->pipefail = opts[O_PIPEFAIL];
    j->cmd = node_text(n);
    for (i = 0; i < nc; i++) {
        pid_t pid;
        if (i < nc - 1 && pipe(pfd) < 0)
            sh_error("cannot create pipe: %s", strerror(errno));
        pid = do_fork(j, 0);
        if (pid == 0) {
            if (prev >= 0) {
                dup2(prev, 0);
                close(prev);
            }
            if (i < nc - 1) {
                close(pfd[0]);
                if (pfd[1] != 1) {
                    dup2(pfd[1], 1);
                    close(pfd[1]);
                }
            }
            eval_tree(n->u.pipe.cmds[i], EV_EXIT);
            exitshell(exitstatus);
        }
        if (prev >= 0)
            close(prev);
        if (i < nc - 1) {
            close(pfd[1]);
            prev = pfd[0];
        }
    }
    (void)flags;
    return job_wait(j);
}

static int eval_async(Node *n)
{
    Job *j = job_new(1, 1);
    pid_t pid;
    j->cmd = node_text(n->u.un.a);
    pid = do_fork(j, 1);
    if (pid == 0) {
        eval_tree(n->u.un.a, EV_EXIT);
        exitshell(exitstatus);
    }
    last_bgpid = j->jobctl ? j->pgid : pid;
    last_bgpid_set = 1;
    job_set_current(j);
    if (interactive && j->jobctl) {
        outfmt(&out2, "[%d] %ld\n", j->num, (long)pid);
        outflush(&out2);
    }
    return 0;
}

static int eval_subshell(Node *n, int flags)
{
    if (exec_in_place_ok(flags)) {
        if (redirect(n->redir, 0) < 0)
            sh_exit_now(1);
        eval_tree(n->u.un.a, EV_EXIT);
        exitshell(exitstatus);
    }
    {
        Job *j = job_new(1, 0);
        pid_t pid;
        j->cmd = node_text(n);
        pid = do_fork(j, 0);
        if (pid == 0) {
            if (redirect(n->redir, 0) < 0)
                sh_exit_now(1);
            eval_tree(n->u.un.a, EV_EXIT);
            exitshell(exitstatus);
        }
        return job_wait(j);
    }
}

/* ------------------------------------------------------------------ */
/* compound commands                                                  */
/* ------------------------------------------------------------------ */

static int loop_check(void)
{
    /* returns 1 to break out of the current loop */
    if (evalskip == SKIPBREAK) {
        if (--skipcount <= 0)
            evalskip = 0;
        return 1;
    }
    if (evalskip == SKIPCONT) {
        if (--skipcount <= 0) {
            evalskip = 0;
            return 0;
        }
        return 1;
    }
    return evalskip != 0;
}

static int eval_for(Node *n, int flags)
{
    ArgList items = { NULL, 0, 0 };
    int i, st = 0;
    if (n->u.nfor.has_in)
        expand_words(n->u.nfor.words, &items);
    else {
        for (i = 0; i < posparams.n; i++)
            arglist_add(&items, ststrdup(posparams.v[i]));
    }
    loopnest++;
    for (i = 0; i < items.n; i++) {
        if (var_set(n->u.nfor.var, items.v[i], 0) < 0) {
            loopnest--;
            free(items.v);
            sh_error("%s: is read only", n->u.nfor.var);
        }
        st = eval_tree(n->u.nfor.body, flags & ~EV_EXIT);
        if (evalskip && loop_check())
            break;
    }
    loopnest--;
    free(items.v);
    return st;
}

static int eval_while(Node *n, int flags, int until)
{
    int st = 0;
    loopnest++;
    for (;;) {
        int c = eval_tree(n->u.loop.cond, (flags & ~EV_EXIT) | EV_TESTED);
        if (evalskip) {
            if (loop_check()) break;
            continue;
        }
        if (until ? c == 0 : c != 0)
            break;
        st = eval_tree(n->u.loop.body, flags & ~EV_EXIT);
        if (evalskip && loop_check())
            break;
    }
    loopnest--;
    return st;
}

static int eval_case(Node *n, int flags)
{
    char *word = expand_word_str(n->u.ncase.word, EXP_NOSPLIT | EXP_TILDE);
    CaseItem *ci;
    for (ci = n->u.ncase.items; ci; ci = ci->next) {
        Word *p;
        for (p = ci->patterns; p; p = p->next) {
            Word one = *p;
            char *pat;
            one.next = NULL;
            pat = expand_word_str(&one, EXP_PATTERN | EXP_TILDE);
            if (pattern_match(pat, word)) {
                int st = 0;
                for (;;) {
                    st = ci->body ? eval_tree(ci->body, flags) : 0;
                    if (evalskip || !ci->fallthrough || !ci->next)
                        break;
                    ci = ci->next;
                }
                return st;
            }
        }
    }
    return 0;
}

static __attribute__((noinline)) int with_redirs(Node *n, int flags)
{
    int depth = redir_depth(), st;
    struct jmploc jl, *saved = handler;
    if (redirect(n->redir, 1) < 0) {
        undo_redirects(depth);
        return 1;
    }
    if (setjmp(jl.loc)) {
        handler = saved;
        undo_redirects(depth);
        raise_exc(exception);
    }
    handler = &jl;
    switch (n->type) {
    case N_BRACE: st = eval_tree(n->u.un.a, flags); break;
    case N_IF: case N_WHILE: case N_UNTIL: case N_FOR: case N_CASE: {
        Redir *r = n->redir;
        n->redir = NULL;
        st = eval_tree(n, flags);
        n->redir = r;
        break;
    }
    default: st = 0;
    }
    handler = saved;
    undo_redirects(depth);
    return st;
}

static int eval_tree1(Node *n, int flags);

int eval_tree(Node *n, int flags)
{
    int st;
    if (!(flags & EV_TESTED) || !n)
        return eval_tree1(n, flags);
    /* -e is ignored for everything executed in this context, including
     * function bodies, eval and dot scripts */
    {
        struct jmploc jl, *saved = handler;
        if (setjmp(jl.loc)) {
            handler = saved;
            errexit_suppressed--;
            raise_exc(exception);
        }
        handler = &jl;
        errexit_suppressed++;
        st = eval_tree1(n, flags);
        errexit_suppressed--;
        handler = saved;
    }
    return st;
}

static int eval_tree1(Node *n, int flags)
{
    int st = 0;
    if (!n)
        return exitstatus = 0;
    if (opts[O_NOEXEC] && !interactive)
        return 0;
    switch (n->type) {
    case N_LIST:
        eval_tree(n->u.bin.a, flags & ~EV_EXIT);
        if (evalskip)
            return exitstatus;
        if (gotsig)
            run_pending_traps();
        return eval_tree(n->u.bin.b, flags);
    case N_AND: case N_OR:
        st = eval_tree(n->u.bin.a, (flags & ~EV_EXIT) | EV_TESTED);
        if (evalskip)
            return exitstatus;
        if ((n->type == N_AND) == (st == 0))
            return eval_tree(n->u.bin.b, flags);
        return exitstatus = st;
    case N_NOT:
        st = eval_tree(n->u.un.a, (flags & ~EV_EXIT) | EV_TESTED);
        return exitstatus = !st;
    case N_ASYNC:
        st = eval_async(n);
        break;
    case N_SIMPLE: {
        StackMark m;
        setstackmark(&m);
        st = eval_simple(n, flags);
        popstackmark(&m);
        break;
    }
    case N_PIPE:
        st = eval_pipe(n, flags);
        break;
    case N_SUBSHELL:
        st = eval_subshell(n, flags);
        break;
    case N_BRACE:
        if (n->redir)
            st = with_redirs(n, flags);
        else
            st = eval_tree(n->u.un.a, flags);
        exitstatus = st;
        return st;
    case N_IF:
        if (n->redir) { st = with_redirs(n, flags); break; }
        {
            int c = eval_tree(n->u.nif.cond, (flags & ~EV_EXIT) | EV_TESTED);
            if (evalskip)
                return exitstatus;
            if (c == 0)
                st = eval_tree(n->u.nif.then, flags);
            else if (n->u.nif.els)
                st = eval_tree(n->u.nif.els, flags);
            else
                st = 0;
        }
        exitstatus = st;
        return st;
    case N_WHILE: case N_UNTIL:
        if (n->redir) { st = with_redirs(n, flags); break; }
        st = eval_while(n, flags, n->type == N_UNTIL);
        exitstatus = st;
        return st;
    case N_FOR:
        if (n->redir) { st = with_redirs(n, flags); break; }
        st = eval_for(n, flags);
        exitstatus = st;
        return st;
    case N_CASE:
        if (n->redir) { st = with_redirs(n, flags); break; }
        {
            StackMark m;
            setstackmark(&m);
            st = eval_case(n, flags);
            popstackmark(&m);
        }
        exitstatus = st;
        return st;
    case N_FUNCDEF:
        func_define(n->u.func.name, n->u.func.body);
        st = 0;
        break;
    }
    exitstatus = st;
    /* errexit: simple commands, pipelines and subshells */
    if (opts[O_ERREXIT] && st != 0 && !(flags & EV_TESTED) && !errexit_suppressed && !evalskip &&
        (n->type == N_SIMPLE || n->type == N_PIPE || n->type == N_SUBSHELL))
        exitshell(st);
    if (flags & EV_EXIT)
        exitshell(st);
    return st;
}

/* ------------------------------------------------------------------ */
/* eval, dot, scripts                                                 */
/* ------------------------------------------------------------------ */

extern void parser_reset(void);

/* parse and execute commands from the current top input until EOF */
static int run_input_commands(void)
{
    int eof = 0, st = 0, ran = 0;
    for (;;) {
        StackMark m;
        Node *n;
        setstackmark(&m);
        n = parse_command(&eof);
        if (eof) {
            popstackmark(&m);
            break;
        }
        if (n) {
            st = eval_tree(n, 0);
            ran = 1;
            if (gotsig)
                run_pending_traps();
        }
        popstackmark(&m);
        if (evalskip)
            break;
    }
    if (!ran)
        exitstatus = 0;
    return st;
}

void eval_string(const char *s, int flags)
{
    volatile int depth = input_depth();
    struct jmploc jl, *saved = handler;
    void *volatile ps = parser_push_state();
    volatile int saveline = parse_lineno;
    (void)flags;
    push_input_string(xstrdup(s), 1);
    if (setjmp(jl.loc)) {
        handler = saved;
        while (input_depth() > depth)
            pop_input();
        parser_pop_state(ps);
        parse_lineno = saveline;
        raise_exc(exception);
    }
    handler = &jl;
    run_input_commands();
    handler = saved;
    while (input_depth() > depth)
        pop_input();
    parser_pop_state(ps);
    parse_lineno = saveline;
}

int eval_string_status(const char *s)
{
    eval_string(s, 0);
    return exitstatus;
}

/* "." : returns -1 if the file could not be opened */
int run_file(const char *path, int must_exist)
{
    int fd = open(path, O_RDONLY), hfd;
    volatile int depth = input_depth();
    volatile int saveline = parse_lineno;
    struct jmploc jl, *saved = handler;
    void *volatile ps;
    (void)must_exist;
    if (fd < 0)
        return -1;
    hfd = move_high(fd);
    close(fd);
    if (hfd < 0)
        return -1;
    ps = parser_push_state();
    push_input_fd(hfd, 0);
    parse_lineno = 1;
    if (setjmp(jl.loc)) {
        handler = saved;
        while (input_depth() > depth)
            pop_input();
        close(hfd);
        parser_pop_state(ps);
        parse_lineno = saveline;
        dotnest--;
        raise_exc(exception);
    }
    handler = &jl;
    dotnest++;
    run_input_commands();
    dotnest--;
    if (evalskip == SKIPRETURN)
        evalskip = 0;
    handler = saved;
    while (input_depth() > depth)
        pop_input();
    close(hfd);
    parser_pop_state(ps);
    parse_lineno = saveline;
    return exitstatus;
}
