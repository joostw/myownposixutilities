/*
 * main.c - invocation, initialization and signals for ex and vi.
 */
#include "ex.h"
#include "cmd.h"

char *pending_msg;

static void on_int(int sig)
{
    (void)sig;
    if (batch_stdin) {
        /* not a terminal: exit without writing anything */
        _exit(1);
    }
    intr = 1;
}

static void on_hup(int sig)
{
    (void)sig;
    got_hup = 1;
    intr = 1;
}

static void on_winch(int sig)
{
    (void)sig;
    got_winch = 1;
}

static void on_cont(int sig)
{
    (void)sig;
    got_cont = 1;
}

static void install(int sig, void (*fn)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;    /* no SA_RESTART: reads are interrupted */
    sigaction(sig, &sa, NULL);
}

static void usage(const char *prog)
{
    diag("usage: %s [-rR] [-s|-v] [-c command] [-t tagstring] [-w size] [file...]", prog);
    exit(2);
}

/* is a .exrc file safe to read?  0 no file, 1 ok, -1 exists but refused */
static int exrc_ok(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0)
        return 0;
    if (st.st_uid != getuid() && getuid() != 0)
        return -1;
    if (st.st_mode & (S_IWGRP | S_IWOTH))
        return -1;
    return 1;
}

static void run_exrc(const char *path)
{
    int fd = open(path, O_RDONLY);
    Str text;
    char buf[4096];
    ssize_t n;
    if (fd < 0)
        return;
    str_init(&text);
    while ((n = read(fd, buf, sizeof buf)) > 0)
        str_addn(&text, buf, (size_t)n);
    close(fd);
    run_rc_text(text.s ? text.s : "");
    str_free(&text);
}

static void initialize(void)
{
    const char *exinit = getenv("EXINIT"), *home = getenv("HOME");
    int home_is_cwd = 0;
    char cwd[PATH_MAX];
    if (exinit) {
        ex_run(exinit, NULL);
    } else if (home && *home) {
        Str p;
        int r;
        str_init(&p);
        str_fmt(&p, "%s/.exrc", home);
        r = exrc_ok(p.s);
        if (r > 0)
            run_exrc(p.s);
        else if (r < 0)
            errmsg_show("$HOME/.exrc: not read: owner or permissions are unsafe");
        str_free(&p);
    }
    if (home && *home && getcwd(cwd, sizeof cwd)) {
        struct stat a, b;
        if (stat(home, &a) == 0 && stat(cwd, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino)
            home_is_cwd = 1;
    }
    if (OV(OP_EXRC) && !home_is_cwd) {
        int r = exrc_ok(".exrc");
        if (r > 0)
            run_exrc(".exrc");
        else if (r < 0)
            errmsg_show(".exrc: not read: owner or permissions are unsafe");
    }
}

int main(int argc, char **argv)
{
    const char *prog = strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0];
    static int visual = 0;
    int sflag = 0, opt, wsize = -1, i;
    static char *tag = NULL;
    char **args;
    int nargv = 0;
    setlocale(LC_ALL, "");
    chars_init();
    if (strncmp(prog, "vi", 2) == 0 || strcmp(prog, "view") == 0)
        visual = 1;
    if (strcmp(prog, "view") == 0)
        ro_from_R = 1;
    /* "+command" is an alternative to -c command */
    args = xmalloc((size_t)(argc * 2 + 1) * sizeof(char *));
    args[nargv++] = argv[0];
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            for (; i < argc; i++)
                args[nargv++] = argv[i];
            break;
        }
        if (argv[i][0] == '+') {
            args[nargv++] = "-c";
            args[nargv++] = argv[i][1] ? argv[i] + 1 : "$";
            continue;
        }
        args[nargv++] = argv[i];
        if (argv[i][0] != '-')
            continue;
        /* options with arguments: keep the argument */
        if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "-w") == 0) &&
            i + 1 < argc)
            args[nargv++] = argv[++i];
    }
    args[nargv] = NULL;
    opterr = 0;
    while ((opt = getopt(nargv, args, ":c:rRst:vw:")) != -1) {
        switch (opt) {
        case 'c':
            c_cmds = xrealloc(c_cmds, (size_t)(n_c_cmds + 1) * sizeof(char *));
            c_cmds[n_c_cmds++] = optarg;
            break;
        case 'r':
            rflag = 1;
            break;
        case 'R':
            ro_from_R = 1;
            break;
        case 's':
            sflag = 1;
            break;
        case 't':
            if (tag) {
                diag("only one -t option is allowed");
                exit(2);
            }
            tag = optarg;
            break;
        case 'v':
            visual = 1;
            break;
        case 'w': {
            char *e;
            long v = strtol(optarg, &e, 10);
            if (*e || v < 0) {
                diag("-w: invalid size %s", optarg);
                exit(2);
            }
            wsize = (int)v;
            break;
        }
        case ':':
            diag("option requires an argument -- %c", optopt);
            usage(prog);
            break;
        default:
            diag("illegal option -- %c", optopt);
            usage(prog);
        }
    }
    if (sflag && visual && strcmp(prog, "ex") == 0) {
        /* -s and -v are mutually exclusive; -s wins for ex */
        visual = 0;
    }
    interactive = isatty(0);
    batch_stdin = !interactive;
    silent = sflag || batch_stdin;
    if (visual && batch_stdin)
        visual = 0;
    term_get_size();
    opts_init();
    if (ro_from_R)
        opts[OP_READONLY].num = 1;
    if (sflag) {
        free(OS(OP_TERM));
        OS(OP_TERM) = xstrdup("dumb");
    }
    if (wsize >= 0) {
        window_from_w = 1;
        opt_window_set(wsize);
    }
    install(SIGINT, on_int);
    install(SIGHUP, on_hup);
    install(SIGTERM, on_hup);
    install(SIGWINCH, on_winch);
    install(SIGCONT, on_cont);
    if (interactive)
        signal(SIGQUIT, SIG_IGN);
    if (rflag && optind >= nargv && !tag) {
        recover_list();
        exit(0);
    }
    if (!silent)
        initialize();
    if (optind < nargv) {
        int n = nargv - optind;
        char **files = xmalloc((size_t)n * sizeof(char *));
        for (i = 0; i < n; i++)
            files[i] = xstrdup(args[optind + i]);
        arg_list_set(files, n);
    }
    if (visual && !term_capable_visual()) {
        diag("%s: terminal type does not support visual mode; using ex mode", OS(OP_TERM));
        visual = 0;
    }
    if (visual)
        mode = MODE_VI;
    {
        jmp_buf jb;
        errjmp = &jb;
        if (setjmp(jb) == 0) {
            if (tag) {
                argidx = -1;
                tag_goto(tag, 1);
                if (!curpath && nlines == 0)
                    edit_file(NULL, 1, NULL, 1);
            } else if (nargs > 0)
                edit_file(arglist[0], 1, NULL, 1);
            else
                edit_file(NULL, 1, NULL, 1);
        } else {
            if (batch_stdin)
                ex_exit(1);
            if (tag && !curpath && nlines == 0) {
                if (setjmp(jb) == 0)
                    edit_file(NULL, 1, NULL, 1);
            }
        }
        errjmp = NULL;
    }
    if (visual) {
        mode = MODE_VI;
        vi_main(cur, 0, 0);
        mode = MODE_EX;
    }
    ex_loop();
    return 0;
}
