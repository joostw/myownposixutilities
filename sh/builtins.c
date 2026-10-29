/*
 * builtins.c - special built-ins (2.15) and intrinsic utilities (1.7).
 */
#include "sh.h"
#include <sys/times.h>
#include <sys/resource.h>

int bopt_ind;
char *bopt_arg;
static int bopt_sub;

/* XBD 12.2 option parsing for built-ins; returns option char, -1 at end,
 * '?' after reporting an unknown option or missing argument */
int bgetopt(int argc, char **argv, const char *optstr)
{
    char *a;
    const char *o;
    int c;
    if (bopt_sub == 0) {
        if (bopt_ind >= argc)
            return -1;
        a = argv[bopt_ind];
        if (a[0] != '-' || a[1] == 0)
            return -1;
        if (strcmp(a, "--") == 0) {
            bopt_ind++;
            return -1;
        }
        bopt_sub = 1;
    }
    a = argv[bopt_ind];
    c = (unsigned char)a[bopt_sub++];
    o = strchr(optstr, c);
    if (!o || c == ':') {
        sh_warn("-%c: invalid option", c);
        if (!a[bopt_sub]) { bopt_ind++; bopt_sub = 0; }
        return '?';
    }
    if (o[1] == ':') {
        if (a[bopt_sub])
            bopt_arg = a + bopt_sub;
        else if (bopt_ind + 1 < argc)
            bopt_arg = argv[++bopt_ind];
        else {
            sh_warn("-%c: option requires an argument", c);
            bopt_ind++;
            bopt_sub = 0;
            return '?';
        }
        bopt_ind++;
        bopt_sub = 0;
        return c;
    }
    if (!a[bopt_sub]) {
        bopt_ind++;
        bopt_sub = 0;
    }
    return c;
}

/* ------------------------------------------------------------------ */
/* special built-ins                                                  */
/* ------------------------------------------------------------------ */

static int bi_colon(int argc, char **argv) { (void)argc; (void)argv; return 0; }

static int loop_count(int argc, char **argv)
{
    long n = 1;
    if (argc > 1) {
        int ok;
        n = parse_long(argv[1], &ok);
        if (!ok || n < 1)
            sh_error("%s: illegal number", argv[1]);
    }
    if (n > loopnest)
        n = loopnest;
    return (int)n;
}

static int bi_break(int argc, char **argv)
{
    int n = loop_count(argc, argv);
    if (n > 0) {
        evalskip = SKIPBREAK;
        skipcount = n;
    }
    return 0;
}

static int bi_continue(int argc, char **argv)
{
    int n = loop_count(argc, argv);
    if (n > 0) {
        evalskip = SKIPCONT;
        skipcount = n;
    }
    return 0;
}

static char *find_dot_file(const char *name)
{
    const char *p = var_get("PATH");
    StrBuf b;
    if (!p) p = "";
    for (;;) {
        const char *e = strchr(p, ':');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        struct stat st;
        sb_init(&b);
        sb_add(&b, n ? p : ".", n ? n : 1);
        sb_addc(&b, '/');
        sb_adds(&b, name);
        if (stat(b.s, &st) == 0 && S_ISREG(st.st_mode) && access(b.s, R_OK) == 0)
            return sb_stack(&b);
        sb_free(&b);
        if (!e)
            break;
        p = e + 1;
    }
    return NULL;
}

static int bi_dot(int argc, char **argv)
{
    char *path;
    int st;
    Params saved;
    int setparams = 0;
    bopt_ind = 1;
    if (bgetopt(argc, argv, "") == '?')
        sh_error("usage: . file");
    if (bopt_ind >= argc)
        sh_error("usage: . file");
    path = argv[bopt_ind];
    if (!strchr(path, '/')) {
        char *f = find_dot_file(path);
        if (!f)
            sh_error("%s: not found", path);
        path = f;
    }
    if (bopt_ind + 1 < argc) {
        saved = posparams;
        posparams.n = 0;
        posparams.v = NULL;
        posparams.malloced = 0;
        params_set(argv + bopt_ind + 1, argc - bopt_ind - 1);
        setparams = 1;
    }
    {
        char *savedname = commandname;
        commandname = NULL;
        st = run_file(path, 1);
        commandname = savedname;
    }
    if (setparams) {
        params_free(&posparams);
        posparams = saved;
    }
    if (st < 0)
        sh_error("%s: cannot open: %s", argv[bopt_ind], strerror(errno));
    return st;
}

static int bi_eval(int argc, char **argv)
{
    StrBuf b;
    int i;
    char *s;
    char *savedname = commandname;
    if (argc < 2)
        return 0;
    sb_init(&b);
    for (i = 1; i < argc; i++) {
        if (i > 1) sb_addc(&b, ' ');
        sb_adds(&b, argv[i]);
    }
    s = sb_take(&b);
    commandname = NULL;
    eval_string(s, 0);
    commandname = savedname;
    free(s);
    return exitstatus;
}

static int bi_exec(int argc, char **argv)
{
    char **env;
    if (argc < 2)
        return 0;
    {
        int i = 1;
        if (strcmp(argv[1], "--") == 0)
            i = 2;
        if (i >= argc)
            return 0;
        flushall();
        env = var_environ();
        set_job_control(0);
        signals_child_reset(0);
        {
            char *path = strchr(argv[i], '/') ? argv[i] : find_command(argv[i], NULL, NULL);
            if (!path) {
                sh_warn("%s: not found", argv[i]);
                if (!interactive)
                    sh_exit_now(127);
                return 127;
            }
            execve(path, argv + i, env);
            if (errno == ENOEXEC) {
                extern void exec_external_path(const char *path, char **argv, char **envp);
                exec_external_path(path, argv + i, env);
            }
            sh_warn("%s: %s", argv[i], strerror(errno));
            if (!interactive)
                sh_exit_now(errno == ENOENT ? 127 : 126);
            return errno == ENOENT ? 127 : 126;
        }
    }
}

static int bi_exit(int argc, char **argv)
{
    int st = in_trap ? trap_status_before : exitstatus;
    if (argc > 1) {
        int ok;
        long n = parse_long(argv[1], &ok);
        if (!ok || n < 0)
            sh_error("%s: illegal number", argv[1]);
        st = (int)n;
    }
    exitshell(st);
}

static int declare(int argc, char **argv, int flag, const char *label)
{
    int i, ret = 0, c;
    int print = 0;
    bopt_ind = 1;
    while ((c = bgetopt(argc, argv, "p")) != -1) {
        if (c == '?')
            sh_error("usage: %s [-p] [name[=value]...]", label);
        print = 1;
    }
    if (bopt_ind >= argc) {
        (void)print;
        var_list(&out1, flag, label);
        return 0;
    }
    for (i = bopt_ind; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        char *name = eq ? ststrndup(argv[i], (size_t)(eq - argv[i])) : argv[i];
        if (!is_name(name))
            sh_error("%s: bad variable name", name);
        if (eq) {
            if (var_set(name, eq + 1, flag) < 0)
                sh_error("%s: is read only", name);
        } else {
            Var *v = var_lookup(name);
            if (!v) {
                var_set(name, NULL, flag);
            } else
                v->flags |= flag;
        }
    }
    return ret;
}

static int bi_export(int argc, char **argv) { return declare(argc, argv, V_EXPORT, "export"); }
static int bi_readonly(int argc, char **argv) { return declare(argc, argv, V_READONLY, "readonly"); }

static int bi_return(int argc, char **argv)
{
    int st = in_trap ? trap_status_before : exitstatus;
    if (argc > 1) {
        int ok;
        long n = parse_long(argv[1], &ok);
        if (!ok || n < 0)
            sh_error("%s: illegal number", argv[1]);
        st = (int)(n & 0xff);
    }
    if (funcnest > 0 || dotnest > 0)
        evalskip = SKIPRETURN;
    else if (!interactive)
        exitshell(st);
    return st;
}

static void print_options(int reinput)
{
    int i;
    for (i = 0; i < NOPTS; i++) {
        if (i == O_INTERACTIVE || i == O_STDIN || i == O_CMDSTR)
            continue;
        if (reinput)
            outfmt(&out1, "set %co %s\n", opts[i] ? '-' : '+', optdesc[i].name);
        else
            outfmt(&out1, "%-16s%s\n", optdesc[i].name, opts[i] ? "on" : "off");
    }
}

/* shared by "set" and invocation: returns index of first operand */
int process_set_options(int argc, char **argv, int *setargs, int invocation)
{
    int i = 1;
    *setargs = 0;
    for (; i < argc; i++) {
        char *a = argv[i];
        int on;
        if (strcmp(a, "--") == 0) {
            *setargs = 1;
            return i + 1;
        }
        if ((a[0] != '-' && a[0] != '+') || a[1] == 0) {
            if (strcmp(a, "-") == 0 && !invocation) {
                opts[O_XTRACE] = opts[O_VERBOSE] = 0;
                *setargs = 1;
                return i + 1;
            }
            break;
        }
        on = a[0] == '-';
        for (a++; *a; a++) {
            if (*a == 'o') {
                char *name;
                if (a[1]) {
                    name = a + 1;
                } else if (i + 1 < argc && !(argv[i + 1][0] == '-' || argv[i + 1][0] == '+')) {
                    name = argv[++i];
                } else if (i + 1 >= argc || argv[i + 1][0] == '-' || argv[i + 1][0] == '+') {
                    if (invocation)
                        sh_error("-o requires an option name");
                    print_options(!on);
                    break;
                } else
                    name = argv[++i];
                if (set_option_name(name, on) < 0)
                    sh_error("%s: illegal option name", name);
                break;
            }
            if (invocation && (*a == 'c' || *a == 's' || *a == 'i')) {
                opts[*a == 'c' ? O_CMDSTR : *a == 's' ? O_STDIN : O_INTERACTIVE] = (char)on;
                continue;
            }
            if (set_option_letter(*a, on) < 0)
                sh_error("%c%c: illegal option", on ? '-' : '+', *a);
        }
    }
    if (i < argc)
        *setargs = 1;
    return i;
}

static int bi_set(int argc, char **argv)
{
    int setargs, i;
    if (argc == 1) {
        var_list(&out1, 0, NULL);
        return 0;
    }
    i = process_set_options(argc, argv, &setargs, 0);
    if (setargs)
        params_set(argv + i, argc - i);
    return 0;
}

static int bi_shift(int argc, char **argv)
{
    long n = 1;
    int i;
    if (argc > 1) {
        int ok;
        n = parse_long(argv[1], &ok);
        if (!ok || n < 0)
            sh_error("%s: illegal number", argv[1]);
    }
    if (n > posparams.n)
        sh_error("can't shift that many");
    if (n == 0)
        return 0;
    if (posparams.malloced)
        for (i = 0; i < n; i++)
            free(posparams.v[i]);
    memmove(posparams.v, posparams.v + n, (size_t)(posparams.n - n + 1) * sizeof(char *));
    posparams.n -= (int)n;
    return 0;
}

static int bi_times(int argc, char **argv)
{
    struct tms t;
    long hz = sysconf(_SC_CLK_TCK);
    (void)argc; (void)argv;
    times(&t);
#define TM(x) (int)((x) / hz / 60), (double)((x) % (hz * 60)) / (double)hz
    outfmt(&out1, "%dm%fs %dm%fs\n%dm%fs %dm%fs\n", TM(t.tms_utime), TM(t.tms_stime),
           TM(t.tms_cutime), TM(t.tms_cstime));
#undef TM
    return 0;
}

static void trap_print_one(int i, char **tab, int all)
{
    const char *a = tab[i];
    StackMark m;
    if (!a && !all)
        return;
    setstackmark(&m);
    if (!a)
        outfmt(&out1, "trap -- - %s\n", sig_name(i));
    else
        outfmt(&out1, "trap -- %s %s\n", *a ? quote_for_reinput(a) : "''", sig_name(i));
    popstackmark(&m);
}

static int bi_trap(int argc, char **argv)
{
    int i, c, pflag = 0, ret = 0;
    char **tab = saved_parent_traps ? saved_parent_traps : trap_actions;
    bopt_ind = 1;
    while ((c = bgetopt(argc, argv, "p")) != -1) {
        if (c == '?')
            sh_error("usage: trap [-p] [action condition...]");
        pflag = 1;
    }
    if (bopt_ind >= argc) {
        for (i = 0; i < NSIGS; i++) {
            if (!pflag && !tab[i] && !(sig_ignored_at_entry[i] && !interactive && i))
                continue;
            if (!tab[i] && sig_ignored_at_entry[i] && !interactive && i && !pflag) {
                outfmt(&out1, "trap -- '' %s\n", sig_name(i));
                continue;
            }
            if (pflag && (i == SIGKILL || i == SIGSTOP))
                continue;
            if (pflag && i && sig_name(i)[0] >= '0' && sig_name(i)[0] <= '9')
                continue;
            trap_print_one(i, tab, pflag);
        }
        return 0;
    }
    if (pflag) {
        for (i = bopt_ind; i < argc; i++) {
            int s = sig_by_name(argv[i]);
            if (s < 0) {
                sh_warn("%s: bad trap", argv[i]);
                ret = 1;
                continue;
            }
            trap_print_one(s, tab, 1);
        }
        return ret;
    }
    /* setting traps: forget the parent's list in a subshell */
    if (saved_parent_traps) {
        for (i = 0; i < NSIGS; i++)
            free(saved_parent_traps[i]);
        free(saved_parent_traps);
        saved_parent_traps = NULL;
    }
    {
        const char *action = argv[bopt_ind];
        int first = bopt_ind + 1;
        if (is_number(action)) {
            action = NULL;
            first = bopt_ind;
        } else if (strcmp(action, "-") == 0)
            action = NULL;
        if (first >= argc && action)
            sh_error("usage: trap [-p] [action condition...]");
        for (i = first; i < argc; i++) {
            int s = sig_by_name(argv[i]);
            if (s < 0 || s >= NSIGS) {
                sh_warn("%s: bad trap", argv[i]);
                ret = 1;
                continue;
            }
            if (s == SIGKILL || s == SIGSTOP)
                continue;
            if (s && sig_ignored_at_entry[s] && !interactive)
                continue;
            free(trap_actions[s]);
            trap_actions[s] = action ? xstrdup(action) : NULL;
            if (s)
                set_signal(s);
        }
    }
    return ret;
}

static int bi_unset(int argc, char **argv)
{
    int c, fflag = 0, vflag = 0, i, ret = 0;
    bopt_ind = 1;
    while ((c = bgetopt(argc, argv, "fv")) != -1) {
        if (c == '?')
            sh_error("usage: unset [-fv] name...");
        if (c == 'f') fflag = 1;
        else vflag = 1;
    }
    for (i = bopt_ind; i < argc; i++) {
        if (fflag && !vflag) {
            func_unset(argv[i]);
            continue;
        }
        if (var_unset(argv[i]) < 0) {
            sh_warn("%s: is read only", argv[i]);
            ret = 1;
        }
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/* intrinsic utilities                                                */
/* ------------------------------------------------------------------ */

static int bi_alias(int argc, char **argv)
{
    int i, ret = 0;
    if (argc == 1) {
        alias_print_all();
        return 0;
    }
    for (i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq && eq != argv[i]) {
            char *name = ststrndup(argv[i], (size_t)(eq - argv[i]));
            alias_set(name, eq + 1);
        } else {
            Alias *a = alias_lookup(argv[i]);
            if (a)
                alias_print(a);
            else {
                sh_warn("%s: not found", argv[i]);
                ret = 1;
            }
        }
    }
    return ret;
}

static int bi_unalias(int argc, char **argv)
{
    int c, i, ret = 0;
    bopt_ind = 1;
    while ((c = bgetopt(argc, argv, "a")) != -1) {
        if (c == '?')
            return 2;
        alias_unset_all();
        return 0;
    }
    if (bopt_ind >= argc) {
        sh_warn("usage: unalias [-a] name...");
        return 2;
    }
    for (i = bopt_ind; i < argc; i++)
        if (alias_unset(argv[i]) < 0) {
            sh_warn("%s: not found", argv[i]);
            ret = 1;
        }
    return ret;
}

/* ----- cd / pwd ----- */

char *pwd_logical;

char *getcwd_heap(void)
{
    size_t sz = 256;
    for (;;) {
        char *b = xmalloc(sz);
        if (getcwd(b, sz))
            return b;
        free(b);
        if (errno != ERANGE)
            return NULL;
        sz *= 2;
    }
}

static int has_dot_components(const char *p)
{
    const char *s = p;
    while (*s) {
        while (*s == '/') s++;
        if (s[0] == '.' && (s[1] == '/' || !s[1]))
            return 1;
        if (s[0] == '.' && s[1] == '.' && (s[2] == '/' || !s[2]))
            return 1;
        while (*s && *s != '/') s++;
    }
    return 0;
}

static int same_dir(const char *a, const char *b)
{
    struct stat x, y;
    return stat(a, &x) == 0 && stat(b, &y) == 0 && x.st_dev == y.st_dev && x.st_ino == y.st_ino;
}

void pwd_init(void)
{
    const char *p = var_get("PWD");
    if (p && p[0] == '/' && !has_dot_components(p) && same_dir(p, ".")) {
        pwd_logical = xstrdup(p);
    } else {
        pwd_logical = getcwd_heap();
        if (pwd_logical)
            var_set("PWD", pwd_logical, V_EXPORT);
    }
}

/* canonicalize a logical absolute path; returns NULL with errno on failure */
static char *canonicalize(const char *path)
{
    StrBuf out;
    const char *p = path;
    int leading = 0;
    while (*p == '/') { p++; leading++; }
    sb_init(&out);
    sb_add(&out, "//", leading == 2 ? 2 : 1);
    while (*p) {
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n == 0 || (n == 1 && p[0] == '.')) {
            /* skip */
        } else if (n == 2 && p[0] == '.' && p[1] == '.') {
            size_t root = leading == 2 ? 2 : 1;
            if (out.len > root) {
                struct stat st;
                if (stat(out.s, &st) < 0 || !S_ISDIR(st.st_mode)) {
                    if (stat(out.s, &st) == 0)
                        errno = ENOTDIR;
                    sb_free(&out);
                    return NULL;
                }
                while (out.len > root && out.s[out.len - 1] != '/')
                    out.len--;
                if (out.len > root)
                    out.len--;
                out.s[out.len] = 0;
            }
        } else {
            if (out.len > 0 && out.s[out.len - 1] != '/')
                sb_addc(&out, '/');
            sb_add(&out, p, n);
        }
        if (!e)
            break;
        p = e + 1;
    }
    return sb_take(&out);
}

static int is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int bi_cd(int argc, char **argv)
{
    int c, physical = 0, eflag = 0, print = 0;
    const char *dir;
    char *curpath = NULL, *newpwd = NULL;
    bopt_ind = 1;
    while ((c = bgetopt(argc, argv, "LPe")) != -1) {
        if (c == '?') return 2;
        if (c == 'L') physical = 0;
        else if (c == 'P') physical = 1;
        else eflag = 1;
    }
    if (argc - bopt_ind > 1) {
        sh_warn("too many arguments");
        return 2;
    }
    dir = argv[bopt_ind];
    if (!dir) {
        dir = var_get("HOME");
        if (!dir || !*dir) {
            sh_warn("HOME not set");
            return 1;
        }
    } else if (strcmp(dir, "-") == 0) {
        dir = var_get("OLDPWD");
        if (!dir || !*dir) {
            sh_warn("OLDPWD not set");
            return 1;
        }
        print = 1;
    }
    if (!*dir) {
        sh_warn("empty directory");
        return 1;
    }
    if (dir[0] == '/' ||
        (dir[0] == '.' && (dir[1] == '/' || !dir[1] || (dir[1] == '.' && (dir[2] == '/' || !dir[2]))))) {
        curpath = xstrdup(dir);
    } else {
        const char *cdp = var_get("CDPATH");
        const char *p = cdp ? cdp : "";
        for (;;) {
            const char *e = strchr(p, ':');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            StrBuf b;
            sb_init(&b);
            if (n) {
                sb_add(&b, p, n);
                if (p[n - 1] != '/')
                    sb_addc(&b, '/');
            } else
                sb_adds(&b, "./");
            sb_adds(&b, dir);
            if (is_dir(b.s)) {
                curpath = sb_take(&b);
                if (n)
                    print = 1;
                break;
            }
            sb_free(&b);
            if (!e)
                break;
            p = e + 1;
        }
        if (!curpath)
            curpath = xstrdup(dir);
    }
    if (!physical) {
        char *abs = curpath;
        if (curpath[0] != '/') {
            StrBuf b;
            const char *pw = pwd_logical ? pwd_logical : "";
            sb_init(&b);
            sb_adds(&b, pw);
            if (!b.len || b.s[b.len - 1] != '/')
                sb_addc(&b, '/');
            sb_adds(&b, curpath);
            abs = sb_take(&b);
            free(curpath);
        }
        curpath = canonicalize(abs);
        free(abs);
        if (!curpath) {
            sh_warn("can't cd to %s: %s", dir, strerror(errno));
            return 1;
        }
        if (chdir(curpath) < 0) {
            sh_warn("can't cd to %s: %s", dir, strerror(errno));
            free(curpath);
            return 1;
        }
        newpwd = curpath;
    } else {
        if (chdir(curpath) < 0) {
            sh_warn("can't cd to %s: %s", dir, strerror(errno));
            free(curpath);
            return 1;
        }
        free(curpath);
        newpwd = getcwd_heap();
        if (!newpwd) {
            return eflag ? 1 : 0;
        }
    }
    {
        const char *old = var_get("PWD");
        if (old)
            var_set("OLDPWD", old, 0);
        else if (pwd_logical)
            var_set("OLDPWD", pwd_logical, 0);
    }
    var_set("PWD", newpwd, 0);
    free(pwd_logical);
    pwd_logical = newpwd;
    if (print)
        outfmt(&out1, "%s\n", newpwd);
    return 0;
}

static int bi_pwd(int argc, char **argv)
{
    int c, physical = 0;
    bopt_ind = 1;
    while ((c = bgetopt(argc, argv, "LP")) != -1) {
        if (c == '?') return 2;
        physical = c == 'P';
    }
    if (!physical) {
        const char *p = var_get("PWD");
        if (p && p[0] == '/' && !has_dot_components(p) && same_dir(p, ".")) {
            outfmt(&out1, "%s\n", p);
            return 0;
        }
    }
    {
        char *d = getcwd_heap();
        if (!d) {
            sh_warn("%s", strerror(errno));
            return 1;
        }
        outfmt(&out1, "%s\n", d);
        free(d);
    }
    return 0;
}

/* ----- getopts ----- */

static int gopt_sub;            /* position within the current argument */
static int gopt_internal;       /* we are assigning OPTIND ourselves */

void getopts_reset(void)
{
    if (!gopt_internal)
        gopt_sub = 0;
}

static void gset(const char *name, const char *val)
{
    if (var_set(name, val, 0) < 0)
        sh_error("%s: is read only", name);
}

static int bi_getopts(int argc, char **argv)
{
    const char *optstr, *name;
    char **args;
    int nargs, optind, silent;
    const char *o;
    char *arg;
    char cbuf[2] = { 0, 0 };
    int ret = 0;
    if (argc < 3) {
        sh_warn("usage: getopts optstring name [arg...]");
        return 2;
    }
    optstr = argv[1];
    name = argv[2];
    if (!is_name(name)) {
        sh_warn("%s: bad variable name", name);
        return 2;
    }
    silent = optstr[0] == ':';
    if (argc > 3) {
        args = argv + 3;
        nargs = argc - 3;
    } else {
        args = posparams.v;
        nargs = posparams.n;
    }
    {
        const char *oi = var_get("OPTIND");
        int ok;
        long v = oi ? parse_long(oi, &ok) : 1;
        if (!oi || !ok || v < 1) { v = 1; gopt_sub = 0; }
        optind = (int)v;
    }
    gopt_internal = 1;
    if (gopt_sub == 0) {
        if (optind > nargs || args[optind - 1][0] != '-' || !args[optind - 1][1]) {
            ret = 1;
            goto end;
        }
        if (strcmp(args[optind - 1], "--") == 0) {
            optind++;
            ret = 1;
            goto end;
        }
        gopt_sub = 1;
    }
    arg = args[optind - 1];
    cbuf[0] = arg[gopt_sub++];
    o = strchr(optstr + silent, cbuf[0]);
    if (!o || cbuf[0] == ':') {
        if (silent)
            gset("OPTARG", cbuf);
        else {
            outfmt(&out2, "%s: illegal option -- %c\n", arg0, cbuf[0]);
            outflush(&out2);
            var_unset("OPTARG");
        }
        gset(name, "?");
        if (!arg[gopt_sub]) { optind++; gopt_sub = 0; }
        goto setind;
    }
    if (o[1] == ':') {
        if (arg[gopt_sub]) {
            gset("OPTARG", arg + gopt_sub);
            optind++;
        } else if (optind < nargs) {
            gset("OPTARG", args[optind]);
            optind += 2;
        } else {
            optind++;
            gopt_sub = 0;
            if (silent) {
                gset("OPTARG", cbuf);
                gset(name, ":");
            } else {
                outfmt(&out2, "%s: option requires an argument -- %c\n", arg0, cbuf[0]);
                outflush(&out2);
                var_unset("OPTARG");
                gset(name, "?");
            }
            goto setind;
        }
        gopt_sub = 0;
    } else {
        var_unset("OPTARG");
        if (!arg[gopt_sub]) { optind++; gopt_sub = 0; }
    }
    gset(name, cbuf);
setind:
    {
        char b[32];
        snprintf(b, sizeof b, "%d", optind);
        gset("OPTIND", b);
    }
    gopt_internal = 0;
    return 0;
end:
    {
        char b[32];
        snprintf(b, sizeof b, "%d", optind);
        gset("OPTIND", b);
        gset(name, "?");
        var_unset("OPTARG");
        gopt_sub = 0;
    }
    gopt_internal = 0;
    return ret;
}

/* ----- hash ----- */

static int bi_hash(int argc, char **argv)
{
    int c, i, ret = 0;
    bopt_ind = 1;
    while ((c = bgetopt(argc, argv, "r")) != -1) {
        if (c == '?') return 2;
        hash_clear();
    }
    if (argc == 1) {
        hash_print();
        return 0;
    }
    for (i = bopt_ind; i < argc; i++)
        ret |= hash_add(argv[i]);
    return ret;
}

/* ----- jobs, fg, bg, wait, kill ----- */

extern void print_job(Out *o, Job *j, int longfmt);
extern Job *parent_jobs;
extern int nparent_jobs, parent_curjob, parent_prevjob;
extern int job_continue(Job *j, int fg);

static int bi_jobs(int argc, char **argv)
{
    int c, lflag = 0, pflag = 0, i, ret = 0;
    bopt_ind = 1;
    while ((c = bgetopt(argc, argv, "lp")) != -1) {
        if (c == '?') return 2;
        if (c == 'l') lflag = 1, pflag = 0;
        else pflag = 1, lflag = 0;
    }
    jobs_reap(0);
    if (bopt_ind >= argc && in_subshell && parent_jobs) {
        /* a subshell may show the jobs of the invoking shell */
        Job *save = jobtab;
        int sn = njobs, sc = curjob, sp = prevjob;
        jobtab = parent_jobs; njobs = nparent_jobs;
        curjob = parent_curjob; prevjob = parent_prevjob;
        for (i = 0; i < njobs; i++)
            if (jobtab[i].used && jobtab[i].bg) {
                if (pflag)
                    outfmt(&out1, "%ld\n", (long)(jobtab[i].jobctl ? jobtab[i].pgid : jobtab[i].procs[jobtab[i].nprocs - 1].pid));
                else
                    print_job(&out1, &jobtab[i], lflag);
            }
        jobtab = save; njobs = sn; curjob = sc; prevjob = sp;
        if (!njobs)
            return 0;
    }
    if (bopt_ind >= argc) {
        for (i = 0; i < njobs; i++) {
            Job *j = &jobtab[i];
            if (!j->used || !j->bg)
                continue;
            if (pflag)
                outfmt(&out1, "%ld\n", (long)(j->jobctl ? j->pgid : j->procs[j->nprocs - 1].pid));
            else
                print_job(&out1, j, lflag);
            j->changed = 0;
            if (j->state == JOB_DONE && !pflag)
                job_free(j);
        }
        return 0;
    }
    for (i = bopt_ind; i < argc; i++) {
        Job *j = job_by_spec(argv[i], 1);
        if (!j) { ret = 1; continue; }
        if (pflag)
            outfmt(&out1, "%ld\n", (long)(j->jobctl ? j->pgid : j->procs[j->nprocs - 1].pid));
        else
            print_job(&out1, j, lflag);
        j->changed = 0;
        if (j->state == JOB_DONE && !pflag)
            job_free(j);
    }
    return ret;
}

static int bi_fgbg(int argc, char **argv)
{
    int fg = argv[0][0] == 'f', i, ret = 0;
    bopt_ind = 1;
    if (bgetopt(argc, argv, "") == '?') return 2;
    if (!job_ctl_enabled()) {
        sh_warn("no job control");
        return 1;
    }
    jobs_reap(0);
    if (bopt_ind >= argc) {
        Job *j = job_by_spec("%+", 1);
        if (!j) return 1;
        if (fg) {
            outfmt(&out1, "%s\n", j->cmd ? j->cmd : "");
            outflush(&out1);
            return job_continue(j, 1);
        }
        outfmt(&out1, "[%d] %s &\n", j->num, j->cmd ? j->cmd : "");
        outflush(&out1);
        job_continue(j, 0);
        last_bgpid = j->procs[j->nprocs - 1].pid;
        last_bgpid_set = 1;
        return 0;
    }
    for (i = bopt_ind; i < argc; i++) {
        Job *j = job_by_spec(argv[i], 1);
        if (!j) { ret = 1; continue; }
        if (fg) {
            outfmt(&out1, "%s\n", j->cmd ? j->cmd : "");
            outflush(&out1);
            return job_continue(j, 1);
        }
        outfmt(&out1, "[%d] %s &\n", j->num, j->cmd ? j->cmd : "");
        outflush(&out1);
        job_continue(j, 0);
        last_bgpid = j->procs[j->nprocs - 1].pid;
        last_bgpid_set = 1;
    }
    return ret;
}

static int pending_trapped_signal(void)
{
    int i;
    for (i = 1; i < NSIGS; i++)
        if (pending_sig[i] && trap_actions[i] && *trap_actions[i])
            return i;
    return 0;
}

/* block until something changes; returns signal number if interrupted */
static int wait_one(void)
{
    int st, sig;
    pid_t pid;
    if ((sig = pending_trapped_signal()))
        return sig;
    pid = waitpid(-1, &st, WUNTRACED);
    if (pid < 0) {
        if (errno == EINTR) {
            if ((sig = pending_trapped_signal()))
                return sig;
            return 0;
        }
        return -1;
    }
    {
        Proc *p;
        Job *j;
        int i, k;
        for (i = 0; i < njobs; i++) {
            if (!jobtab[i].used) continue;
            for (k = 0; k < jobtab[i].nprocs; k++)
                if (jobtab[i].procs[k].pid == pid) {
                    p = &jobtab[i].procs[k];
                    j = &jobtab[i];
                    p->status = st;
                    p->state = WIFSTOPPED(st) ? JOB_STOPPED : JOB_DONE;
                    {
                        int r = 0, s = 0, q;
                        for (q = 0; q < j->nprocs; q++) {
                            if (j->procs[q].state == JOB_RUNNING) r++;
                            else if (j->procs[q].state == JOB_STOPPED) s++;
                        }
                        j->state = r ? JOB_RUNNING : s ? JOB_STOPPED : JOB_DONE;
                        j->changed = 1;
                    }
                }
        }
    }
    return 0;
}

static int bi_wait(int argc, char **argv)
{
    int i, st = 0;
    bopt_ind = 1;
    if (bgetopt(argc, argv, "") == '?') return 2;
    if (bopt_ind >= argc) {
        for (;;) {
            int any = 0, r;
            jobs_reap(0);
            for (i = 0; i < njobs; i++)
                if (jobtab[i].used && jobtab[i].bg && jobtab[i].state == JOB_RUNNING)
                    any = 1;
            if (!any)
                break;
            r = wait_one();
            if (r > 0)
                return 128 + r;
            if (r < 0)
                break;
        }
        for (i = 0; i < njobs; i++)
            if (jobtab[i].used && jobtab[i].bg && jobtab[i].state == JOB_DONE &&
                !(interactive && jobtab[i].changed && 0))
                job_free(&jobtab[i]);
        return 0;
    }
    for (i = bopt_ind; i < argc; i++) {
        Job *j = job_by_spec(argv[i], 0);
        pid_t pid = 0;
        if (!j) {
            st = 127;
            continue;
        }
        if (argv[i][0] != '%')
            pid = (pid_t)atol(argv[i]);
        for (;;) {
            int r;
            jobs_reap(0);
            if (pid && j->pgid != pid) {
                int k, done = 0;
                for (k = 0; k < j->nprocs; k++)
                    if (j->procs[k].pid == pid && j->procs[k].state != JOB_RUNNING) {
                        done = 1;
                        st = wait_status_to_exit(j->procs[k].status);
                    }
                if (done)
                    break;
            } else if (j->state != JOB_RUNNING) {
                st = job_status(j);
                break;
            }
            r = wait_one();
            if (r > 0)
                return 128 + r;
            if (r < 0) {
                st = 127;
                break;
            }
        }
        if (j->state == JOB_DONE)
            job_free(j);
    }
    return st;
}

static void list_signals(void)
{
    int i, first = 1;
    for (i = 1; i < NSIGS; i++) {
        const char *n = sig_name(i);
        if (isdigit((unsigned char)n[0]))
            continue;
        outfmt(&out1, first ? "%s" : " %s", n);
        first = 0;
    }
    outc(&out1, '\n');
}

static int bi_kill(int argc, char **argv)
{
    int sig = SIGTERM, i = 1, ret = 0;
    if (argc < 2) {
        sh_warn("usage: kill [-s signal_name] pid... | -l [exit_status]");
        return 2;
    }
    if (strcmp(argv[1], "-l") == 0) {
        if (argc == 2) {
            list_signals();
            return 0;
        }
        for (i = 2; i < argc; i++) {
            int ok;
            long n = parse_long(argv[i], &ok);
            if (ok) {
                if (n > 128) n -= 128;
                if (n > 0 && n < NSIGS && !isdigit((unsigned char)sig_name((int)n)[0]))
                    outfmt(&out1, "%s\n", sig_name((int)n));
                else {
                    sh_warn("%s: invalid signal number", argv[i]);
                    ret = 1;
                }
            } else {
                int s = sig_by_name(argv[i]);
                if (s > 0) outfmt(&out1, "%d\n", s);
                else { sh_warn("%s: invalid signal", argv[i]); ret = 1; }
            }
        }
        return ret;
    }
    if (strcmp(argv[1], "-s") == 0) {
        if (argc < 3) { sh_warn("-s requires a signal name"); return 2; }
        sig = strcmp(argv[2], "0") == 0 ? 0 : sig_by_name(argv[2]);
        if (sig < 0 || (sig == 0 && strcmp(argv[2], "0"))) {
            sh_warn("%s: invalid signal", argv[2]);
            return 2;
        }
        i = 3;
    } else if (argv[1][0] == '-' && argv[1][1] && strcmp(argv[1], "--")) {
        const char *s = argv[1] + 1;
        sig = is_number(s) ? atoi(s) : sig_by_name(s);
        if (sig < 0 || sig >= NSIGS || (!is_number(s) && sig == 0)) {
            sh_warn("%s: invalid signal", s);
            return 2;
        }
        i = 2;
    }
    if (i < argc && strcmp(argv[i], "--") == 0)
        i++;
    if (i >= argc) {
        sh_warn("no process id specified");
        return 2;
    }
    for (; i < argc; i++) {
        if (argv[i][0] == '%') {
            Job *j = job_by_spec(argv[i], 1);
            int k;
            if (!j) { ret = 1; continue; }
            if (j->jobctl) {
                if (killpg(j->pgid, sig) < 0) { sh_warn("%s: %s", argv[i], strerror(errno)); ret = 1; }
            } else
                for (k = 0; k < j->nprocs; k++)
                    kill(j->procs[k].pid, sig);
            if (sig == SIGCONT) {
                for (k = 0; k < j->nprocs; k++)
                    if (j->procs[k].state == JOB_STOPPED) j->procs[k].state = JOB_RUNNING;
                j->state = JOB_RUNNING;
            }
        } else {
            int ok;
            long pid = parse_long(argv[i], &ok);
            if (!ok) {
                sh_warn("%s: arguments must be process or job IDs", argv[i]);
                ret = 1;
                continue;
            }
            if (kill((pid_t)pid, sig) < 0) {
                sh_warn("(%s) - %s", argv[i], strerror(errno));
                ret = 1;
            }
        }
    }
    return ret;
}

/* ----- read ----- */

static int bi_read(int argc, char **argv)
{
    int c, raw = 0, delim = '\n', nvars, i;
    StrBuf line;
    StrBuf qf;      /* per-byte quoted flags */
    int eof = 0, ret = 0;
    const char *ifs;
    bopt_ind = 1;
    while ((c = bgetopt(argc, argv, "rd:")) != -1) {
        if (c == '?') return 2;
        if (c == 'r') raw = 1;
        else delim = (unsigned char)bopt_arg[0];
    }
    nvars = argc - bopt_ind;
    if (nvars < 1) {
        sh_warn("usage: read [-r] [-d delim] var...");
        return 2;
    }
    for (i = bopt_ind; i < argc; i++)
        if (!is_name(argv[i])) {
            sh_warn("%s: bad variable name", argv[i]);
            return 2;
        }
    sb_init(&line);
    sb_init(&qf);
    for (;;) {
        char ch;
        ssize_t n = read(0, &ch, 1);
        if (n < 0) {
            if (errno == EINTR) {
                if (pending_trapped_signal()) { ret = 128 + pending_trapped_signal(); eof = 1; break; }
                continue;
            }
            eof = 1;
            ret = 2;
            break;
        }
        if (n == 0) {
            eof = 1;
            break;
        }
        if ((unsigned char)ch == (unsigned)delim && (delim || !ch))
            break;
        if (!raw && ch == '\\') {
            char d;
            n = read(0, &d, 1);
            if (n <= 0) {
                eof = 1;
                break;
            }
            if (d == '\n' || ((unsigned char)d == (unsigned)delim)) {
                if (interactive && isatty(0)) {
                    const char *ps2 = var_get("PS2");
                    outs(&out2, ps2 ? ps2 : "");
                    outflush(&out2);
                }
                continue;
            }
            sb_addc(&line, d);
            sb_addc(&qf, 1);
            continue;
        }
        sb_addc(&line, ch);
        sb_addc(&qf, 0);
    }
    if (eof && ret == 0)
        ret = 1;
    ifs = ifs_value();
    {
        char *s = line.s ? line.s : "";
        char *q = qf.s ? qf.s : "";
        size_t len = line.len, p = 0;
        int vi = bopt_ind;
        if (ifs_isset() && !*ifs) {
            if (var_set(argv[vi], s, 0) < 0) { sh_warn("%s: is read only", argv[vi]); ret = 2; }
            for (vi++; vi < argc; vi++)
                if (var_set(argv[vi], "", 0) < 0) { sh_warn("%s: is read only", argv[vi]); ret = 2; }
            goto done;
        }
#define ISWS(k) (!q[k] && (s[k] == ' ' || s[k] == '\t' || s[k] == '\n') && strchr(ifs, s[k]))
#define ISIFS(k) (!q[k] && s[k] && strchr(ifs, s[k]))
        /* fields for all but the last variable */
        while (vi < argc - 1) {
            StrBuf f;
            int seen = 0, delimited = 0;
            /* leading IFS white space */
            while (p < len && ISWS(p)) p++;
            if (p >= len)
                break;
            sb_init(&f);
            while (p < len) {
                if (!ISIFS(p)) {
                    sb_addc(&f, s[p]);
                    p++;
                    continue;
                }
                while (p < len && ISWS(p)) p++;
                if (p < len && ISIFS(p) && !ISWS(p)) {
                    seen = 1;
                    p++;
                }
                delimited = 1;
                break;
            }
            (void)seen;
            (void)delimited;
            if (var_set(argv[vi], f.s ? f.s : "", 0) < 0) { sh_warn("%s: is read only", argv[vi]); ret = 2; }
            sb_free(&f);
            vi++;
        }
        if (vi == argc - 1) {
            size_t e = len;
            StrBuf f;
            while (p < len && ISWS(p)) p++;
            while (e > p && ISWS(e - 1)) e--;
            sb_init(&f);
            sb_add(&f, s + p, e - p);
            if (var_set(argv[vi], f.s ? f.s : "", 0) < 0) { sh_warn("%s: is read only", argv[vi]); ret = 2; }
            sb_free(&f);
            vi++;
        }
        for (; vi < argc; vi++)
            if (var_set(argv[vi], "", 0) < 0) { sh_warn("%s: is read only", argv[vi]); ret = 2; }
#undef ISWS
#undef ISIFS
    }
done:
    sb_free(&line);
    sb_free(&qf);
    return ret;
}

/* ----- type and command -v/-V ----- */

static const char *reserved_words[] = {
    "!", "{", "}", "case", "do", "done", "elif", "else", "esac", "fi", "for",
    "if", "in", "then", "until", "while", NULL
};

static int is_reserved(const char *s)
{
    int i;
    for (i = 0; reserved_words[i]; i++)
        if (strcmp(s, reserved_words[i]) == 0)
            return 1;
    return 0;
}

static const char *default_path(void)
{
    static char buf[1024];
    size_t n = confstr(_CS_PATH, buf, sizeof buf);
    if (n == 0 || n > sizeof buf)
        return "/usr/bin:/bin";
    return buf;
}

/* mode: 0 = type, 1 = command -v, 2 = command -V */
static int describe(const char *name, int mode, int usedefpath)
{
    const Builtin *b;
    Alias *a;
    char *path;
    StackMark m;
    const char *pathvar = usedefpath ? default_path() : NULL;
    setstackmark(&m);
    if ((a = alias_lookup(name))) {
        if (mode == 1)
            outfmt(&out1, "alias %s=%s\n", name, quote_for_reinput(a->val));
        else
            outfmt(&out1, "%s is an alias for %s\n", name, a->val);
        popstackmark(&m);
        return 0;
    }
    if (is_reserved(name)) {
        if (mode == 1) outfmt(&out1, "%s\n", name);
        else outfmt(&out1, "%s is a shell keyword\n", name);
        return 0;
    }
    b = builtin_lookup(name);
    if (b && (b->flags & BI_SPECIAL)) {
        if (mode == 1) outfmt(&out1, "%s\n", name);
        else outfmt(&out1, "%s is a special shell builtin\n", name);
        return 0;
    }
    if (func_lookup(name)) {
        if (mode == 1) outfmt(&out1, "%s\n", name);
        else outfmt(&out1, "%s is a shell function\n", name);
        return 0;
    }
    if (b && (b->flags & BI_INTRINSIC)) {
        if (mode == 1) outfmt(&out1, "%s\n", name);
        else outfmt(&out1, "%s is a shell builtin\n", name);
        return 0;
    }
    if (strchr(name, '/')) {
        struct stat st;
        if (stat(name, &st) == 0 && access(name, X_OK) == 0 && !S_ISDIR(st.st_mode)) {
            if (mode == 1) {
                if (name[0] == '/')
                    outfmt(&out1, "%s\n", name);
                else {
                    char *cwd = getcwd_heap();
                    outfmt(&out1, "%s/%s\n", cwd ? cwd : ".", name);
                    free(cwd);
                }
            } else
                outfmt(&out1, "%s is %s\n", name, name);
            return 0;
        }
        goto notfound;
    }
    path = find_command(name, pathvar, NULL);
    if (path) {
        char *abs = path;
        if (path[0] != '/') {
            char *cwd = getcwd_heap();
            StrBuf sb;
            sb_init(&sb);
            sb_adds(&sb, cwd ? cwd : ".");
            sb_addc(&sb, '/');
            sb_adds(&sb, path[0] == '.' && path[1] == '/' ? path + 2 : path);
            abs = sb_stack(&sb);
            free(cwd);
        }
        if (mode == 1)
            outfmt(&out1, "%s\n", abs);
        else if (b)
            outfmt(&out1, "%s is a shell builtin (%s)\n", name, abs);
        else
            outfmt(&out1, "%s is %s\n", name, abs);
        popstackmark(&m);
        return 0;
    }
notfound:
    popstackmark(&m);
    if (mode != 1) {
        outflush(&out1);
        if (mode == 0)
            sh_warn("%s: not found", name);
        else
            sh_warn("%s: not found", name);
    }
    return mode == 0 ? 127 : 1;
}

static int bi_type(int argc, char **argv)
{
    int i, ret = 0;
    for (i = 1; i < argc; i++)
        if (describe(argv[i], 0, 0))
            ret = 1;
    return ret;
}

/* "command" when given -v or -V (other forms are handled in exec.c) */
static int bi_command(int argc, char **argv)
{
    int c, p = 0, v = 0, i, ret = 0;
    bopt_ind = 1;
    while ((c = bgetopt(argc, argv, "pvV")) != -1) {
        if (c == '?') return 2;
        if (c == 'p') p = 1;
        else v = c == 'v' ? 1 : 2;
    }
    if (!v)
        return 0;
    for (i = bopt_ind; i < argc; i++)
        if (describe(argv[i], v, p))
            ret = 1;
    return ret;
}

/* ----- ulimit ----- */

static const struct { char opt; int res; int unit; const char *desc; } limits[] = {
    { 'c', RLIMIT_CORE, 512, "core file size (blocks)" },
    { 'd', RLIMIT_DATA, 1024, "data seg size (kbytes)" },
    { 'f', RLIMIT_FSIZE, 512, "file size (blocks)" },
    { 'n', RLIMIT_NOFILE, 1, "open files" },
    { 's', RLIMIT_STACK, 1024, "stack size (kbytes)" },
    { 't', RLIMIT_CPU, 1, "cpu time (seconds)" },
    { 'v', RLIMIT_AS, 1024, "virtual memory (kbytes)" },
    { 0, 0, 0, NULL }
};

static void print_limit(rlim_t v, int unit)
{
    if (v == RLIM_INFINITY)
        outs(&out1, "unlimited\n");
    else
        outfmt(&out1, "%llu\n", (unsigned long long)(v / (rlim_t)unit));
}

static int bi_ulimit(int argc, char **argv)
{
    int c, hard = 0, soft = 0, all = 0, which = 2, i;
    bopt_ind = 1;
    while ((c = bgetopt(argc, argv, "HSacdfnstv")) != -1) {
        if (c == '?') return 2;
        if (c == 'H') hard = 1;
        else if (c == 'S') soft = 1;
        else if (c == 'a') all = 1;
        else
            for (i = 0; limits[i].opt; i++)
                if (limits[i].opt == c) which = i;
    }
    if (all) {
        for (i = 0; limits[i].opt; i++) {
            struct rlimit rl;
            getrlimit(limits[i].res, &rl);
            outfmt(&out1, "-%c: %-26s ", limits[i].opt, limits[i].desc);
            print_limit(hard ? rl.rlim_max : rl.rlim_cur, limits[i].unit);
        }
        return 0;
    }
    {
        struct rlimit rl;
        getrlimit(limits[which].res, &rl);
        if (bopt_ind >= argc) {
            print_limit(hard ? rl.rlim_max : rl.rlim_cur, limits[which].unit);
            return 0;
        }
        {
            const char *s = argv[bopt_ind];
            rlim_t v;
            if (strcmp(s, "unlimited") == 0)
                v = RLIM_INFINITY;
            else if (is_number(s)) {
                unsigned long long n = strtoull(s, NULL, 10);
                v = (rlim_t)(n * (unsigned long long)limits[which].unit);
            } else {
                sh_warn("%s: bad number", s);
                return 2;
            }
            if (!hard && !soft)
                hard = soft = 1;
            if (hard) rl.rlim_max = v;
            if (soft) rl.rlim_cur = v;
            if (setrlimit(limits[which].res, &rl) < 0) {
                sh_warn("error setting limit: %s", strerror(errno));
                return 1;
            }
        }
    }
    return 0;
}

/* ----- umask ----- */

/* apply a chmod-style symbolic mode to mode m (permission bits) */
static int apply_symbolic(const char *s, mode_t *mp, mode_t cmask)
{
    mode_t m = *mp;
    const char *p = s;
    for (;;) {
        mode_t who = 0;
        int whoset = 0;
        while (*p && strchr("ugoa", *p)) {
            whoset = 1;
            switch (*p) {
            case 'u': who |= 0700; break;
            case 'g': who |= 0070; break;
            case 'o': who |= 0007; break;
            case 'a': who |= 0777; break;
            }
            p++;
        }
        if (!whoset)
            who = 0777 & ~cmask;
        if (!*p || !strchr("+-=", *p))
            return -1;
        while (*p && strchr("+-=", *p)) {
            char op = *p++;
            mode_t perm = 0;
            if (*p && strchr("ugo", *p)) {
                mode_t src = 0;
                switch (*p) {
                case 'u': src = (m >> 6) & 7; break;
                case 'g': src = (m >> 3) & 7; break;
                case 'o': src = m & 7; break;
                }
                perm = src | (src << 3) | (src << 6);
                p++;
            } else {
                while (*p && strchr("rwxXst", *p)) {
                    switch (*p) {
                    case 'r': perm |= 0444; break;
                    case 'w': perm |= 0222; break;
                    case 'x': perm |= 0111; break;
                    case 'X': if (m & 0111) perm |= 0111; break;
                    }
                    p++;
                }
            }
            if (!whoset && op != '=')
                perm &= who;
            else
                perm &= (whoset ? who : 0777);
            if (op == '+') m |= perm;
            else if (op == '-') m &= ~perm;
            else m = (m & ~(whoset ? who : 0777)) | perm;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p)
            return -1;
        break;
    }
    *mp = m;
    return 0;
}

static int bi_umask(int argc, char **argv)
{
    int c, sym = 0;
    mode_t mask;
    bopt_ind = 1;
    while ((c = bgetopt(argc, argv, "S")) != -1) {
        if (c == '?') return 2;
        sym = 1;
    }
    mask = umask(0);
    umask(mask);
    if (bopt_ind >= argc) {
        if (sym) {
            mode_t m = ~mask & 0777;
            char buf[32];
            int k = 0, w;
            const char *who = "ugo";
            for (w = 0; w < 3; w++) {
                mode_t bits = (m >> (6 - 3 * w)) & 7;
                if (w) buf[k++] = ',';
                buf[k++] = who[w];
                buf[k++] = '=';
                if (bits & 4) buf[k++] = 'r';
                if (bits & 2) buf[k++] = 'w';
                if (bits & 1) buf[k++] = 'x';
            }
            buf[k] = 0;
            outfmt(&out1, "%s\n", buf);
        } else
            outfmt(&out1, "%04o\n", (unsigned)mask);
        return 0;
    }
    {
        const char *s = argv[bopt_ind];
        if (isdigit((unsigned char)*s)) {
            char *end;
            long v = strtol(s, &end, 8);
            if (*end || v < 0 || v > 07777) {
                sh_warn("%s: illegal mode", s);
                return 2;
            }
            umask((mode_t)v & 0777);
        } else {
            mode_t m = ~mask & 0777;
            if (apply_symbolic(s, &m, mask) < 0) {
                sh_warn("%s: illegal mode", s);
                return 2;
            }
            umask(~m & 0777);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* regular built-ins: true, false (echo/printf/test are in bltin2.c)  */
/* ------------------------------------------------------------------ */

static int bi_true(int argc, char **argv) { (void)argc; (void)argv; return 0; }
static int bi_false(int argc, char **argv) { (void)argc; (void)argv; return 1; }

extern int bi_echo(int, char **);
extern int bi_printf(int, char **);
extern int bi_test(int, char **);

const Builtin builtins[] = {
    { ".", bi_dot, BI_SPECIAL },
    { ":", bi_colon, BI_SPECIAL },
    { "[", bi_test, BI_REGULAR },
    { "alias", bi_alias, BI_INTRINSIC },
    { "bg", bi_fgbg, BI_INTRINSIC },
    { "break", bi_break, BI_SPECIAL },
    { "cd", bi_cd, BI_INTRINSIC },
    { "command", bi_command, BI_INTRINSIC },
    { "continue", bi_continue, BI_SPECIAL },
    { "echo", bi_echo, BI_REGULAR },
    { "eval", bi_eval, BI_SPECIAL },
    { "exec", bi_exec, BI_SPECIAL },
    { "exit", bi_exit, BI_SPECIAL },
    { "export", bi_export, BI_SPECIAL | BI_DECL },
    { "false", bi_false, BI_REGULAR },
    { "fc", bi_fc, BI_INTRINSIC },
    { "fg", bi_fgbg, BI_INTRINSIC },
    { "getopts", bi_getopts, BI_INTRINSIC },
    { "hash", bi_hash, BI_INTRINSIC },
    { "jobs", bi_jobs, BI_INTRINSIC },
    { "kill", bi_kill, BI_INTRINSIC },
    { "printf", bi_printf, BI_REGULAR },
    { "pwd", bi_pwd, BI_REGULAR },
    { "read", bi_read, BI_INTRINSIC },
    { "readonly", bi_readonly, BI_SPECIAL | BI_DECL },
    { "return", bi_return, BI_SPECIAL },
    { "set", bi_set, BI_SPECIAL },
    { "shift", bi_shift, BI_SPECIAL },
    { "test", bi_test, BI_REGULAR },
    { "times", bi_times, BI_SPECIAL },
    { "trap", bi_trap, BI_SPECIAL },
    { "true", bi_true, BI_REGULAR },
    { "type", bi_type, BI_INTRINSIC },
    { "ulimit", bi_ulimit, BI_INTRINSIC },
    { "umask", bi_umask, BI_INTRINSIC },
    { "unalias", bi_unalias, BI_INTRINSIC },
    { "unset", bi_unset, BI_SPECIAL },
    { "wait", bi_wait, BI_INTRINSIC },
    { NULL, NULL, 0 }
};

bltin_fn bltin_exec_fn(void) { return bi_exec; }

const Builtin *builtin_lookup(const char *name)
{
    int i;
    for (i = 0; builtins[i].name; i++)
        if (strcmp(builtins[i].name, name) == 0)
            return &builtins[i];
    return NULL;
}

int run_builtin(const Builtin *b, int argc, char **argv)
{
    int saved_ind = bopt_ind, saved_sub = bopt_sub, r;
    bopt_ind = 1;
    bopt_sub = 0;
    builtin_running++;
    r = b->fn(argc, argv);
    builtin_running--;
    bopt_ind = saved_ind;
    bopt_sub = saved_sub;
    return r;
}
