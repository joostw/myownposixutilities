/*
 * front.c - sccs: front end for the SCCS subsystem
 * (POSIX.1-2024, XCU sccs)
 *
 *   sccs [-r] [-d path] [-p path] command [options...] [operands...]
 *
 * The SCCS utilities are run in a child process (they are part of this
 * program); operands that are not options or option-arguments become
 * [path/][dir/]SCCS/s.name (SCCS is the -p path), unless the last
 * component already starts with "s.".  With no operands, the SCCS
 * directory itself is named.
 */

#include "sccs.h"

#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *dpath, *ppath = "SCCS";
static bool real_user;

struct cmd {
    const char *name;
    int (*fn)(int, char **);
    const char *argopts;        /* options with an argument */
    bool convert;               /* operands become SCCS file names */
};

static const struct cmd cmds[] = {
    { "admin", admin_main, "adefmr", true }, { "delta", delta_main, "gmr", true },
    { "get", get_main, "cirx", true }, { "prs", prs_main, "dc", true },
    { "rmdel", rmdel_main, "r", true }, { "sact", sact_main, "", true },
    { "unget", unget_main, "r", true }, { "val", val_main, "mry", true },
    { "what", what_main, "", false },
};

static const struct cmd *find(const char *name)
{
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++)
        if (strcmp(cmds[i].name, name) == 0)
            return &cmds[i];
    return NULL;
}

/* run a utility: its status (128 + signal when killed) */
static int run(const char *name, struct strs *args)
{
    const struct cmd *c = find(name);
    char **av = xmalloc((size_t)(args->n + 2) * sizeof *av);
    av[0] = (char *)name;
    for (int i = 0; i < args->n; i++)
        av[i + 1] = args->v[i];
    av[args->n + 1] = NULL;
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        err("fork: %s", strerror(errno));
        free(av);
        return 1;
    }
    if (pid == 0) {
        progname = name;
        status = 0;
        if (real_user || strcmp(name, "admin") == 0 || strcmp(name, "rmdel") == 0)
            if (setuid(getuid()) != 0)
                _exit(1);
        int r = c->fn(args->n + 1, av);
        fflush(stdout);
        _exit(r);
    }
    free(av);
    int st;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR)
            return 1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

/* the SCCS file name for an operand */
static char *convert(const char *op)
{
    const char *b = base_name(op);
    if (strncmp(b, "s.", 2) == 0)
        return xstrdup(op);
    size_t dl = (size_t)(b - op);
    size_t n = (dpath ? strlen(dpath) + 1 : 0) + dl + strlen(ppath) + strlen(b) + 4;
    char *r = xmalloc(n);
    snprintf(r, n, "%s%s%.*s%s/s.%s", dpath ? dpath : "", dpath ? "/" : "", (int)dl, op, ppath, b);
    return r;
}

/* the SCCS directory (of dir, or of the current directory) */
static char *sccs_dir(const char *dir)
{
    size_t n = (dpath ? strlen(dpath) + 1 : 0) + (dir ? strlen(dir) + 1 : 0) + strlen(ppath) + 1;
    char *r = xmalloc(n);
    snprintf(r, n, "%s%s%s%s%s", dpath ? dpath : "", dpath ? "/" : "", dir ? dir : "",
             dir ? "/" : "", ppath);
    return r;
}

/* split the arguments after the command into options (with their
   arguments) and operands, by the options that take an argument */
static void split(int argc, char **argv, const char *argopts, struct strs *opts, struct strs *ops)
{
    int i;
    for (i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (a[0] != '-' || !a[1]) {
            strs_add(ops, a);
            continue;
        }
        strs_add(opts, a);
        /* an option that takes an argument, last in the group */
        for (const char *p = a + 1; *p; p++)
            if (strchr(argopts, *p)) {
                if (!p[1] && i + 1 < argc)
                    strs_add(opts, argv[++i]);
                break;
            }
    }
    for (; i < argc; i++)
        strs_add(ops, argv[i]);
}

static void add_all(struct strs *to, const struct strs *from)
{
    for (int i = 0; i < from->n; i++)
        strs_add(to, from->v[i]);
}

/* a real utility, or one of the simple forms (edit, print) */
static int simple(const char *name, const char *extra, int argc, char **argv)
{
    const struct cmd *c = find(name);
    struct strs opts = { 0 }, ops = { 0 }, args = { 0 };
    split(argc, argv, c->argopts, &opts, &ops);
    if (extra)
        strs_add(&args, extra);
    add_all(&args, &opts);
    if (c->convert && strcmp(name, "val") != 0 && ops.n == 0) {
        char *d = sccs_dir(NULL);
        strs_add(&args, d);
        free(d);
    }
    for (int i = 0; i < ops.n; i++) {
        if (c->convert && strcmp(ops.v[i], "-") != 0) {
            char *s = convert(ops.v[i]);
            strs_add(&args, s);
            free(s);
        } else {
            strs_add(&args, ops.v[i]);
        }
    }
    int r = run(name, &args);
    strs_free(&opts), strs_free(&ops), strs_free(&args);
    return r;
}

/* the options a pseudo-utility passes on: with an argument (":"), or
   an optional one that is attached (";") */
static const char takes[] = "c:g:i:m:r:x:u:y;";

/* the options of opts (as split() left them) whose letters are in keep */
static void pick(const struct strs *opts, const char *keep, struct strs *out)
{
    for (int i = 0; i < opts->n; i++) {
        const char *a = opts->v[i];
        if (a[0] != '-')
            continue;
        for (const char *p = a + 1; *p; p++) {
            const char *t = strchr(takes, *p);
            bool k = strchr(keep, *p) != NULL;
            if (t) {
                const char *v = p[1] ? p + 1 : NULL;
                bool next = !v && t[1] == ':' && i + 1 < opts->n;
                if (k) {
                    const char *val = v ? v : next ? opts->v[i + 1] : "";
                    char *o = xasprintf("-%c%s", *p, val);
                    strs_add(out, o);
                    free(o);
                }
                if (next)
                    i++;
                break;
            }
            if (k) {
                char o[3] = { '-', *p, '\0' };
                strs_add(out, o);
            }
        }
    }
}

/* the names of the g-files being edited in an SCCS directory */
struct edit {
    char *gname;
    struct pentry e;
};

static void editing(const char *dir, bool skip_branches, const char *user, struct edit **out, int *n)
{
    char *sd = sccs_dir(dir);
    DIR *d = opendir(sd);
    if (!d) {
        free(sd);
        return;
    }
    struct strs names = { 0 };
    struct dirent *de;
    while ((de = readdir(d)))
        if (strncmp(de->d_name, "p.", 2) == 0 && de->d_name[2])
            strs_add(&names, de->d_name);
    closedir(d);
    for (int i = 0; i < names.n; i++)
        for (int j = i + 1; j < names.n; j++)
            if (strcmp(names.v[i], names.v[j]) > 0) {
                char *t = names.v[i];
                names.v[i] = names.v[j];
                names.v[j] = t;
            }
    for (int i = 0; i < names.n; i++) {
        char *p = xasprintf("%s/%s", sd, names.v[i]);
        struct pfile pf;
        if (pfile_read(p, &pf))
            for (int k = 0; k < pf.n; k++) {
                struct sid s;
                if (skip_branches && sid_parse(pf.e[k].newsid, &s) == 4)
                    continue;
                if (user && strcmp(user, pf.e[k].user) != 0)
                    continue;
                *out = xrealloc(*out, (size_t)(*n + 1) * sizeof **out);
                struct edit *e = &(*out)[(*n)++];
                char *g = xasprintf("%s%s%s", dir ? dir : "", dir ? "/" : "", names.v[i] + 2);
                e->gname = g;
                e->e.got = xstrdup(pf.e[k].got);
                e->e.newsid = xstrdup(pf.e[k].newsid);
                e->e.user = xstrdup(pf.e[k].user);
                e->e.date = xstrdup(pf.e[k].date);
                e->e.time = xstrdup(pf.e[k].time);
            }
        pfile_free(&pf);
        free(p);
    }
    strs_free(&names);
    free(sd);
}

/* info, check, tell */
static int edited(const char *what, int argc, char **argv)
{
    bool b = false;
    const char *user = NULL;
    struct opts o = { 1, 0, NULL };
    int c;
    argv--, argc++;             /* sopt starts at 1 */
    while ((c = sopt(argc, argv, "bu:U", &o)) != -1) {
        switch (c) {
        case 'b': b = true; break;
        case 'u': user = o.arg; break;
        case 'U': user = login_name(); break;
        default:
            fprintf(stderr, "usage: sccs %s [-b] [-u user | -U] [directory...]\n", what);
            return 2;
        }
    }
    struct edit *e = NULL;
    int n = 0;
    if (o.ind >= argc)
        editing(NULL, b, user, &e, &n);
    for (int i = o.ind; i < argc; i++)
        editing(argv[i], b, user, &e, &n);
    for (int i = 0; i < n; i++) {
        if (strcmp(what, "tell") == 0)
            printf("%s\n", e[i].gname);
        else
            printf("%12s: being edited: %s %s %s %s %s\n", e[i].gname, e[i].e.got, e[i].e.newsid,
                   e[i].e.user, e[i].e.date, e[i].e.time);
    }
    if (n == 0 && strcmp(what, "info") == 0)
        printf("Nothing being edited\n");
    int r = strcmp(what, "check") == 0 && n > 0 ? 1 : 0;
    for (int i = 0; i < n; i++) {
        free(e[i].gname);
        free(e[i].e.got), free(e[i].e.newsid), free(e[i].e.user), free(e[i].e.date), free(e[i].e.time);
    }
    free(e);
    return r;
}

static int clean(int argc, char **argv)
{
    bool b = false;
    struct opts o = { 1, 0, NULL };
    int c;
    argv--, argc++;
    while ((c = sopt(argc, argv, "b", &o)) != -1) {
        if (c != 'b') {
            fprintf(stderr, "usage: sccs clean [-b] [directory...]\n");
            return 2;
        }
        b = true;
    }
    int r = 0;
    int nd = argc - o.ind;
    for (int k = 0; k < (nd ? nd : 1); k++) {
        const char *dir = nd ? argv[o.ind + k] : NULL;
        struct edit *e = NULL;
        int n = 0;
        editing(dir, b, NULL, &e, &n);
        char *sd = sccs_dir(dir);
        DIR *d = opendir(sd);
        if (!d) {
            err("%s: %s", sd, strerror(errno));
            r = 1;
            free(sd);
            continue;
        }
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strncmp(de->d_name, "s.", 2) != 0 || !de->d_name[2])
                continue;
            char *g = xasprintf("%s%s%s", dir ? dir : "", dir ? "/" : "", de->d_name + 2);
            bool busy = false;
            for (int i = 0; i < n; i++)
                if (strcmp(e[i].gname, g) == 0)
                    busy = true;
            struct stat st;
            if (!busy && lstat(g, &st) == 0 && S_ISREG(st.st_mode) && unlink(g) != 0) {
                err("%s: %s", g, strerror(errno));
                r = 1;
            }
            free(g);
        }
        closedir(d);
        free(sd);
        for (int i = 0; i < n; i++) {
            free(e[i].gname);
            free(e[i].e.got), free(e[i].e.newsid), free(e[i].e.user), free(e[i].e.date), free(e[i].e.time);
        }
        free(e);
    }
    return r;
}

/* delget, deledit */
static int delget(bool edit, int argc, char **argv)
{
    struct strs opts = { 0 }, ops = { 0 }, dopts = { 0 }, gopts = { 0 };
    split(argc, argv, "mrcixg", &opts, &ops);
    pick(&opts, "mprsy", &dopts);
    pick(&opts, "bceiklsx", &gopts);
    if (edit)
        strs_add(&gopts, "-e");
    int r = 0;
    for (int i = 0; i < ops.n; i++) {
        char *s = convert(ops.v[i]);
        struct strs a = { 0 };
        add_all(&a, &dopts);
        strs_add(&a, s);
        int k = run("delta", &a);
        strs_free(&a);
        if (k == 0) {
            add_all(&a, &gopts);
            strs_add(&a, s);
            k = run("get", &a);
            strs_free(&a);
        }
        if (k)
            r = k;
        free(s);
    }
    strs_free(&opts), strs_free(&ops), strs_free(&dopts), strs_free(&gopts);
    return r;
}

/* get of an SCCS file, in the directory of the g-file */
static int get_in(const char *op)
{
    const char *b = base_name(op);
    size_t dl = (size_t)(b - op);
    pid_t pid = fork();
    if (pid == 0) {
        char *full = convert(op);
        if (dl) {
            /* the SCCS file relative to the new directory, unless -d */
            char *d = xstrndup(op, dl);
            if (chdir(d) != 0)
                _exit(1);
            if (!dpath) {
                free(full);
                full = xasprintf("%s/s.%s", ppath, b);
            }
        }
        struct strs g = { 0 };
        strs_add(&g, full);
        _exit(run("get", &g));
    }
    int st;
    if (pid < 0 || waitpid(pid, &st, 0) < 0)
        return 1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

static int create(int argc, char **argv)
{
    struct strs opts = { 0 }, ops = { 0 }, here = { 0 };
    split(argc, argv, "adefmr", &opts, &ops);
    int r = 0;
    for (int i = 0; i < ops.n; i++) {
        char *s = convert(ops.v[i]);
        struct strs a = { 0 };
        char *iopt = xasprintf("-i%s", ops.v[i]);
        strs_add(&a, iopt);
        free(iopt);
        add_all(&a, &opts);
        strs_add(&a, s);
        int k = run("admin", &a);
        strs_free(&a);
        if (k == 0) {
            /* the original file becomes ,name */
            const char *b = base_name(ops.v[i]);
            size_t dl = (size_t)(b - ops.v[i]);
            char *comma = xasprintf("%.*s,%s", (int)dl, ops.v[i], b);
            if (rename(ops.v[i], comma) != 0) {
                err("%s: %s", ops.v[i], strerror(errno));
                k = 1;
            } else if (dl) {
                k = get_in(ops.v[i]);
            } else {
                strs_add(&here, s);
            }
            free(comma);
        }
        if (k)
            r = k;
        free(s);
    }
    /* then the g-files, by one get (as CSSC's sccs does) */
    if (here.n) {
        int k = run("get", &here);
        if (k)
            r = k;
    }
    strs_free(&opts), strs_free(&ops), strs_free(&here);
    return r;
}

static int diffs(int argc, char **argv)
{
    struct strs opts = { 0 }, ops = { 0 }, gopts = { 0 }, dopts = { 0 };
    split(argc, argv, "rcix", &opts, &ops);
    pick(&opts, "rcixt", &gopts);
    pick(&opts, "lsefhbuC", &dopts);
    for (int i = 0; i < dopts.n; i++)
        if (strcmp(dopts.v[i], "-C") == 0)
            dopts.v[i][1] = 'c';
    int r = 0;
    for (int i = 0; i < ops.n; i++) {
        char *s = convert(ops.v[i]);
        printf("\n------- %s -------\n", ops.v[i]);
        fflush(stdout);
        int fd[2];
        if (pipe(fd) != 0) {
            err("pipe: %s", strerror(errno));
            free(s);
            r = 1;
            continue;
        }
        pid_t g = fork();
        if (g == 0) {
            close(fd[0]);
            dup2(fd[1], 1);
            close(fd[1]);
            struct strs a = { 0 };
            strs_add(&a, "-s");
            strs_add(&a, "-p");
            strs_add(&a, "-k");
            add_all(&a, &gopts);
            strs_add(&a, s);
            _exit(run("get", &a));
        }
        pid_t d = fork();
        if (d == 0) {
            close(fd[1]);
            dup2(fd[0], 0);
            close(fd[0]);
            char **av = xmalloc((size_t)(dopts.n + 4) * sizeof *av);
            int k = 0;
            av[k++] = "diff";
            for (int j = 0; j < dopts.n; j++)
                av[k++] = dopts.v[j];
            av[k++] = "-";
            av[k++] = ops.v[i];
            av[k] = NULL;
            execvp("diff", av);
            err("diff: %s", strerror(errno));
            _exit(127);
        }
        close(fd[0]);
        close(fd[1]);
        int gs, ds;
        waitpid(g, &gs, 0);
        waitpid(d, &ds, 0);
        if (!WIFEXITED(gs) || WEXITSTATUS(gs))
            r = 1;
        else if (!WIFEXITED(ds) || WEXITSTATUS(ds) > 1)
            r = 1;
        free(s);
    }
    strs_free(&opts), strs_free(&ops), strs_free(&gopts), strs_free(&dopts);
    return r;
}

static int fix(int argc, char **argv)
{
    struct strs opts = { 0 }, ops = { 0 }, ropt = { 0 };
    split(argc, argv, "r", &opts, &ops);
    pick(&opts, "r", &ropt);
    if (ropt.n == 0) {
        fprintf(stderr, "usage: sccs fix -r SID file...\n");
        strs_free(&opts), strs_free(&ops);
        return 2;
    }
    const char *sidarg = ropt.v[ropt.n - 1];
    if (strcmp(sidarg, "-r") == 0)
        sidarg = "";
    else if (strncmp(sidarg, "-r", 2) == 0)
        sidarg += 2;
    int r = 0;
    for (int i = 0; i < ops.n; i++) {
        char *s = convert(ops.v[i]);
        struct sfile sf;
        struct sid sid;
        struct delta *d;
        char pred[64] = "";
        int saved = status;
        if (!sfile_read(s, &sf, NULL)) {
            r = 1;
            free(s);
            continue;
        }
        status = saved;
        if (!sid_parse(sidarg, &sid) || !(d = by_sid(&sf, &sid)) || !d->pred) {
            err("%s: cannot fix %s", s, sidarg);
            sfile_free(&sf);
            free(s);
            r = 1;
            continue;
        }
        sid_str(&by_serial(&sf, d->pred)->sid, pred);
        sfile_free(&sf);
        char ra[80], rp[80];
        snprintf(ra, sizeof ra, "-r%s", sidarg);
        snprintf(rp, sizeof rp, "-r%s", pred);
        struct strs a = { 0 };
        strs_add(&a, "-k"), strs_add(&a, ra), strs_add(&a, s);
        int k = run("get", &a);
        strs_free(&a);
        if (k == 0) {
            strs_add(&a, ra), strs_add(&a, s);
            k = run("rmdel", &a);
            strs_free(&a);
        }
        if (k == 0) {
            strs_add(&a, "-e"), strs_add(&a, "-g"), strs_add(&a, rp), strs_add(&a, s);
            k = run("get", &a);
            strs_free(&a);
        }
        if (k)
            r = k;
        free(s);
    }
    strs_free(&opts), strs_free(&ops), strs_free(&ropt);
    return r;
}

static int unedit(int argc, char **argv)
{
    struct strs opts = { 0 }, ops = { 0 };
    split(argc, argv, "", &opts, &ops);
    int r = 0;
    for (int i = 0; i < ops.n; i++) {
        char *s = convert(ops.v[i]);
        char *p = aux_name(s, 'p');
        struct pfile pf;
        bool mine = false;
        if (pfile_read(p, &pf))
            for (int k = 0; k < pf.n; k++)
                if (strcmp(pf.e[k].user, login_name()) == 0)
                    mine = true;
        pfile_free(&pf);
        free(p);
        if (!mine) {
            printf("%12s: not being edited\n", ops.v[i]);
            free(s);
            continue;
        }
        struct strs a = { 0 };
        strs_add(&a, "-s"), strs_add(&a, s);
        int k = run("unget", &a);
        strs_free(&a);
        if (k == 0) {
            printf("%12s: removed\n", ops.v[i]);
            strs_add(&a, s);
            k = run("get", &a);
            strs_free(&a);
        }
        if (k)
            r = k;
        free(s);
    }
    strs_free(&opts), strs_free(&ops);
    return r;
}

static void usage(void)
{
    fprintf(stderr, "usage: sccs [-r] [-d path] [-p path] command [options...] [operands...]\n");
    exit(2);
}

int sccs_main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (strcmp(a, "-r") == 0) {
            real_user = true;
        } else if (a[1] == 'd' || a[1] == 'p') {
            const char *v = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : NULL);
            if (!v)
                usage();
            if (a[1] == 'd')
                dpath = v;
            else
                ppath = v;
        } else {
            fprintf(stderr, "sccs: unknown option %s\n", a);
            usage();
        }
    }
    if (i >= argc)
        usage();
    if (!dpath) {
        /* PROJECTDIR: a directory, or the name of a user whose src (or
           source) directory is meant */
        const char *pd = getenv("PROJECTDIR");
        if (pd && *pd) {
            if (pd[0] == '/') {
                dpath = pd;
            } else {
                struct passwd *pw = getpwnam(pd);
                if (!pw) {
                    err("PROJECTDIR: %s: no such user", pd);
                    return 1;
                }
                static char home[4096];
                struct stat st;
                snprintf(home, sizeof home, "%s/src", pw->pw_dir);
                if (stat(home, &st) != 0)
                    snprintf(home, sizeof home, "%s/source", pw->pw_dir);
                dpath = home;
            }
        }
    }
    if (real_user && setuid(getuid()) != 0) {
        err("setuid: %s", strerror(errno));
        return 1;
    }
    const char *cmd = argv[i];
    int ac = argc - i - 1;
    char **av = argv + i + 1;
    if (find(cmd))
        return simple(cmd, NULL, ac, av);
    if (strcmp(cmd, "edit") == 0)
        return simple("get", "-e", ac, av);
    if (strcmp(cmd, "print") == 0)
        return simple("prs", NULL, ac, av);
    if (strcmp(cmd, "delget") == 0 || strcmp(cmd, "deledit") == 0)
        return delget(strcmp(cmd, "deledit") == 0, ac, av);
    if (strcmp(cmd, "create") == 0)
        return create(ac, av);
    if (strcmp(cmd, "diffs") == 0)
        return diffs(ac, av);
    if (strcmp(cmd, "fix") == 0)
        return fix(ac, av);
    if (strcmp(cmd, "unedit") == 0)
        return unedit(ac, av);
    if (strcmp(cmd, "info") == 0 || strcmp(cmd, "check") == 0 || strcmp(cmd, "tell") == 0) {
        if (setuid(getuid()) != 0)
            return 1;
        return edited(cmd, ac, av);
    }
    if (strcmp(cmd, "clean") == 0) {
        if (setuid(getuid()) != 0)
            return 1;
        return clean(ac, av);
    }
    fprintf(stderr, "sccs: unknown command: %s\n", cmd);
    return 2;
}
