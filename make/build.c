/*
 * build.c - bringing targets up-to-date: rule selection (target rules,
 * inference rules, pattern rules, .DEFAULT, SCCS), out-of-date checks,
 * running commands (also in parallel with -j and a token pool shared
 * with sub-makes), -n -q -t, and the handling of signals.
 */
#include "make.h"

int opt_i, opt_k, opt_n, opt_p, opt_q, opt_r, opt_s, opt_t;
int max_jobs = 1;
int jobserver_r = -1, jobserver_w = -1;
int question_outdated;

static int stop_starting;     /* a non-ignored error without -k */
static int any_error;
static long commands_started;
static int include_mode;      /* making an include file: commands always run */
static int sigpipe[2] = { -1, -1 };
static volatile sig_atomic_t caught_signal;

/* ---------- jobs ---------- */

typedef struct Seg {
    Cmds *cmds;
    Auto a;
    char *q, *hat, *plus;
} Seg;

typedef struct Job {
    Node *n;
    Seg *segs;
    int nsegs, seg, line;
    pid_t pid;
    int ignore_this;      /* errors of the running line are ignored */
    int has_token;
    int executed;         /* at least one command really ran */
    int failed;
    struct Job *next;
} Job;
static Job *jobs;
static int njobs;

static int serial(void)
{
    return max_jobs <= 1 || not_parallel;
}

/* ---------- time ---------- */

static void stat_node(Node *n)
{
    struct stat st;
    if (n->stat_done)
        return;
    n->stat_done = 1;
    n->exists = 0;
    if (n->lib) {
        if (ar_member_time(n->lib, n->member, &n->mtime) == 0)
            n->exists = 1;
        return;
    }
    if (stat(n->name, &st) == 0) {
        n->exists = 1;
        n->mtime = st.st_mtim;
        n->dev = st.st_dev;
        n->ino = st.st_ino;
    }
}

/* is prerequisite p newer than target n (so that n is out-of-date)? */
static int is_newer(Node *p, Node *n)
{
    if (p == wait_node)
        return 0;
    stat_node(p);
    if (p->newer || !p->exists)
        return 1;
    if (!n->exists)
        return 1;
    if (p->lib || n->lib) {
        /* archive members have a resolution of one second */
        return p->mtime.tv_sec > n->mtime.tv_sec;
    }
    if (p->mtime.tv_sec != n->mtime.tv_sec)
        return p->mtime.tv_sec > n->mtime.tv_sec;
    /* identical times count as out-of-date, as the standard encourages,
       unless both names are the same file (a symbolic link to it) */
    if (p->mtime.tv_nsec == n->mtime.tv_nsec)
        return !(p->dev == n->dev && p->ino == n->ino);
    return p->mtime.tv_nsec > n->mtime.tv_nsec;
}

/* ---------- rule selection ---------- */

static int file_exists(const char *s)
{
    struct stat st;
    return stat(s, &st) == 0;
}

/* a prerequisite that exists or that a target rule can make */
static int can_use(const char *name)
{
    Node *n;
    if (file_exists(name))
        return 1;
    n = node_find(name);
    return n && n->is_target;
}

/* "dir/x.c~" -> "dir/s.x.c": a reference to an SCCS file */
static char *tilde_path(const char *cand)
{
    size_t l = strlen(cand);
    const char *sl;
    Buf b = { 0 };
    if (!l || cand[l - 1] != '~')
        return xstrdup(cand);
    sl = strrchr(cand, '/');
    if (sl)
        buf_add(&b, cand, (size_t)(sl - cand) + 1);
    buf_adds(&b, "s.");
    buf_add(&b, sl ? sl + 1 : cand, l - (size_t)(sl ? sl + 1 - cand : 0) - 1);
    return buf_take(&b);
}

static int ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a > b && strcmp(s + a - b, suf) == 0;
}

static Cmds *builtin_ar_cmds(void)
{
    static Cmds c;
    static char *v[1];
    if (!c.n) {
        v[0] = xstrdup("$(AR) $(ARFLAGS) $@ $%");
        c.v = v;
        c.n = 1;
        c.file = "(built-in)";
    }
    return &c;
}

/* inference: sets n->implicit, n->rcmds, n->stem; returns 1 if found */
static int find_inference(Node *n)
{
    Buf b = { 0 };
    if (n->lib) {
        /* lib(member.o) and the .s2.a rules */
        const char *m = n->member, *dot = strrchr(m, '.'), *sl = strrchr(m, '/');
        char *stem = (dot && (!sl || dot > sl)) ? xstrndup(m, (size_t)(dot - m)) : xstrdup(m);
        for (int i = 0; i < suffixes.n; i++) {
            Infer *r;
            buf_reset(&b);
            buf_adds(&b, suffixes.v[i]);
            buf_adds(&b, ".a");
            r = infer_get(b.s);
            if (r) {
                char *cand, *path;
                buf_reset(&b);
                buf_adds(&b, stem);
                buf_adds(&b, suffixes.v[i]);
                cand = buf_take(&b);
                path = tilde_path(cand);
                if (can_use(path)) {
                    n->implicit = node_get(path);
                    n->rcmds = r->cmds;
                    n->stem = stem;
                    free(cand);
                    free(path);
                    return 1;
                }
                free(cand);
                free(path);
            }
        }
        /* extension: put the member file itself in the library */
        if (can_use(m) && strcmp(m, n->lib) != 0) {
            n->implicit = node_get(m);
            n->rcmds = builtin_ar_cmds();
            n->dflt_ar = 1;
            n->stem = stem;
            buf_free(&b);
            return 1;
        }
        free(stem);
        buf_free(&b);
        return 0;
    }
    /* double-suffix rules */
    for (int i = 0; i < suffixes.n; i++) {
        const char *s1 = suffixes.v[i];
        size_t bl;
        if (!ends_with(n->name, s1))
            continue;
        bl = strlen(n->name) - strlen(s1);
        for (int k = 0; k < suffixes.n; k++) {
            Infer *r;
            char *cand, *path;
            buf_reset(&b);
            buf_adds(&b, suffixes.v[k]);
            buf_adds(&b, s1);
            r = infer_get(b.s);
            if (!r)
                continue;
            buf_reset(&b);
            buf_add(&b, n->name, bl);
            buf_adds(&b, suffixes.v[k]);
            cand = buf_take(&b);
            path = tilde_path(cand);
            if (strcmp(path, n->name) != 0 && can_use(path)) {
                n->implicit = node_get(path);
                n->rcmds = r->cmds;
                n->stem = xstrndup(n->name, bl);
                free(cand);
                free(path);
                buf_free(&b);
                return 1;
            }
            free(cand);
            free(path);
        }
    }
    /* single-suffix rules, for a target without a known suffix */
    for (int i = 0; i < suffixes.n; i++)
        if (ends_with(n->name, suffixes.v[i]))
            goto none;
    for (int k = 0; k < suffixes.n; k++) {
        Infer *r = infer_get(suffixes.v[k]);
        char *cand, *path;
        if (!r)
            continue;
        buf_reset(&b);
        buf_adds(&b, n->name);
        buf_adds(&b, suffixes.v[k]);
        cand = buf_take(&b);
        path = tilde_path(cand);
        if (can_use(path)) {
            n->implicit = node_get(path);
            n->rcmds = r->cmds;
            n->stem = xstrdup(n->name);
            free(cand);
            free(path);
            buf_free(&b);
            return 1;
        }
        free(cand);
        free(path);
    }
none:
    buf_free(&b);
    return 0;
}

/* pattern rules (extension); the prerequisites are added to *imp */
static int find_pattern(Node *n, Node ***imp, int *nimp)
{
    for (Pattern *p = patterns; p; p = p->next) {
        const char *pct = strchr(p->target, '%');
        size_t pl = (size_t)(pct - p->target), sl = strlen(pct + 1), l = strlen(n->name);
        char *stem;
        int ok = 1;
        if (!p->cmds || l < pl + sl || strncmp(n->name, p->target, pl) != 0 ||
            strcmp(n->name + l - sl, pct + 1) != 0)
            continue;
        stem = xstrndup(n->name + pl, l - pl - sl);
        if (!*stem) {
            free(stem);
            continue;
        }
        *nimp = 0;
        *imp = NULL;
        for (int i = 0; i < p->npre && ok; i++) {
            Buf b = { 0 };
            const char *q = strchr(p->pre[i], '%');
            if (q) {
                buf_add(&b, p->pre[i], (size_t)(q - p->pre[i]));
                buf_adds(&b, stem);
                buf_adds(&b, q + 1);
            } else
                buf_adds(&b, p->pre[i]);
            if (!can_use(b.s) || strcmp(b.s, n->name) == 0)
                ok = 0;
            else {
                *imp = xrealloc(*imp, (size_t)(*nimp + 1) * sizeof(Node *));
                (*imp)[(*nimp)++] = node_get(b.s);
            }
            buf_free(&b);
        }
        if (ok) {
            n->rcmds = p->cmds;
            n->stem = stem;
            return 1;
        }
        free(*imp);
        *imp = NULL;
        *nimp = 0;
        free(stem);
    }
    return 0;
}

/* the SCCS file for a target, if there is one */
static char *sccs_file(const char *name)
{
    const char *sl = strrchr(name, '/');
    const char *pd = getenv("PROJECTDIR");
    Buf b = { 0 };
    if (sl)
        buf_add(&b, name, (size_t)(sl - name) + 1);
    else if (pd && *pd) {
        if (pd[0] == '/')
            buf_adds(&b, pd);
        else {
            struct passwd *pw = getpwnam(pd);
            if (pw) {
                Buf t = { 0 };
                buf_adds(&t, pw->pw_dir);
                buf_adds(&t, "/src");
                if (!file_exists(t.s)) {
                    buf_reset(&t);
                    buf_adds(&t, pw->pw_dir);
                    buf_adds(&t, "/source");
                }
                if (file_exists(t.s))
                    buf_adds(&b, t.s);
                else
                    buf_adds(&b, pd);
                buf_free(&t);
            } else
                buf_adds(&b, pd);
        }
        buf_addc(&b, '/');
    }
    buf_adds(&b, "SCCS/s.");
    buf_adds(&b, sl ? sl + 1 : name);
    if (file_exists(b.s))
        return buf_take(&b);
    buf_free(&b);
    return NULL;
}

static void add_all(Node *n, Node *p)
{
    n->all = xrealloc(n->all, (size_t)(n->nall + 1) * sizeof(Node *));
    n->all[n->nall++] = p;
}

static void prepare(Node *n)
{
    Node **imp = NULL;
    int nimp = 0;
    stat_node(n);
    if (n->dcolon) {
        n->rcmds = NULL;
    } else if (n->cmds) {
        const char *nm = n->lib ? n->member : n->name;
        n->rcmds = n->cmds;
        for (int i = 0; i < suffixes.n; i++)
            if (ends_with(nm, suffixes.v[i])) {
                n->stem = xstrndup(nm, strlen(nm) - strlen(suffixes.v[i]));
                break;
            }
    } else if (!n->no_infer && find_pattern(n, &imp, &nimp)) {
        ;
    } else if (!n->no_infer && find_inference(n)) {
        imp = xmalloc(sizeof(Node *));
        imp[0] = n->implicit;
        nimp = 1;
    } else {
        char *sf = !n->lib && sccs_get_cmds ? sccs_file(n->name) : NULL;
        if (sf) {
            struct stat st;
            int writable = stat(n->name, &st) == 0 && (st.st_mode & 0222) == 0222;
            if (!writable) {
                n->rcmds = sccs_get_cmds;
                n->implicit = node_get(sf);
                n->implicit->no_infer = 1;
                imp = xmalloc(sizeof(Node *));
                imp[0] = n->implicit;
                nimp = 1;
            }
            free(sf);
        }
        if (!n->rcmds && default_cmds && !n->is_target) {
            n->rcmds = default_cmds;
            n->from_default = 1;
        }
    }
    for (int i = 0; i < nimp; i++) {
        if (!imp[i]->is_target)
            imp[i]->no_infer = 1;
        add_all(n, imp[i]);
    }
    if (nimp && !n->implicit)
        n->implicit = imp[0];
    free(imp);
    for (int i = 0; i < n->npre; i++)
        add_all(n, n->pre[i]);
}

/* ---------- internal macros ---------- */

static const char *list_name(Node *p)
{
    return p->lib ? p->member : p->name;
}

static void fill_auto(Node *n, Seg *s, Node **pre, int npre)
{
    Buf q = { 0 }, hat = { 0 }, plus = { 0 };
    for (int i = 0; i < npre; i++) {
        Node *p = pre[i];
        int dup = 0;
        if (p == wait_node)
            continue;
        if (plus.len)
            buf_addc(&plus, ' ');
        buf_adds(&plus, list_name(p));
        for (int k = 0; k < i; k++)
            if (pre[k] == p)
                dup = 1;
        if (dup)
            continue;
        if (hat.len)
            buf_addc(&hat, ' ');
        buf_adds(&hat, list_name(p));
        if (is_newer(p, n)) {
            if (q.len)
                buf_addc(&q, ' ');
            buf_adds(&q, list_name(p));
        }
    }
    s->q = buf_take(&q);
    s->hat = buf_take(&hat);
    s->plus = buf_take(&plus);
    s->a.q = s->q;
    s->a.hat = s->hat;
    s->a.plus = s->plus;
    s->a.at = n->lib ? n->lib : n->name;
    s->a.pct = n->member ? n->member : "";
    s->a.star = n->stem ? n->stem : "";
    if (n->from_default)
        s->a.lt = n->name;
    else if (n->implicit)
        s->a.lt = n->implicit->name;
    else {
        s->a.lt = "";
        for (int i = 0; i < npre; i++)
            if (pre[i] != wait_node) {
                s->a.lt = list_name(pre[i]);
                break;
            }
    }
}

/* ---------- running commands ---------- */

static int recursive_line(const char *raw)
{
    return strstr(raw, "$(MAKE)") || strstr(raw, "${MAKE}") || strstr(raw, "$(MAKE:") ||
           strstr(raw, "${MAKE:");
}

char *shell_path(void)
{
    Var *sv = var_get("SHELL");
    char *shell = sv ? (sv->delayed ? expand(sv->value) : xstrdup(sv->value)) : xstrdup("/bin/sh");
    if (!*shell) {
        free(shell);
        shell = xstrdup("/bin/sh");
    }
    return shell;
}

static pid_t spawn(const char *line, int ignore, int recursive)
{
    pid_t pid;
    char *shell = shell_path();
    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0) {
        diag("fork: %s", strerror(errno));
        free(shell);
        return -1;
    }
    if (pid == 0) {
        char **env = child_environ(recursive);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        if (recursive && jobserver_r >= 0) {
            fcntl(jobserver_r, F_SETFD, 0);
            fcntl(jobserver_w, F_SETFD, 0);
        }
        if (sigpipe[0] >= 0) {
            close(sigpipe[0]);
            close(sigpipe[1]);
        }
        if (ignore)
            execle(shell, shell, "-c", line, (char *)NULL, env);
        else
            execle(shell, shell, "-e", "-c", line, (char *)NULL, env);
        fprintf(stderr, "%s: %s: %s\n", progname, shell, strerror(errno));
        _exit(127);
    }
    free(shell);
    return pid;
}

static void touch_target(Node *n)
{
    if (!opt_s && !all_silent && !n->silent) {
        printf("touch %s\n", n->name);
        fflush(stdout);
    }
    commands_started++;
    if (n->lib) {
        if (ar_touch_member(n->lib, n->member) < 0)
            diag("cannot touch %s", n->name);
        return;
    }
    {
        int fd = open(n->name, O_WRONLY | O_CREAT, 0666);
        if (fd < 0 || futimens(fd, NULL) < 0)
            diag("cannot touch %s: %s", n->name, strerror(errno));
        if (fd >= 0)
            close(fd);
    }
}

static void finish_node(Node *n, int ok, int executed);

/* start the next command line of the job; returns 1 if a process runs */
static int job_next(Job *j)
{
    Node *n = j->n;
    for (;;) {
        Seg *s;
        const char *raw;
        char *line, *p;
        int silent = 0, ignore = 0, force = 0, recursive, run;
        if (j->seg >= j->nsegs)
            return 0;
        s = &j->segs[j->seg];
        if (j->line >= s->cmds->n) {
            j->seg++;
            j->line = 0;
            continue;
        }
        raw = s->cmds->v[j->line++];
        recursive = recursive_line(raw);
        cur_auto = &s->a;
        line = expand(raw);
        cur_auto = NULL;
        for (p = line; *p == '@' || *p == '-' || *p == '+' || *p == ' ' || *p == '\t'; p++) {
            if (*p == '@')
                silent = 1;
            else if (*p == '-')
                ignore = 1;
            else if (*p == '+')
                force = 1;
        }
        if (opt_i || all_ignore || n->ignore)
            ignore = 1;
        if (opt_s || all_silent || n->silent)
            silent = 1;
        if (include_mode)
            run = 1;
        else if (opt_n)
            run = force || recursive;
        else if (opt_q)
            run = force;
        else if (opt_t)
            run = force || recursive;
        else
            run = 1;
        if (opt_n && !include_mode && !opt_s && !all_silent && !n->silent)
            silent = 0;
        if (opt_q && !run) {
            free(line);
            continue;
        }
        if (opt_t && !run) {
            free(line);
            continue;
        }
        if (!silent) {
            fputs(p, stdout);
            fputc('\n', stdout);
            fflush(stdout);
        }
        commands_started++;
        if (!run) {
            free(line);
            continue;
        }
        if (!*p) {
            free(line);
            continue;
        }
        j->ignore_this = ignore;
        j->pid = spawn(p, ignore, recursive || force);
        free(line);
        if (j->pid < 0) {
            j->failed = 1;
            return 0;
        }
        j->executed = 1;
        return 1;
    }
}

static void release_token(Job *j)
{
    if (j->has_token && jobserver_w >= 0) {
        char c = '+';
        while (write(jobserver_w, &c, 1) < 0 && errno == EINTR)
            ;
    }
    j->has_token = 0;
}

static void job_done(Job *j)
{
    Job **pp;
    int ok = !j->failed;
    Node *n = j->n;
    for (pp = &jobs; *pp; pp = &(*pp)->next)
        if (*pp == j) {
            *pp = j->next;
            break;
        }
    njobs--;
    release_token(j);
    if (ok && opt_t && !include_mode && !n->phony)
        touch_target(n);
    if (opt_q && !include_mode)
        question_outdated = 1;
    finish_node(n, ok, j->executed);
    for (int i = 0; i < j->nsegs; i++) {
        free(j->segs[i].q);
        free(j->segs[i].hat);
        free(j->segs[i].plus);
    }
    free(j->segs);
    free(j);
}

/* a child has exited */
static void reap(pid_t pid, int status)
{
    Job *j;
    for (j = jobs; j; j = j->next)
        if (j->pid == pid)
            break;
    if (!j)
        return;
    j->pid = 0;
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        char what[64];
        if (WIFSIGNALED(status))
            snprintf(what, sizeof what, "%s", strsignal(WTERMSIG(status)));
        else
            snprintf(what, sizeof what, "Error %d", WEXITSTATUS(status));
        if (j->ignore_this) {
            if (!opt_s)
                diag("[%s] %s (ignored)", j->n->name, what);
        } else {
            fflush(stdout);
            fprintf(stderr, "%s: *** [%s] %s\n", progname, j->n->name, what);
            j->failed = 1;
            any_error = 1;
            if (!opt_k && !stop_starting) {
                stop_starting = 1;
                if (njobs > 1)
                    fprintf(stderr, "%s: *** Waiting for unfinished jobs....\n", progname);
            }
            job_done(j);
            return;
        }
    }
    if (!job_next(j))
        job_done(j);
}

static void handle_signal(void);

/* wait until something happens: a child exits, a token or a signal arrives */
static void wait_event(int want_token)
{
    for (;;) {
        int status;
        pid_t pid;
        struct pollfd pf[2];
        int npf = 1;
        if (caught_signal)
            handle_signal();
        pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            reap(pid, status);
            return;
        }
        if (njobs == 0 && !want_token)
            return;
        pf[0].fd = sigpipe[0];
        pf[0].events = POLLIN;
        if (want_token && jobserver_r >= 0) {
            pf[1].fd = jobserver_r;
            pf[1].events = POLLIN;
            npf = 2;
        }
        if (poll(pf, (nfds_t)npf, njobs ? -1 : 100) > 0) {
            if (pf[0].revents) {
                char buf[64];
                while (read(sigpipe[0], buf, sizeof buf) > 0)
                    ;
            }
            if (npf == 2 && pf[1].revents)
                return;
        }
    }
}

/* may another job start now?  takes a token when needed */
static int take_slot(int *token)
{
    *token = 0;
    if (njobs == 0)
        return 1;
    if (serial() || include_mode)
        return 0;
    if (jobserver_r < 0)
        return njobs < max_jobs;
    {
        char c;
        ssize_t r = read(jobserver_r, &c, 1);
        if (r == 1) {
            *token = 1;
            return 1;
        }
    }
    return 0;
}

static int start_job(Node *n, Seg *segs, int nsegs, int token)
{
    Job *j = xcalloc(1, sizeof *j);
    struct stat st;
    j->n = n;
    j->segs = segs;
    j->nsegs = nsegs;
    j->has_token = token;
    n->start_exists = stat(n->name, &st) == 0;
    if (n->start_exists)
        n->start_mtime = st.st_mtim;
    n->state = ST_RUN;
    j->next = jobs;
    jobs = j;
    njobs++;
    if (!job_next(j)) {
        job_done(j);
        return 0;
    }
    return 1;
}

/* ---------- updating ---------- */

enum { U_OK, U_FAIL, U_PENDING };

static void finish_node(Node *n, int ok, int executed)
{
    n->state = ST_DONE;
    n->ok = ok;
    if (!ok)
        return;
    n->made = 1;
    n->stat_done = 0;
    stat_node(n);
    if (!n->exists || !executed || n->phony)
        n->newer = 1;
}

static int no_rule(Node *n, Node *parent)
{
    fflush(stdout);
    if (parent)
        fprintf(stderr, "%s: *** No rule to make target '%s', needed by '%s'.%s\n", progname, n->name,
                parent->name, opt_k ? "" : "  Stop.");
    else
        fprintf(stderr, "%s: *** No rule to make target '%s'.%s\n", progname, n->name,
                opt_k ? "" : "  Stop.");
    any_error = 1;
    if (!opt_k)
        stop_starting = 1;
    n->state = ST_DONE;
    n->ok = 0;
    return U_FAIL;
}

static int update(Node *n, Node *parent)
{
    int pending = 0, failed = 0, outdated = 0;
    if (n->state == ST_DONE)
        return n->ok ? U_OK : U_FAIL;
    if (n->state == ST_RUN)
        return U_PENDING;
    if (n->visiting) {
        diag("Circular %s <- %s dependency dropped.", parent ? parent->name : "?", n->name);
        return U_OK;
    }
    if (n->state == ST_NEW) {
        prepare(n);
        n->state = ST_SCAN;
    }
    n->visiting = 1;
    for (int i = 0; i < n->nall; i++) {
        Node *p = n->all[i];
        int r;
        if (p == wait_node) {
            if (pending)
                break;
            continue;
        }
        r = update(p, n);
        if (r == U_PENDING) {
            pending = 1;
            if (serial())
                break;
        } else if (r == U_FAIL) {
            failed = 1;
            if (!opt_k)
                break;
        }
        if (stop_starting && !opt_k)
            break;
    }
    n->visiting = 0;
    if (pending)
        return U_PENDING;
    if (failed || (stop_starting && !opt_k)) {
        n->state = ST_DONE;
        n->ok = 0;
        return U_FAIL;
    }
    /* are we out-of-date? */
    stat_node(n);
    if (n->phony || !n->exists)
        outdated = 1;
    for (int i = 0; i < n->nall && !outdated; i++)
        if (is_newer(n->all[i], n))
            outdated = 1;
    if (n->dcolon) {
        /* :: rules: each runs if out-of-date with respect to its own prerequisites */
        Seg *segs = NULL;
        int nsegs = 0, token;
        for (DRule *d = n->drules; d; d = d->next) {
            int od = n->phony || !n->exists || d->npre == 0;
            for (int i = 0; i < d->npre && !od; i++)
                if (is_newer(d->pre[i], n))
                    od = 1;
            if (!od || !d->cmds)
                continue;
            segs = xrealloc(segs, (size_t)(nsegs + 1) * sizeof(Seg));
            memset(&segs[nsegs], 0, sizeof(Seg));
            segs[nsegs].cmds = d->cmds;
            fill_auto(n, &segs[nsegs], d->pre, d->npre);
            nsegs++;
        }
        if (!nsegs) {
            free(segs);
            finish_node(n, 1, 1);
            if (!outdated)
                n->made = 0;
            return U_OK;
        }
        if (stop_starting || !take_slot(&token)) {
            for (int i = 0; i < nsegs; i++) {
                free(segs[i].q);
                free(segs[i].hat);
                free(segs[i].plus);
            }
            free(segs);
            if (stop_starting) {
                n->state = ST_DONE;
                return U_FAIL;
            }
            return U_PENDING;
        }
        start_job(n, segs, nsegs, token);
        return n->state == ST_DONE ? (n->ok ? U_OK : U_FAIL) : U_PENDING;
    }
    if (!outdated) {
        n->state = ST_DONE;
        n->ok = 1;
        return U_OK;
    }
    if (n->rcmds) {
        Seg *seg;
        int token;
        if (stop_starting) {
            n->state = ST_DONE;
            n->ok = 0;
            return U_FAIL;
        }
        if (!take_slot(&token))
            return U_PENDING;
        if (opt_q && !include_mode && !n->rcmds->n) {
            question_outdated = 1;
        }
        seg = xcalloc(1, sizeof *seg);
        seg->cmds = n->rcmds;
        fill_auto(n, seg, n->all, n->nall);
        start_job(n, seg, 1, token);
        return n->state == ST_DONE ? (n->ok ? U_OK : U_FAIL) : U_PENDING;
    }
    /* no commands */
    if (!n->exists && !n->is_target && !n->nall) {
        if (include_mode && !parent)
            return U_FAIL;
        return no_rule(n, parent);
    }
    n->state = ST_DONE;
    n->ok = 1;
    n->made = 1;
    n->newer = 1;
    return U_OK;
}

/* ---------- signals ---------- */

static void on_signal(int sig)
{
    int e = errno;
    if (njobs == 0) {
        signal(sig, SIG_DFL);
        raise(sig);
    }
    caught_signal = sig;
    if (sigpipe[1] >= 0)
        (void)!write(sigpipe[1], "s", 1);
    errno = e;
}

static void on_chld(int sig)
{
    int e = errno;
    (void)sig;
    if (sigpipe[1] >= 0)
        (void)!write(sigpipe[1], "c", 1);
    errno = e;
}

static void handle_signal(void)
{
    int sig = caught_signal;
    for (Job *j = jobs; j; j = j->next)
        if (j->pid > 0)
            kill(j->pid, sig);
    for (Job *j = jobs; j; j = j->next) {
        int status;
        if (j->pid > 0)
            while (waitpid(j->pid, &status, 0) < 0 && errno == EINTR)
                ;
    }
    for (Job *j = jobs; j; j = j->next) {
        Node *n = j->n;
        struct stat st;
        if (n->phony || n->precious || all_precious || n->lib)
            continue;
        if (stat(n->name, &st) < 0 || S_ISDIR(st.st_mode))
            continue;
        if (!n->start_exists || st.st_mtim.tv_sec != n->start_mtime.tv_sec ||
            st.st_mtim.tv_nsec != n->start_mtime.tv_nsec) {
            fflush(stdout);
            fprintf(stderr, "%s: *** Deleting file '%s'\n", progname, n->name);
            unlink(n->name);
        }
    }
    for (Job *j = jobs; j; j = j->next)
        if (j->pid > 0)
            fprintf(stderr, "%s: *** [%s] %s\n", progname, j->n->name, strsignal(sig));
    signal(sig, SIG_DFL);
    raise(sig);
    _exit(128 + sig);
}

void setup_signals(void)
{
    static const int sigs[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM };
    struct sigaction sa;
    if (pipe(sigpipe) == 0) {
        fcntl(sigpipe[0], F_SETFL, O_NONBLOCK);
        fcntl(sigpipe[1], F_SETFL, O_NONBLOCK);
        fcntl(sigpipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(sigpipe[1], F_SETFD, FD_CLOEXEC);
    }
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_chld;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    if (opt_n || opt_p || opt_q)
        return;
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
        struct sigaction old;
        sigaction(sigs[i], NULL, &old);
        if (old.sa_handler == SIG_IGN)
            continue;
        sa.sa_handler = on_signal;
        sa.sa_flags = SA_RESTART;
        sigaction(sigs[i], &sa, NULL);
    }
}

/* ---------- goals ---------- */

static void drain_jobs(void)
{
    while (njobs > 0)
        wait_event(0);
}

int build_goals(Words *goals, int report_uptodate)
{
    Node **g = xmalloc((size_t)(goals->n + 1) * sizeof(Node *));
    long *before = xcalloc((size_t)goals->n + 1, sizeof(long));
    int *res = xcalloc((size_t)goals->n + 1, sizeof(int));
    for (int i = 0; i < goals->n; i++)
        g[i] = node_get(goals->v[i]);
    if (serial()) {
        for (int i = 0; i < goals->n; i++) {
            int r;
            before[i] = commands_started;
            for (;;) {
                r = update(g[i], NULL);
                if (r != U_PENDING)
                    break;
                wait_event(0);
            }
            res[i] = r;
            if (r == U_FAIL && !opt_k)
                break;
            if (r == U_OK && report_uptodate && commands_started == before[i] && !opt_q) {
                if ((g[i]->rcmds || g[i]->dcolon) && !(opt_t && g[i]->phony))
                    printf("%s: '%s' is up to date.\n", progname, g[i]->name);
                else
                    printf("%s: Nothing to be done for '%s'.\n", progname, g[i]->name);
            }
        }
    } else {
        long start = commands_started;
        int done;
        do {
            int want = 0;
            done = 1;
            for (int i = 0; i < goals->n; i++) {
                int r = update(g[i], NULL);
                res[i] = r;
                if (r == U_PENDING)
                    done = 0;
            }
            if (!done) {
                /* a token may let a waiting target start */
                want = !serial() && jobserver_r >= 0 && njobs > 0;
                wait_event(want);
            }
        } while (!done && !(stop_starting && njobs == 0));
        if (report_uptodate && commands_started == start && !opt_q)
            for (int i = 0; i < goals->n; i++)
                if (res[i] == U_OK) {
                    if (g[i]->rcmds || g[i]->dcolon)
                        printf("%s: '%s' is up to date.\n", progname, g[i]->name);
                    else
                        printf("%s: Nothing to be done for '%s'.\n", progname, g[i]->name);
                }
    }
    drain_jobs();
    if (opt_k)
        for (int i = 0; i < goals->n; i++)
            if (g[i]->state == ST_DONE && !g[i]->ok && any_error) {
                fflush(stdout);
                fprintf(stderr, "%s: Target '%s' not remade because of errors.\n", progname, g[i]->name);
            }
    fflush(stdout);
    free(g);
    free(before);
    free(res);
    if (any_error)
        return 2;
    if (opt_q && question_outdated)
        return 1;
    return 0;
}

int make_include(const char *name, int optional)
{
    Node *n = node_get(name);
    Words w = { 0 };
    int r;
    int was_q = question_outdated;
    /* only when there is a rule that could make it */
    prepare(n);
    if (!n->rcmds && !n->nall && !n->is_target && !n->dcolon) {
        node_reset_all();
        return 0;
    }
    node_reset_all();
    include_mode = 1;
    words_add(&w, xstrdup(name));
    {
        int save_k = opt_k;
        r = build_goals(&w, 0);
        opt_k = save_k;
    }
    include_mode = 0;
    question_outdated = was_q;
    words_free(&w);
    node_reset_all();
    if (r == 2) {
        if (!optional)
            exit(2);
        any_error = 0;
        stop_starting = 0;
    }
    return r;
}
