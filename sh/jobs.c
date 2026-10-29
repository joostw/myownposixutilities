/*
 * jobs.c - job control (2.11), waiting, signals and traps (2.12).
 */
#include "sh.h"
#include <termios.h>

Job *jobtab;
int njobs;
int curjob = -1, prevjob = -1;
pid_t shell_pgid;
int tty_fd = -1;
static int jobctl_on;
static struct termios shell_tmodes;
static int have_tmodes;

char *trap_actions[NSIGS];
volatile sig_atomic_t pending_sig[NSIGS];
volatile sig_atomic_t gotsig;
volatile sig_atomic_t intpending;
int sig_ignored_at_entry[NSIGS];
char **saved_parent_traps;
int in_trap;
int trap_status_before;

/* ------------------------------------------------------------------ */
/* signal names                                                       */
/* ------------------------------------------------------------------ */

static const struct { const char *name; int num; } signames[] = {
    {"HUP", SIGHUP}, {"INT", SIGINT}, {"QUIT", SIGQUIT}, {"ILL", SIGILL},
    {"TRAP", SIGTRAP}, {"ABRT", SIGABRT}, {"BUS", SIGBUS}, {"FPE", SIGFPE},
    {"KILL", SIGKILL}, {"USR1", SIGUSR1}, {"SEGV", SIGSEGV}, {"USR2", SIGUSR2},
    {"PIPE", SIGPIPE}, {"ALRM", SIGALRM}, {"TERM", SIGTERM}, {"CHLD", SIGCHLD},
    {"CONT", SIGCONT}, {"STOP", SIGSTOP}, {"TSTP", SIGTSTP}, {"TTIN", SIGTTIN},
    {"TTOU", SIGTTOU}, {"URG", SIGURG}, {"XCPU", SIGXCPU}, {"XFSZ", SIGXFSZ},
    {"VTALRM", SIGVTALRM}, {"PROF", SIGPROF}, {"SYS", SIGSYS},
#ifdef SIGWINCH
    {"WINCH", SIGWINCH},
#endif
#ifdef SIGPOLL
    {"POLL", SIGPOLL},
#endif
    {NULL, 0}
};

int sig_by_name(const char *name)
{
    int i;
    char up[32];
    size_t k;
    if (is_number(name)) {
        int n = atoi(name);
        return n >= 0 && n < NSIGS ? n : -1;
    }
    for (k = 0; name[k] && k < sizeof up - 1; k++)
        up[k] = (char)toupper((unsigned char)name[k]);
    up[k] = 0;
    name = up;
    if (strncmp(name, "SIG", 3) == 0)
        name += 3;
    if (strcmp(name, "EXIT") == 0)
        return 0;
    for (i = 0; signames[i].name; i++)
        if (strcmp(signames[i].name, name) == 0)
            return signames[i].num;
#ifdef SIGRTMIN
    if (strncmp(name, "RTMIN", 5) == 0) {
        int off = name[5] == '+' ? atoi(name + 6) : 0;
        if (SIGRTMIN + off <= SIGRTMAX && SIGRTMIN + off < NSIGS)
            return SIGRTMIN + off;
    }
    if (strncmp(name, "RTMAX", 5) == 0) {
        int off = name[5] == '-' ? atoi(name + 6) : 0;
        if (SIGRTMAX - off >= SIGRTMIN && SIGRTMAX - off < NSIGS)
            return SIGRTMAX - off;
    }
#endif
    return -1;
}

const char *sig_name(int sig)
{
    static char buf[16];
    int i;
    if (sig == 0)
        return "EXIT";
    for (i = 0; signames[i].name; i++)
        if (signames[i].num == sig)
            return signames[i].name;
#ifdef SIGRTMIN
    if (sig >= SIGRTMIN && sig <= SIGRTMAX) {
        if (sig == SIGRTMIN) return "RTMIN";
        if (sig == SIGRTMAX) return "RTMAX";
        snprintf(buf, sizeof buf, "RTMIN+%d", sig - SIGRTMIN);
        return buf;
    }
#endif
    snprintf(buf, sizeof buf, "%d", sig);
    return buf;
}

/* ------------------------------------------------------------------ */
/* signal dispositions                                                */
/* ------------------------------------------------------------------ */

static void on_sig(int sig)
{
    if (sig > 0 && sig < NSIGS)
        pending_sig[sig] = 1;
    gotsig = 1;
    if (sig == SIGINT && !trap_actions[SIGINT])
        intpending = 1;
}

static void setact(int sig, void (*h)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = h;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;    /* no SA_RESTART: let waits notice */
    sigaction(sig, &sa, NULL);
}

void set_signal(int sig)
{
    const char *t = trap_actions[sig];
    if (sig <= 0 || sig >= NSIGS || sig == SIGKILL || sig == SIGSTOP)
        return;
    if (sig_ignored_at_entry[sig] && !interactive) {
        setact(sig, SIG_IGN);
        return;
    }
    if (t && *t) {
        setact(sig, on_sig);
        return;
    }
    if (t && !*t) {
        setact(sig, SIG_IGN);
        return;
    }
    if (interactive) {
        if (sig == SIGINT) { setact(sig, on_sig); return; }
        if (sig == SIGQUIT || sig == SIGTERM) { setact(sig, SIG_IGN); return; }
        if (jobctl_on && (sig == SIGTSTP || sig == SIGTTIN || sig == SIGTTOU)) {
            setact(sig, SIG_IGN);
            return;
        }
    }
    if (sig == SIGCHLD) {
        if (opts[O_NOTIFY] && interactive && jobctl_on)
            setact(sig, on_sig);
        else
            setact(sig, SIG_DFL);
        return;
    }
    setact(sig, SIG_DFL);
}

/* set -b: asynchronous notification of background job completion */
void set_notify(int on)
{
    (void)on;
    set_signal(SIGCHLD);
}

/* called when reading input is interrupted: report finished jobs now */
int notify_pending(void)
{
    if (pending_sig[SIGCHLD] && opts[O_NOTIFY] && interactive && jobctl_on) {
        pending_sig[SIGCHLD] = 0;
        outc(&out2, '\n');
        jobs_notify();
        return 1;
    }
    return 0;
}

void signals_init(void)
{
    int i;
    for (i = 1; i < NSIGS; i++) {
        struct sigaction sa;
        if (sigaction(i, NULL, &sa) == 0 && sa.sa_handler == SIG_IGN)
            sig_ignored_at_entry[i] = 1;
    }
    if (interactive) {
        set_signal(SIGINT);
        set_signal(SIGQUIT);
        set_signal(SIGTERM);
    }
}

/* in a child about to run something: restore default dispositions */
void signals_child_reset(int async_nojc)
{
    int i;
    for (i = 1; i < NSIGS; i++) {
        struct sigaction sa;
        if (i == SIGKILL || i == SIGSTOP)
            continue;
        if (sigaction(i, NULL, &sa) < 0)
            continue;
        if (sa.sa_handler != SIG_IGN && sa.sa_handler != SIG_DFL)
            setact(i, SIG_DFL);
        else if (sa.sa_handler == SIG_IGN && !sig_ignored_at_entry[i] &&
                 !(trap_actions[i] && !*trap_actions[i]))
            setact(i, SIG_DFL);
    }
    if (async_nojc) {
        setact(SIGINT, SIG_IGN);
        setact(SIGQUIT, SIG_IGN);
    }
}

void reset_traps_for_subshell(void)
{
    int i;
    if (!saved_parent_traps) {
        saved_parent_traps = xcalloc(NSIGS, sizeof(char *));
        for (i = 0; i < NSIGS; i++)
            if (trap_actions[i])
                saved_parent_traps[i] = xstrdup(trap_actions[i]);
    }
    for (i = 0; i < NSIGS; i++) {
        if (trap_actions[i] && *trap_actions[i]) {
            free(trap_actions[i]);
            trap_actions[i] = NULL;
            if (i)
                set_signal(i);
        }
        pending_sig[i] = 0;
    }
    gotsig = 0;
    /* an interactive shell's own handling does not apply in subshells */
    for (i = 1; i < NSIGS; i++)
        if (i == SIGINT || i == SIGQUIT || i == SIGTERM || i == SIGTSTP ||
            i == SIGTTIN || i == SIGTTOU)
            set_signal(i);
}

void run_pending_traps(void)
{
    int i;
    if (in_trap)
        return;
    while (gotsig) {
        gotsig = 0;
        for (i = 1; i < NSIGS; i++) {
            if (!pending_sig[i])
                continue;
            pending_sig[i] = 0;
            if (trap_actions[i] && *trap_actions[i]) {
                int save = exitstatus;
                int saveskip = evalskip;
                char *act = xstrdup(trap_actions[i]);
                in_trap++;
                trap_status_before = save;
                evalskip = 0;
                eval_string(act, 0);
                free(act);
                in_trap--;
                if (!evalskip)
                    evalskip = saveskip;
                exitstatus = save;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* jobs                                                               */
/* ------------------------------------------------------------------ */

int wait_status_to_exit(int st)
{
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    if (WIFSIGNALED(st))
        return 128 + WTERMSIG(st);
    if (WIFSTOPPED(st))
        return 128 + WSTOPSIG(st);
    return st;
}

int job_ctl_enabled(void) { return jobctl_on; }

static void take_terminal(pid_t pg)
{
    if (tty_fd >= 0 && jobctl_on) {
        sigset_t s, o;
        sigemptyset(&s);
        sigaddset(&s, SIGTTOU);
        sigprocmask(SIG_BLOCK, &s, &o);
        tcsetpgrp(tty_fd, pg);
        sigprocmask(SIG_SETMASK, &o, NULL);
    }
}

void set_job_control(int on)
{
    if (on == jobctl_on)
        return;
    if (on) {
        int fd;
        if (tty_fd < 0) {
            fd = open("/dev/tty", O_RDWR);
            if (fd < 0) {
                if (isatty(2)) fd = dup(2);
                else if (isatty(0)) fd = dup(0);
            }
            if (fd < 0) {
                if (interactive)
                    sh_warn("can't access tty; job control turned off");
                opts[O_MONITOR] = 0;
                return;
            }
            tty_fd = fcntl(fd, F_DUPFD_CLOEXEC, 100);
            if (tty_fd < 0)
                tty_fd = fcntl(fd, F_DUPFD_CLOEXEC, 10);
            close(fd);
        }
        for (;;) {
            pid_t fg = tcgetpgrp(tty_fd);
            if (fg < 0) {
                close(tty_fd);
                tty_fd = -1;
                opts[O_MONITOR] = 0;
                return;
            }
            if (fg == getpgrp())
                break;
            kill(0, SIGTTIN);
        }
        shell_pgid = getpid();
        setpgid(0, shell_pgid);
        jobctl_on = 1;
        take_terminal(shell_pgid);
        if (tcgetattr(tty_fd, &shell_tmodes) == 0)
            have_tmodes = 1;
        set_signal(SIGTSTP);
        set_signal(SIGTTIN);
        set_signal(SIGTTOU);
        set_signal(SIGCHLD);
    } else {
        jobctl_on = 0;
        set_signal(SIGTSTP);
        set_signal(SIGTTIN);
        set_signal(SIGTTOU);
    }
}

void set_job_control_child(void)
{
    jobctl_on = 0;
    opts[O_MONITOR] = 0;
}

void jobs_init(void)
{
    shell_pgid = getpgrp();
    if (opts[O_MONITOR]) {
        opts[O_MONITOR] = 0;
        set_job_control(1);
        opts[O_MONITOR] = (char)jobctl_on;
    }
}

static int alloc_jobnum(void)
{
    int n = 1, i, used;
    do {
        used = 0;
        for (i = 0; i < njobs; i++)
            if (jobtab[i].used && jobtab[i].num == n) {
                used = 1;
                n++;
                break;
            }
    } while (used);
    return n;
}

static void prune_done(void)
{
    int i, ndone = 0;
    for (i = 0; i < njobs; i++)
        if (jobtab[i].used && jobtab[i].bg && jobtab[i].state == JOB_DONE)
            ndone++;
    if (ndone < 1000)
        return;
    for (i = 0; i < njobs && ndone > 500; i++)
        if (jobtab[i].used && jobtab[i].bg && jobtab[i].state == JOB_DONE) {
            job_free(&jobtab[i]);
            ndone--;
        }
}

Job *job_new(int nprocs, int bg)
{
    int i;
    Job *j;
    prune_done();
    for (i = 0; i < njobs; i++)
        if (!jobtab[i].used)
            break;
    if (i == njobs) {
        int nn = njobs ? njobs * 2 : 16, k;
        jobtab = xrealloc(jobtab, (size_t)nn * sizeof *jobtab);
        for (k = njobs; k < nn; k++)
            jobtab[k].used = 0;
        njobs = nn;
    }
    j = &jobtab[i];
    memset(j, 0, sizeof *j);
    j->used = 1;
    j->num = bg ? alloc_jobnum() : 0;
    j->bg = bg;
    j->jobctl = jobctl_on;
    j->procs = xcalloc((size_t)(nprocs > 0 ? nprocs : 1), sizeof(Proc));
    j->nprocs = 0;
    j->state = JOB_RUNNING;
    (void)nprocs;
    return j;
}

void job_add_proc(Job *j, pid_t pid)
{
    j->procs = xrealloc(j->procs, (size_t)(j->nprocs + 1) * sizeof(Proc));
    j->procs[j->nprocs].pid = pid;
    j->procs[j->nprocs].status = 0;
    j->procs[j->nprocs].state = JOB_RUNNING;
    j->nprocs++;
}

void job_free(Job *j)
{
    int idx = (int)(j - jobtab);
    free(j->procs);
    free(j->cmd);
    j->procs = NULL;
    j->cmd = NULL;
    j->used = 0;
    if (curjob == idx) {
        curjob = prevjob;
        prevjob = -1;
    } else if (prevjob == idx)
        prevjob = -1;
    if (curjob < 0) {
        int i;
        for (i = njobs - 1; i >= 0; i--)
            if (jobtab[i].used && jobtab[i].bg && jobtab[i].state != JOB_DONE) {
                curjob = i;
                break;
            }
    }
}

void job_set_current(Job *j)
{
    int idx = (int)(j - jobtab);
    if (curjob == idx)
        return;
    prevjob = curjob;
    curjob = idx;
}

/* parent's jobs, kept for display by "jobs" in a subshell */
Job *parent_jobs;
int nparent_jobs;
int parent_curjob = -1, parent_prevjob = -1;

void jobs_forget_all(void)
{
    if (njobs && !parent_jobs) {
        parent_jobs = jobtab;
        nparent_jobs = njobs;
        parent_curjob = curjob;
        parent_prevjob = prevjob;
        jobtab = NULL;
        njobs = 0;
    } else {
        int i;
        for (i = 0; i < njobs; i++)
            if (jobtab[i].used) {
                free(jobtab[i].procs);
                free(jobtab[i].cmd);
                jobtab[i].used = 0;
            }
    }
    curjob = prevjob = -1;
}

int job_status(Job *j)
{
    int i, st;
    if (j->nprocs == 0)
        return 0;
    if (j->pipefail) {
        st = 0;
        for (i = 0; i < j->nprocs; i++) {
            int s = wait_status_to_exit(j->procs[i].status);
            if (s)
                st = s;
        }
        return st;
    }
    return wait_status_to_exit(j->procs[j->nprocs - 1].status);
}

static void update_job_state(Job *j)
{
    int i, running = 0, stopped = 0;
    for (i = 0; i < j->nprocs; i++) {
        if (j->procs[i].state == JOB_RUNNING) running++;
        else if (j->procs[i].state == JOB_STOPPED) stopped++;
    }
    {
        int old = j->state;
        if (running)
            j->state = JOB_RUNNING;
        else if (stopped)
            j->state = JOB_STOPPED;
        else
            j->state = JOB_DONE;
        if (old != j->state)
            j->changed = 1;
    }
}

static Job *find_proc(pid_t pid, Proc **pp)
{
    int i, k;
    for (i = 0; i < njobs; i++) {
        if (!jobtab[i].used)
            continue;
        for (k = 0; k < jobtab[i].nprocs; k++)
            if (jobtab[i].procs[k].pid == pid) {
                *pp = &jobtab[i].procs[k];
                return &jobtab[i];
            }
    }
    return NULL;
}

static void record_status(pid_t pid, int st)
{
    Proc *p;
    Job *j = find_proc(pid, &p);
    if (!j)
        return;
    p->status = st;
    if (WIFSTOPPED(st))
        p->state = JOB_STOPPED;
    else if (WIFCONTINUED(st))
        p->state = JOB_RUNNING;
    else
        p->state = JOB_DONE;
    update_job_state(j);
}

/* collect status changes of background children */
void jobs_reap(int block)
{
    for (;;) {
        int st;
        pid_t pid = waitpid(-1, &st, (block ? 0 : WNOHANG) | WUNTRACED | WCONTINUED);
        if (pid <= 0) {
            if (pid < 0 && errno == EINTR && block)
                return;
            return;
        }
        record_status(pid, st);
        if (block)
            return;
    }
}

char *job_state_str(Job *j, char *buf, size_t n)
{
    if (j->state == JOB_RUNNING)
        snprintf(buf, n, "Running");
    else if (j->state == JOB_STOPPED) {
        int i, sig = SIGTSTP;
        for (i = 0; i < j->nprocs; i++)
            if (j->procs[i].state == JOB_STOPPED)
                sig = WSTOPSIG(j->procs[i].status);
        if (sig == SIGTSTP)
            snprintf(buf, n, "Stopped");
        else
            snprintf(buf, n, "Stopped (SIG%s)", sig_name(sig));
    } else {
        int st = j->procs[j->nprocs - 1].status;
        if (j->pipefail) {
            int s = job_status(j);
            if (s == 0) snprintf(buf, n, "Done");
            else snprintf(buf, n, "Done(%d)", s);
        } else if (WIFSIGNALED(st)) {
            const char *d = strsignal(WTERMSIG(st));
            snprintf(buf, n, "Terminated (%s)", d ? d : sig_name(WTERMSIG(st)));
        } else if (WEXITSTATUS(st))
            snprintf(buf, n, "Done(%d)", WEXITSTATUS(st));
        else
            snprintf(buf, n, "Done");
    }
    return buf;
}

static char job_mark(Job *j)
{
    int idx = (int)(j - jobtab);
    return idx == curjob ? '+' : idx == prevjob ? '-' : ' ';
}

void print_job(Out *o, Job *j, int longfmt)
{
    char st[128];
    job_state_str(j, st, sizeof st);
    if (longfmt) {
        int i;
        outfmt(o, "[%d] %c %ld %s %s\n", j->num, job_mark(j),
               (long)(j->jobctl ? j->pgid : j->procs[0].pid), st, j->cmd ? j->cmd : "");
        for (i = 1; i < j->nprocs; i++)
            outfmt(o, "      %ld\n", (long)j->procs[i].pid);
    } else
        outfmt(o, "[%d] %c %s %s\n", j->num, job_mark(j), st, j->cmd ? j->cmd : "");
}

/* report changed background jobs (before a prompt) */
void jobs_notify(void)
{
    int i;
    if (!interactive)
        return;
    jobs_reap(0);
    for (i = 0; i < njobs; i++) {
        Job *j = &jobtab[i];
        if (!j->used || !j->bg || !j->changed)
            continue;
        j->changed = 0;
        if (j->state == JOB_RUNNING)
            continue;
        print_job(&out2, j, 0);
        if (j->state == JOB_DONE)
            job_free(j);
    }
    outflush(&out2);
}

Job *job_by_pid(pid_t pid)
{
    Proc *p;
    return find_proc(pid, &p);
}

/* parse a job ID (XBD 3.182) */
Job *job_by_spec(const char *spec, int report)
{
    int i;
    Job *found = NULL;
    if (spec[0] != '%') {
        if (is_number(spec)) {
            pid_t pid = (pid_t)atol(spec);
            for (i = 0; i < njobs; i++)
                if (jobtab[i].used && jobtab[i].nprocs &&
                    (jobtab[i].pgid == pid || jobtab[i].procs[0].pid == pid))
                    return &jobtab[i];
            return job_by_pid(pid);
        }
        goto none;
    }
    spec++;
    if (!*spec || strcmp(spec, "%") == 0 || strcmp(spec, "+") == 0) {
        if (curjob >= 0 && jobtab[curjob].used)
            return &jobtab[curjob];
        goto none;
    }
    if (strcmp(spec, "-") == 0) {
        if (prevjob >= 0 && jobtab[prevjob].used)
            return &jobtab[prevjob];
        goto none;
    }
    if (is_number(spec)) {
        int n = atoi(spec);
        for (i = 0; i < njobs; i++)
            if (jobtab[i].used && jobtab[i].bg && jobtab[i].num == n)
                return &jobtab[i];
        goto none;
    }
    for (i = 0; i < njobs; i++) {
        Job *j = &jobtab[i];
        int m;
        if (!j->used || !j->bg || !j->cmd)
            continue;
        if (spec[0] == '?')
            m = strstr(j->cmd, spec + 1) != NULL;
        else
            m = strncmp(j->cmd, spec, strlen(spec)) == 0;
        if (m) {
            if (found) {
                if (report)
                    sh_warn("%%%s: ambiguous job specification", spec);
                return NULL;
            }
            found = j;
        }
    }
    if (found)
        return found;
none:
    if (report)
        sh_warn("%s: no such job", spec);
    return NULL;
}

/* wait for a foreground job */
int job_wait(Job *j)
{
    int i, st;
    for (i = 0; i < j->nprocs; i++) {
        while (j->procs[i].state == JOB_RUNNING) {
            pid_t r = waitpid(j->procs[i].pid, &st, j->jobctl ? WUNTRACED : 0);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                j->procs[i].state = JOB_DONE;
                j->procs[i].status = 0;
                break;
            }
            j->procs[i].status = st;
            j->procs[i].state = WIFSTOPPED(st) ? JOB_STOPPED : JOB_DONE;
        }
    }
    update_job_state(j);
    if (j->jobctl) {
        take_terminal(shell_pgid);
        if (have_tmodes && tty_fd >= 0)
            tcsetattr(tty_fd, TCSADRAIN, &shell_tmodes);
    }
    if (j->state == JOB_STOPPED) {
        int sig = SIGTSTP;
        for (i = 0; i < j->nprocs; i++)
            if (j->procs[i].state == JOB_STOPPED)
                sig = WSTOPSIG(j->procs[i].status);
        j->bg = 1;
        j->num = alloc_jobnum();
        j->changed = 0;
        job_set_current(j);
        if (interactive) {
            outc(&out2, '\n');
            print_job(&out2, j, 0);
            outflush(&out2);
        }
        return 128 + sig;
    }
    st = job_status(j);
    {
        int killed_int = 0;
        for (i = 0; i < j->nprocs; i++)
            if (WIFSIGNALED(j->procs[i].status) && WTERMSIG(j->procs[i].status) == SIGINT)
                killed_int = 1;
        if (!j->bg)
            job_free(j);
        if (killed_int && interactive) {
            outc(&out2, '\n');
            outflush(&out2);
            exitstatus = st;
            raise_exc(EX_INT);
        }
    }
    return st;
}

/* continue a job in the foreground (fg) or background (bg) */
int job_continue(Job *j, int fg)
{
    int i;
    if (fg) {
        j->bg = 0;
        if (j->jobctl)
            take_terminal(j->pgid);
    }
    for (i = 0; i < j->nprocs; i++)
        if (j->procs[i].state == JOB_STOPPED)
            j->procs[i].state = JOB_RUNNING;
    j->state = JOB_RUNNING;
    if (j->jobctl)
        killpg(j->pgid, SIGCONT);
    else
        for (i = 0; i < j->nprocs; i++)
            kill(j->procs[i].pid, SIGCONT);
    if (fg) {
        int st;
        int keepnum = j->num;
        (void)keepnum;
        st = job_wait(j);
        return st;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* deparsing (for job listings)                                       */
/* ------------------------------------------------------------------ */

static void dp_word(StrBuf *b, const char *p, Word *w)
{
    while (*p) {
        char c = *p++;
        switch (c) {
        case CTLESC:
            if (*p) {
                if (strchr("|&;<>()$`\\\"' \t\n*?[#~", *p))
                    sb_addc(b, '\\');
                sb_addc(b, *p++);
            }
            break;
        case CTLQUOTE: sb_addc(b, '"'); break;
        case CTLENDQ: sb_addc(b, '"'); break;
        case CTLQNULL: break;
        case CTLVAR: {
            int sub = (unsigned char)*p++;
            int colon = sub >= VS_NORMAL + VS_COLON_OFF;
            if (colon) sub -= VS_COLON_OFF;
            sb_adds(b, "${");
            if (sub == VS_LENGTH) sb_addc(b, '#');
            while (*p && *p != '=') sb_addc(b, *p++);
            if (*p) p++;
            if (colon) sb_addc(b, ':');
            switch (sub) {
            case VS_MINUS: sb_addc(b, '-'); break;
            case VS_PLUS: sb_addc(b, '+'); break;
            case VS_QUESTION: sb_addc(b, '?'); break;
            case VS_ASSIGN: sb_addc(b, '='); break;
            case VS_TRIMLEFT: sb_addc(b, '#'); break;
            case VS_TRIMLEFTMAX: sb_adds(b, "##"); break;
            case VS_TRIMRIGHT: sb_addc(b, '%'); break;
            case VS_TRIMRIGHTMAX: sb_adds(b, "%%"); break;
            }
            break;
        }
        case CTLENDVAR: sb_addc(b, '}'); break;
        case CTLBACKQ: {
            int idx = atoi(p);
            while (*p && *p != ';') p++;
            if (*p) p++;
            sb_adds(b, "$(");
            if (w && idx < w->nsubs) {
                char *t = node_text(w->subs[idx]);
                sb_adds(b, t);
                free(t);
            }
            sb_addc(b, ')');
            break;
        }
        case CTLARI: sb_adds(b, "$(("); break;
        case CTLENDARI: sb_adds(b, "))"); break;
        default: sb_addc(b, c);
        }
    }
}

static void dp(StrBuf *b, Node *n);

static void dp_redirs(StrBuf *b, Redir *r)
{
    static const char *ops[] = { "<", ">", ">|", ">>", "<>", "<&", ">&", "<<", "<<-" };
    for (; r; r = r->next) {
        int deffd = (r->type == R_IN || r->type == R_RDWR || r->type == R_DUPIN ||
                     r->type == R_HERE || r->type == R_HERESTRIP) ? 0 : 1;
        sb_addc(b, ' ');
        if (r->fd != deffd)
            sb_printf(b, "%d", r->fd);
        sb_adds(b, ops[r->type]);
        if (r->word)
            dp_word(b, r->word->s, r->word);
    }
}

static void dp(StrBuf *b, Node *n)
{
    if (!n)
        return;
    switch (n->type) {
    case N_SIMPLE: {
        Word *w;
        int first = 1;
        for (w = n->u.simple.assigns; w; w = w->next) {
            if (!first) sb_addc(b, ' ');
            dp_word(b, w->s, w);
            first = 0;
        }
        for (w = n->u.simple.words; w; w = w->next) {
            if (!first) sb_addc(b, ' ');
            dp_word(b, w->s, w);
            first = 0;
        }
        dp_redirs(b, n->redir);
        return;
    }
    case N_PIPE: {
        int i;
        for (i = 0; i < n->u.pipe.n; i++) {
            if (i) sb_adds(b, " | ");
            dp(b, n->u.pipe.cmds[i]);
        }
        return;
    }
    case N_AND: dp(b, n->u.bin.a); sb_adds(b, " && "); dp(b, n->u.bin.b); return;
    case N_OR: dp(b, n->u.bin.a); sb_adds(b, " || "); dp(b, n->u.bin.b); return;
    case N_LIST: dp(b, n->u.bin.a); sb_adds(b, "; "); dp(b, n->u.bin.b); return;
    case N_ASYNC: dp(b, n->u.un.a); sb_adds(b, " &"); return;
    case N_NOT: sb_adds(b, "! "); dp(b, n->u.un.a); return;
    case N_SUBSHELL: sb_adds(b, "("); dp(b, n->u.un.a); sb_adds(b, ")"); break;
    case N_BRACE: sb_adds(b, "{ "); dp(b, n->u.un.a); sb_adds(b, "; }"); break;
    case N_IF:
        sb_adds(b, "if "); dp(b, n->u.nif.cond); sb_adds(b, "; then ");
        dp(b, n->u.nif.then);
        if (n->u.nif.els) { sb_adds(b, "; else "); dp(b, n->u.nif.els); }
        sb_adds(b, "; fi");
        break;
    case N_WHILE: case N_UNTIL:
        sb_adds(b, n->type == N_WHILE ? "while " : "until ");
        dp(b, n->u.loop.cond); sb_adds(b, "; do "); dp(b, n->u.loop.body);
        sb_adds(b, "; done");
        break;
    case N_FOR: {
        Word *w;
        sb_printf(b, "for %s", n->u.nfor.var);
        if (n->u.nfor.has_in) {
            sb_adds(b, " in");
            for (w = n->u.nfor.words; w; w = w->next) {
                sb_addc(b, ' ');
                dp_word(b, w->s, w);
            }
        }
        sb_adds(b, "; do "); dp(b, n->u.nfor.body); sb_adds(b, "; done");
        break;
    }
    case N_CASE: {
        CaseItem *ci;
        sb_adds(b, "case ");
        dp_word(b, n->u.ncase.word->s, n->u.ncase.word);
        sb_adds(b, " in");
        for (ci = n->u.ncase.items; ci; ci = ci->next) {
            Word *p;
            sb_addc(b, ' ');
            for (p = ci->patterns; p; p = p->next) {
                if (p != ci->patterns) sb_addc(b, '|');
                dp_word(b, p->s, p);
            }
            sb_adds(b, ") ");
            dp(b, ci->body);
            sb_adds(b, ci->fallthrough ? ";&" : ";;");
        }
        sb_adds(b, " esac");
        break;
    }
    case N_FUNCDEF:
        sb_printf(b, "%s() ", n->u.func.name);
        dp(b, n->u.func.body);
        return;
    }
    dp_redirs(b, n->redir);
}

char *node_text(Node *n)
{
    StrBuf b;
    sb_init(&b);
    dp(&b, n);
    return sb_take(&b);
}
