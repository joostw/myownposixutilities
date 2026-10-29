/*
 * main.c - invocation (sh utility) and the top-level command loop.
 */
#include "sh.h"
#include <poll.h>

extern char **environ;
extern int process_set_options(int argc, char **argv, int *setargs, int invocation);
extern void parser_reset(void);
extern void var_clear_nonexported(void);

int interactive;
int shell_exiting;
char *pending_script, **pending_script_argv;

static int toplevel_input_depth;
static int terminal_gone(void);

void sh_exit_now(int status)
{
    flushall();
    _exit(status & 0xff);
}

void exitshell(int status)
{
    static int exiting;
    if (exiting) {
        /* exit invoked from the EXIT trap: leave immediately */
        flushall();
        _exit(status);
    }
    exiting = 1;
    shell_exiting = 1;
    if (trap_actions[0]) {
        char *t = trap_actions[0];
        struct jmploc jl;
        trap_actions[0] = NULL;
        if (!setjmp(jl.loc)) {
            handler = &jl;
            in_trap++;
            trap_status_before = status;
            exitstatus = status;
            evalskip = 0;
            eval_string(t, 0);
            in_trap--;
        }
        handler = NULL;
    }
    flushall();
    if (interactive && !in_subshell)
        history_save();
    _exit(status & 0xff);
}

static void reinit_for_script(void)
{
    int i, fd, hfd;
    char *path = pending_script;
    char **argv = pending_script_argv;
    int argc = 0;
    while (argv[argc]) argc++;
    pending_script = NULL;
    func_clear_all();
    alias_unset_all();
    var_clear_nonexported();
    var_set("IFS", " \t\n", 0);
    var_set("OPTIND", "1", 0);
    for (i = 0; i < NSIGS; i++) {
        if (trap_actions[i] && *trap_actions[i]) {
            free(trap_actions[i]);
            trap_actions[i] = NULL;
            if (i) set_signal(i);
        }
    }
    for (i = 0; i < NOPTS; i++)
        opts[i] = 0;
    evalskip = skipcount = loopnest = funcnest = dotnest = 0;
    while (input_depth() > 0)
        pop_input();
    parser_reset();
    clear_redir_saves();
    params_set(argv + 1, argc - 1);
    arg_zero = xstrdup(argv[0]);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        sh_warn("%s: cannot open: %s", path, strerror(errno));
        sh_exit_now(127);
    }
    hfd = fcntl(fd, F_DUPFD_CLOEXEC, 100);
    if (hfd < 0) hfd = fcntl(fd, F_DUPFD_CLOEXEC, 10);
    close(fd);
    push_input_fd(hfd, 0);
    parse_lineno = 1;
    toplevel_input_depth = input_depth();
    exitstatus = 0;
}

static void main_loop(void)
{
    struct jmploc jl;
    volatile int eof = 0;
    for (;;) {
        if (setjmp(jl.loc)) {
            int e = exception;
            handler = &jl;
            if (e == EX_EXEC) {
                reinit_for_script();
                continue;
            }
            if (e == EX_EXIT)
                exitshell(exitstatus);
            /* EX_ERROR, EX_INT */
            if (!interactive || in_subshell) {
                if (e == EX_INT)
                    exitshell(128 + SIGINT);
                exitshell(exitstatus ? exitstatus : 2);
            }
            if (e == EX_INT) {
                exitstatus = exitstatus ? exitstatus : 130;
            }
            while (input_depth() > toplevel_input_depth)
                pop_input();
            input_discard_line();
            parser_reset();
            evalskip = skipcount = loopnest = funcnest = dotnest = 0;
            errexit_suppressed = 0;
            in_trap = 0;
            builtin_running = 0;
            commandname = NULL;
            undo_redirects(0);
            intpending = 0;
        }
        handler = &jl;
        for (;;) {
            StackMark m;
            Node *n;
            int e2;
            setstackmark(&m);
            if (gotsig)
                run_pending_traps();
            if (interactive)
                intpending = 0;
            n = parse_command(&e2);
            eof = e2;
            if (eof) {
                popstackmark(&m);
                if (interactive && opts[O_IGNOREEOF] && !in_subshell && !terminal_gone()) {
                    outs(&out2, "Use \"exit\" to leave the shell.\n");
                    outflush(&out2);
                    input_reset_eof();
                    continue;
                }
                if (interactive) {
                    outc(&out2, '\n');
                    outflush(&out2);
                }
                exitshell(exitstatus);
            }
            input_sync();
            if (n)
                eval_tree(n, 0);
            if (evalskip) {
                /* return outside a function or dot script */
                evalskip = 0;
                skipcount = 0;
            }
            if (gotsig)
                run_pending_traps();
            popstackmark(&m);
        }
    }
}

/*
 * ignoreeof keeps the shell going after an end-of-file typed at the
 * terminal, but not after the terminal has gone away (a hangup with
 * SIGHUP ignored): every read then returns end-of-file at once, and the
 * shell would never stop.  A disconnected terminal is reported by
 * poll() as POLLHUP (XBD 11.1.10 and poll()); a typed ^D is not.
 */
static int terminal_gone(void)
{
    struct pollfd p;
    p.fd = 0;
    p.events = POLLIN;
    p.revents = 0;
    if (poll(&p, 1, 0) < 0)
        return errno != EINTR;
    return (p.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0;
}

static int is_ifs_whitespace_ok(void) { return 1; }

int main(int argc, char **argv)
{
    int i, setargs, first;
    static char *cmdstr;
    static char *file;
    static int explicit_m;
    explicit_m = -1;

    setlocale(LC_ALL, "");
    shell_pid = getpid();
    arg0 = argv[0] ? argv[0] : "sh";
    {
        const char *b = strrchr(arg0, '/');
        if (b) arg0 = b + 1;
        if (*arg0 == '-') arg0++;
        if (!*arg0) arg0 = "sh";
    }
    var_init(environ);
    arg_zero = argv[0] ? argv[0] : "sh";

    {
        struct jmploc jl;
        if (setjmp(jl.loc)) {
            /* invalid invocation option */
            sh_exit_now(2);
        }
        handler = &jl;
        /* track whether -m/+m was explicitly given */
        for (i = 1; i < argc && (argv[i][0] == '-' || argv[i][0] == '+') && argv[i][1]; i++) {
            if (strcmp(argv[i], "--") == 0) break;
            if (strchr(argv[i], 'm') && argv[i][1] != 'o')
                explicit_m = argv[i][0] == '-';
            if ((argv[i][1] == 'o' && !argv[i][2] && i + 1 < argc) &&
                strcmp(argv[i + 1], "monitor") == 0)
                explicit_m = argv[i][0] == '-';
        }
        first = process_set_options(argc, argv, &setargs, 1);
        handler = NULL;
    }
    if (first < argc && strcmp(argv[first], "-") == 0 && !(first > 1 && strcmp(argv[first - 1], "--") == 0))
        first++;

    if (opts[O_CMDSTR]) {
        if (first >= argc) {
            sh_warn("-c requires an argument");
            sh_exit_now(2);
        }
        cmdstr = argv[first++];
        if (first < argc)
            arg_zero = argv[first++];
    } else if (!opts[O_STDIN] && first < argc) {
        file = argv[first++];
        arg_zero = file;
    } else
        opts[O_STDIN] = 1;
    params_set(argv + first, argc - first);

    if (opts[O_INTERACTIVE] ||
        (!cmdstr && !file && isatty(0) && isatty(2))) {
        opts[O_INTERACTIVE] = 1;
        interactive = 1;
        if (explicit_m != 0)
            opts[O_MONITOR] = 1;
    }
    signals_init();
    jobs_init();
    pwd_init();
    (void)is_ifs_whitespace_ok;

    {
        struct jmploc jl;
        handler = &jl;
        if (setjmp(jl.loc)) {
            if (exception == EX_EXEC) {
                handler = NULL;
                goto run;   /* handled in main_loop */
            }
            if (!interactive)
                exitshell(exitstatus ? exitstatus : 2);
        } else {
            if (interactive) {
                history_init();
                /* ENV processing */
                if (getuid() == geteuid() && getgid() == getegid()) {
                    const char *env = var_get("ENV");
                    if (env && *env) {
                        char *path = expand_prompt_params(env);
                        if (path && *path)
                            run_file(path, 0);
                    }
                }
            }
        }
        handler = NULL;
    }
run:
    if (cmdstr) {
        if (!*cmdstr)
            sh_exit_now(0);
        push_input_string(xstrdup(cmdstr), 1);
    } else if (file) {
        int fd = open(file, O_RDONLY), hfd;
        if (fd < 0 && !strchr(file, '/') && errno == ENOENT) {
            /* optionally search PATH */
            char *p = find_command(file, NULL, NULL);
            if (p)
                fd = open(p, O_RDONLY);
            errno = ENOENT;
        }
        if (fd < 0) {
            sh_warn("%s: cannot open: %s", file, strerror(errno));
            sh_exit_now(127);
        }
        hfd = fcntl(fd, F_DUPFD_CLOEXEC, 100);
        if (hfd < 0) hfd = fcntl(fd, F_DUPFD_CLOEXEC, 10);
        close(fd);
        push_input_fd(hfd, 0);
    } else {
        push_input_fd(0, interactive && isatty(0));
    }
    toplevel_input_depth = input_depth();
    if (pending_script)
        reinit_for_script();
    main_loop();
    return 0;
}
