/*
 * exfile.c - files, the argument list, shell escapes and filters,
 * preservation and recovery, tags, and the remaining ex commands.
 */
#include "ex.h"
#include "cmd.h"
#include <dirent.h>
#include <pwd.h>

char **arglist;
int nargs, argidx;
char *last_bang_cmd;
char *recover_path, *preserve_path;
char **c_cmds;            /* -c commands */
int n_c_cmds, c_cmds_done;
int rflag;                /* -r */
static char **edited;     /* files edited so far (for -r) */
static int nedited;
int ro_from_R;            /* -R */

/* remembered positions of files edited before */
typedef struct FilePos {
    char *path;
    int line, off;
    struct FilePos *next;
} FilePos;
static FilePos *filepos;
static char *last_file_edited;

void ex_exit(int status)
{
    if (vi_active)
        vs_suspend();
    out_flush();
    if (interactive && term_raw)
        term_raw_off();
    exit(status);
}

void hup_exit(void)
{
    if (modified && nlines >= 0)
        preserve_buffer(1);
    ex_exit(1);
}

/* ---------- file name expansion ---------- */

static int same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    if (!a || !b)
        return 0;
    if (strcmp(a, b) == 0)
        return 1;
    if (stat(a, &sa) == 0 && stat(b, &sb) == 0)
        return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
    return 0;
}

static void shquote(Str *b, const char *s)
{
    str_addc(b, '\'');
    for (; *s; s++) {
        if (*s == '\'')
            str_adds(b, "'\\''");
        else
            str_addc(b, *s);
    }
    str_addc(b, '\'');
}

/* run "$shell -c 'echo text'" and return its output */
char *shell_echo(const char *text)
{
    Str cmd, out;
    char *r;
    str_init(&cmd);
    str_init(&out);
    str_adds(&cmd, "echo ");
    str_adds(&cmd, text);
    run_shell(cmd.s, 0, 0, 0, &out, 0);
    str_free(&cmd);
    while (out.len && (out.s[out.len - 1] == '\n' || out.s[out.len - 1] == '\r'))
        out.s[--out.len] = 0;
    r = str_detach(&out);
    return r;
}

/*
 * Expand a file argument: % and # (unless escaped), backslash escapes and,
 * when shell metacharacters are present, the shell.  multi: several file
 * names allowed (next); otherwise more than one is an error.
 */
char *expand_file_arg(const char *arg, int multi, char ***listp, int *np)
{
    Str lit, sh;
    int need_shell = 0, ntok = 0;
    char **list = NULL;
    int n = 0;
    const char *p = skipbl(arg);
    str_init(&lit);
    str_init(&sh);
    /* trim trailing blanks */
    {
        size_t l = strlen(p);
        while (l > 0 && (p[l - 1] == ' ' || p[l - 1] == '\t') && !(l > 1 && p[l - 2] == '\\'))
            l--;
        arg = p = xstrndup(p, l);
    }
    for (; *p; p++) {
        int c = (unsigned char)*p;
        if (c == '\\' && p[1]) {
            char t[2] = { p[1], 0 };
            p++;
            str_addc(&lit, p[0]);
            if (p[0] == ' ' || p[0] == '\t' || strchr("~{[*?$\"`'\\|&;<>()", p[0]))
                shquote(&sh, t);
            else
                str_addc(&sh, p[0]);
            continue;
        }
        if (c == '%' || c == '#') {
            const char *v = c == '%' ? curpath : altpath;
            if (!v) {
                free((char *)arg);
                str_free(&lit);
                str_free(&sh);
                ex_err(c == '%' ? "No current filename to substitute for %%"
                                : "No alternate filename to substitute for #");
            }
            str_adds(&lit, v);
            shquote(&sh, v);
            continue;
        }
        if (c == ' ' || c == '\t') {
            /* argument separator */
            while (p[1] == ' ' || p[1] == '\t')
                p++;
            list = xrealloc(list, (size_t)(n + 1) * sizeof(char *));
            list[n++] = xstrdup(lit.s ? lit.s : "");
            str_clear(&lit);
            str_addc(&sh, ' ');
            ntok++;
            continue;
        }
        if (strchr("~{[*?$\"`'", c))
            need_shell = 1;
        str_addc(&lit, c);
        str_addc(&sh, c);
    }
    if (lit.len || n == 0) {
        list = xrealloc(list, (size_t)(n + 1) * sizeof(char *));
        list[n++] = xstrdup(lit.s ? lit.s : "");
    }
    if (n == 1 && list[0][0] == 0) {
        free(list[0]);
        n = 0;
    }
    if (need_shell) {
        char *out = shell_echo(sh.s ? sh.s : "");
        for (int i = 0; i < n; i++)
            free(list[i]);
        n = 0;
        if (multi) {
            char *tok, *save = NULL;
            for (tok = strtok_r(out, " \t\n", &save); tok; tok = strtok_r(NULL, " \t\n", &save)) {
                list = xrealloc(list, (size_t)(n + 1) * sizeof(char *));
                list[n++] = xstrdup(tok);
            }
        } else if (*out) {
            list = xrealloc(list, sizeof(char *));
            list[n++] = xstrdup(out);
            if (strchr(out, '\n') || (strchr(out, ' ') && !strchr(sh.s, '\\') && !strchr(sh.s, '"') &&
                                       !strchr(sh.s, '\''))) {
                free(out);
                free((char *)arg);
                str_free(&lit);
                str_free(&sh);
                ex_err("Too many file names");
            }
        }
        free(out);
    }
    free((char *)arg);
    str_free(&lit);
    str_free(&sh);
    if (!multi && n > 1) {
        for (int i = 0; i < n; i++)
            free(list[i]);
        free(list);
        ex_err("Too many file names");
    }
    if (listp) {
        *listp = list;
        *np = n;
        return n ? list[0] : NULL;
    }
    {
        char *r = n ? list[0] : NULL;
        for (int i = 1; i < n; i++)
            free(list[i]);
        free(list);
        return r;
    }
}

/* expand % # ! in a shell command */
char *expand_bang(const char *cmd, int *changed)
{
    Str b;
    *changed = 0;
    str_init(&b);
    for (const char *p = cmd; *p; p++) {
        if (*p == '\\' && (p[1] == '%' || p[1] == '#' || p[1] == '!')) {
            str_addc(&b, p[1]);
            p++;
            *changed = 1;
            continue;
        }
        if (*p == '%' || *p == '#') {
            const char *v = *p == '%' ? curpath : altpath;
            if (!v) {
                str_free(&b);
                ex_err(*p == '%' ? "No current filename to substitute for %%"
                                 : "No alternate filename to substitute for #");
            }
            str_adds(&b, v);
            *changed = 1;
            continue;
        }
        if (*p == '!') {
            if (!last_bang_cmd) {
                str_free(&b);
                ex_err("No previous command to substitute for !");
            }
            str_adds(&b, last_bang_cmd);
            *changed = 1;
            continue;
        }
        str_addc(&b, *p);
    }
    return str_detach(&b);
}

/* ---------- running the shell ---------- */

static int write_lines_fd(int fd, int a, int b)
{
    Str buf;
    str_init(&buf);
    for (int i = a; i >= 1 && i <= b; i++) {
        str_addn(&buf, LT(i), (size_t)LL(i));
        str_addc(&buf, '\n');
        if (buf.len > 65536 || i == b) {
            size_t off = 0;
            while (off < buf.len) {
                ssize_t n = write(fd, buf.s + off, buf.len - off);
                if (n < 0) {
                    if (errno == EINTR)
                        continue;
                    str_free(&buf);
                    return -1;
                }
                off += (size_t)n;
            }
            str_clear(&buf);
        }
    }
    str_free(&buf);
    return 0;
}

/*
 * Run cmd with the shell edit option.  in_lines: give lines a..b as the
 * standard input (otherwise ex's standard input).  capture: collect the
 * standard output and standard error.  tty_io: the command talks to the
 * terminal directly.
 */
int run_shell(const char *cmd, int in_lines, int a, int b, Str *capture, int tty_io)
{
    int pfd[2] = { -1, -1 }, infd = -1, status = 0;
    pid_t pid;
    char tmpl[] = "/tmp/exXXXXXX";
    const char *sh = OS(OP_SHELL) && *OS(OP_SHELL) ? OS(OP_SHELL) : "sh";
    struct sigaction sa, oint, oquit;
    if (in_lines) {
        infd = mkstemp(tmpl);
        if (infd < 0)
            ex_err("Unable to create temporary file: %s", strerror(errno));
        unlink(tmpl);
        if (write_lines_fd(infd, a, b) < 0) {
            close(infd);
            ex_err("Unable to write temporary file: %s", strerror(errno));
        }
        lseek(infd, 0, SEEK_SET);
    } else
        restore_stdin_position();
    if (capture && pipe(pfd) < 0) {
        if (infd >= 0)
            close(infd);
        ex_err("pipe: %s", strerror(errno));
    }
    out_flush();
    if (tty_io && vi_active)
        vs_suspend();
    else if (vi_active && !in_lines && term_raw)
        term_raw_off();
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &oint);
    sigaction(SIGQUIT, &sa, &oquit);
    pid = fork();
    if (pid < 0) {
        sigaction(SIGINT, &oint, NULL);
        sigaction(SIGQUIT, &oquit, NULL);
        if (infd >= 0)
            close(infd);
        if (pfd[0] >= 0) {
            close(pfd[0]);
            close(pfd[1]);
        }
        ex_err("fork: %s", strerror(errno));
    }
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGWINCH, SIG_DFL);
        signal(SIGCONT, SIG_DFL);
        if (infd >= 0) {
            dup2(infd, 0);
            close(infd);
        }
        if (capture) {
            dup2(pfd[1], 1);
            dup2(pfd[1], 2);
            close(pfd[0]);
            close(pfd[1]);
        }
        execl(sh, sh, "-c", cmd, (char *)NULL);
        if (!strchr(sh, '/'))
            execlp(sh, sh, "-c", cmd, (char *)NULL);
        _exit(127);
    }
    if (infd >= 0)
        close(infd);
    if (capture) {
        char buf[4096];
        ssize_t n;
        close(pfd[1]);
        for (;;) {
            n = read(pfd[0], buf, sizeof buf);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            str_addn(capture, buf, (size_t)n);
        }
        close(pfd[0]);
    }
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    sigaction(SIGINT, &oint, NULL);
    sigaction(SIGQUIT, &oquit, NULL);
    intr = 0;
    if (tty_io && vi_active)
        vs_resume();
    else if (vi_active && !in_lines && !term_raw)
        term_raw_on();
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

/* split captured output into lines and insert them after `after` */
static int insert_output(Str *out, int after)
{
    int n = 0;
    size_t start = 0;
    if (!out->s)
        return 0;
    for (size_t i = 0; i <= out->len; i++) {
        if (i == out->len || out->s[i] == '\n') {
            size_t e = i;
            if (i == out->len && i == start)
                break;
            if (e > start && out->s[e - 1] == '\r' && i < out->len)
                e--;
            {
                /* drop NUL bytes */
                Str l;
                str_init(&l);
                for (size_t k = start; k < e; k++)
                    if (out->s[k])
                        str_addc(&l, out->s[k]);
                buf_insert1(after + n, l.s ? l.s : "", (int)l.len);
                str_free(&l);
            }
            n++;
            start = i + 1;
        }
    }
    return n;
}

int filter_lines(int a, int b, const char *cmd, int replace)
{
    Str out;
    int n;
    (void)replace;
    str_init(&out);
    run_shell(cmd, 1, a, b, &out, 0);
    reg_store_lines(0, a, b, 0);
    buf_delete(a, b);
    n = insert_output(&out, a - 1);
    str_free(&out);
    return n;
}

/* ---------- reading and writing files ---------- */

int read_file_into(const char *path, int after, long *nl, long *nb, int *incomplete, int *exists)
{
    int fd = open(path, O_RDONLY);
    struct stat st;
    char *data = NULL;
    size_t len = 0, cap = 0;
    long lines = 0;
    int load = after < 0;
    *nl = *nb = 0;
    *incomplete = 0;
    *exists = 0;
    if (fd < 0)
        return -1;
    *exists = 1;
    if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
        close(fd);
        errno = EISDIR;
        return -1;
    }
    for (;;) {
        ssize_t n;
        if (len + 65536 > cap) {
            cap = cap ? cap * 2 : 131072;
            data = xrealloc(data, cap);
        }
        n = read(fd, data + len, cap - len);
        if (n < 0) {
            if (errno == EINTR) {
                if (intr)
                    break;
                continue;
            }
            free(data);
            close(fd);
            return -1;
        }
        if (n == 0)
            break;
        len += (size_t)n;
    }
    close(fd);
    *nb = (long)len;
    {
        size_t start = 0;
        int at = load ? 0 : after;
        Str clean;
        str_init(&clean);
        for (size_t i = 0; i <= len; i++) {
            if (i == len || data[i] == '\n') {
                const char *s = data + start;
                size_t l = i - start;
                if (i == len && l == 0)
                    break;
                if (i == len)
                    *incomplete = 1;
                if (OV(OP_BEAUTIFY) || memchr(s, 0, l)) {
                    str_clear(&clean);
                    for (size_t k = 0; k < l; k++) {
                        unsigned char c = (unsigned char)s[k];
                        if (c == 0)
                            continue;
                        if (OV(OP_BEAUTIFY) && c < 0x20 && c != '\t' && c != '\f')
                            continue;
                        str_addc(&clean, (char)c);
                    }
                    s = clean.s ? clean.s : "";
                    l = clean.len;
                }
                if (load)
                    buf_load_line(s, (int)l);
                else
                    buf_insert1(at + (int)lines, s, (int)l);
                lines++;
                start = i + 1;
            }
        }
        str_free(&clean);
    }
    free(data);
    *nl = lines;
    return 0;
}

int file_write(int a, int b, const char *path, int append, int whole, int force)
{
    int fd, flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), istty;
    long nb = 0, nl = 0;
    Str buf;
    (void)force;
    fd = open(path, flags, 0666);
    if (fd < 0)
        ex_err("%s: %s", path, strerror(errno));
    istty = isatty(fd);
    str_init(&buf);
    for (int i = a; i >= 1 && i <= b && nlines > 0; i++) {
        str_addn(&buf, LT(i), (size_t)LL(i));
        str_addc(&buf, '\n');
        nl++;
        if (buf.len >= 65536 || i == b) {
            size_t off = 0;
            while (off < buf.len) {
                ssize_t n = write(fd, buf.s + off, buf.len - off);
                if (n < 0) {
                    int e = errno;
                    if (e == EINTR)
                        continue;
                    close(fd);
                    str_free(&buf);
                    ex_err("%s: %s", path, strerror(e));
                }
                off += (size_t)n;
            }
            nb += (long)buf.len;
            str_clear(&buf);
        }
    }
    str_free(&buf);
    if (close(fd) < 0)
        ex_err("%s: %s", path, strerror(errno));
    msg("\"%s\"%s %ld line%s, %ld byte%s", path, append ? " [Appended]" : "", nl,
        nl == 1 ? "" : "s", nb, nb == 1 ? "" : "s");
    if (whole && !append && !istty) {
        modified = 0;
        if (same_file(path, curpath)) {
            path_changed_by_file = 0;
            recover_cleanup();
        }
    }
    return 0;
}

static int file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

int autowrite_check(int force)
{
    if (!modified || force)
        return 0;
    if (OV(OP_AUTOWRITE) && curpath) {
        if (OV(OP_READONLY))
            ex_err("Read-only file; not written");
        if (path_changed_by_file && file_exists(curpath) && !OV(OP_WRITEANY))
            ex_err("%s exists, not written", curpath);
        file_write(1, nlines, curpath, 0, 1, 0);
        return 0;
    }
    ex_err("No write since last change (add ! to override)");
}

/* ---------- editing a file ---------- */

static void remember_pos(void)
{
    FilePos *f;
    if (!curpath)
        return;
    for (f = filepos; f; f = f->next)
        if (strcmp(f->path, curpath) == 0)
            break;
    if (!f) {
        f = xcalloc(1, sizeof *f);
        f->path = xstrdup(curpath);
        f->next = filepos;
        filepos = f;
    }
    f->line = cur;
    f->off = cur_off;
}

static FilePos *find_pos(const char *path)
{
    for (FilePos *f = filepos; f; f = f->next)
        if (strcmp(f->path, path) == 0)
            return f;
    return NULL;
}

static void set_readonly_for(const char *path)
{
    if (opts[OP_READONLY].user_set)
        return;
    opts[OP_READONLY].num = ro_from_R;
    if (path && access(path, F_OK) == 0 && access(path, W_OK) != 0)
        opts[OP_READONLY].num = 1;
}

static int was_edited(const char *path)
{
    for (int i = 0; i < nedited; i++)
        if (strcmp(edited[i], path) == 0)
            return 1;
    edited = xrealloc(edited, (size_t)(nedited + 1) * sizeof(char *));
    edited[nedited++] = xstrdup(path);
    return 0;
}

static void run_c_cmds(void)
{
    c_cmds_done = 1;
    cur = nlines;
    set_nonblank();
    for (int i = 0; i < n_c_cmds; i++)
        if (ex_run(c_cmds[i], NULL) < 0)
            break;
}

int edit_ctx_tag;   /* the edit is part of a tag command */

/*
 * Replace the edit buffer with the contents of path.  pluscmd: a +command.
 */
int edit_file(const char *path, int force, const char *pluscmd, int is_initial)
{
    long nl = 0, nb = 0;
    int incomplete = 0, exists = 0, r, recovered = 0, older = 0, same, prev_same;
    char *newpath = path ? xstrdup(path) : (curpath ? xstrdup(curpath) : NULL);
    (void)force;
    same = newpath && curpath && strcmp(newpath, curpath) == 0;
    prev_same = newpath && last_file_edited && strcmp(newpath, last_file_edited) == 0 && !same;
    remember_pos();
    if (curpath) {
        free(last_file_edited);
        last_file_edited = xstrdup(curpath);
    }
    buf_reset();
    for (int i = 0; i < 27; i++)
        marks[i].set = 0;
    buffer_generation++;
    modified = 0;
    path_changed_by_file = 0;
    cur = 0;
    cur_off = 0;
    if (newpath && !same) {
        if (curpath) {
            free(altpath);
            altpath = curpath;
        }
        curpath = NULL;
    }
    if (newpath) {
        free(curpath);
        curpath = newpath;
    }
    set_readonly_for(curpath);
    if (curpath && rflag && !was_edited(curpath)) {
        recovered = recover_file(curpath, &older);
        if (!recovered)
            msg("No recovery information for %s", curpath);
    } else if (curpath)
        was_edited(curpath);
    if (!recovered && curpath) {
        r = read_file_into(curpath, -1, &nl, &nb, &incomplete, &exists);
        if (r < 0 && exists) {
            int e = errno;
            msg("\"%s\": %s", curpath, strerror(e));
            buf_reset();
        } else if (r < 0)
            msg("\"%s\" [New file]", curpath);
        else
            msg("\"%s\"%s%s %ld line%s, %ld byte%s", curpath, OV(OP_READONLY) ? " [Read only]" : "",
                incomplete ? " [Incomplete last line]" : "", nl, nl == 1 ? "" : "s", nb,
                nb == 1 ? "" : "s");
    }
    if (recovered) {
        modified = 1;
        exists = 1;
        if (older)
            msg("There are older versions of this file to recover");
    }
    /* initial position */
    if (nlines == 0) {
        cur = 0;
        cur_off = 0;
        want_col = 0;
    } else if (mode == MODE_EX || pluscmd) {
        cur = nlines;
        set_nonblank();
    } else if (!path || same) {
        cur = 1;
        set_nonblank();
    } else if (prev_same || find_pos(curpath)) {
        FilePos *f = find_pos(curpath);
        if (f && f->line >= 1 && f->line <= nlines) {
            cur = f->line;
            cur_off = f->off < LL(cur) ? f->off : 0;
            want_col = col_of(LT(cur), LL(cur), cur_off);
        } else {
            cur = 1;
            set_nonblank();
        }
    } else {
        cur = 1;
        set_nonblank();
    }
    if (mode != MODE_EX || nlines > 0) {
        if (mode != MODE_EX)
            mark_set(MARK_CONTEXT, cur, cur_off);
        else
            mark_set(MARK_CONTEXT, cur, first_nonblank(cur));
    }
    if (exists && !c_cmds_done && n_c_cmds > 0) {
        c_cmds_done = 1;
        if (!edit_ctx_tag)
            run_c_cmds();
        else
            c_cmds_done = 2;   /* run by the tag code */
    } else if (exists && !c_cmds_done)
        c_cmds_done = 1;
    if (pluscmd && *pluscmd)
        ex_run(pluscmd, NULL);
    (void)is_initial;
    return 0;
}

/* parse "[+command] file" for e, ex, n, vi */
static const char *parse_plus(const char *p, char **plus)
{
    p = skipbl(p);
    *plus = NULL;
    if (*p == '+') {
        Str b;
        str_init(&b);
        p++;
        while (*p && *p != ' ' && *p != '\t') {
            if (*p == '\\' && p[1]) {
                p++;
                str_addc(&b, *p++);
                continue;
            }
            str_addc(&b, *p++);
        }
        *plus = str_detach(&b);
        if (!**plus) {
            free(*plus);
            *plus = xstrdup("$");
        }
    }
    return p;
}

void c_edit(Cmd *c)
{
    char *plus, *file;
    const char *p = parse_plus(c->arg, &plus);
    file = expand_file_arg(p, 0, NULL, NULL);
    if (!file && !curpath) {
        free(plus);
        ex_err("No current filename");
    }
    if (!c->bang && modified) {
        if (file && (!curpath || strcmp(file, curpath) != 0)) {
            free(altpath);
            altpath = file;
            file = NULL;
        }
        free(plus);
        free(file);
        ex_err("No write since last change (:edit! overrides)");
    }
    edit_file(file, c->bang, plus, 0);
    free(file);
    free(plus);
}

void arg_list_set(char **files, int n)
{
    for (int i = 0; i < nargs; i++)
        free(arglist[i]);
    free(arglist);
    arglist = files;
    nargs = n;
    argidx = 0;
}

void c_next(Cmd *c)
{
    char *plus, **files = NULL;
    int n = 0;
    const char *p = parse_plus(c->arg, &plus);
    p = skipbl(p);
    if (*p)
        expand_file_arg(p, 1, &files, &n);
    if (!n && argidx + 1 >= nargs) {
        free(plus);
        free(files);
        ex_err("No more files to edit");
    }
    {
        jmp_buf jb, *saved = errjmp;
        errjmp = &jb;
        if (setjmp(jb)) {
            errjmp = saved;
            for (int i = 0; i < n; i++)
                free(files[i]);
            free(files);
            free(plus);
            ex_err_silent();
        }
        autowrite_check(c->bang);
        errjmp = saved;
    }
    if (n) {
        arg_list_set(files, n);
        argidx = 0;
    } else
        argidx++;
    edit_file(arglist[argidx], 1, plus, 0);
    free(plus);
}

void c_rewind(Cmd *c)
{
    end_args(c->arg);
    if (nargs == 0)
        ex_err("No argument list");
    autowrite_check(c->bang);
    argidx = 0;
    edit_file(arglist[0], 1, NULL, 0);
}

void c_args(Cmd *c)
{
    Str b;
    end_args(c->arg);
    str_init(&b);
    if (nargs == 0) {
        msg("No file names");
        return;
    }
    for (int i = 0; i < nargs; i++) {
        if (i)
            str_addc(&b, ' ');
        if (i == argidx)
            str_fmt(&b, "[%s]", arglist[i]);
        else
            str_adds(&b, arglist[i]);
    }
    str_addc(&b, '\n');
    out_write(b.s, b.len);
    str_free(&b);
}

void show_file_info(void)
{
    Str b;
    if (silent && !vi_active)
        return;
    str_init(&b);
    if (curpath)
        str_fmt(&b, "\"%s\"", curpath);
    else
        str_adds(&b, "[No file]");
    if (modified)
        str_adds(&b, " [Modified]");
    if (OV(OP_READONLY))
        str_adds(&b, " [Read only]");
    if (nlines > 0)
        str_fmt(&b, " line %d of %d --%d%%--", cur, nlines, (int)((long)cur * 100 / nlines));
    else
        str_adds(&b, " --No lines in buffer--");
    if (vi_active)
        vs_message(b.s, 0);
    else {
        str_addc(&b, '\n');
        out_write(b.s, b.len);
    }
    str_free(&b);
}

void c_file(Cmd *c)
{
    char *file = expand_file_arg(c->arg, 0, NULL, NULL);
    if (file) {
        free(altpath);
        altpath = curpath;
        curpath = file;
        path_changed_by_file = 1;
    }
    show_file_info();
}

void c_read(Cmd *c)
{
    long nl = 0, nb = 0;
    int incomplete, exists, after = c->a1, n;
    if (c->shell) {
        int changed;
        char *cmd = expand_bang(c->arg, &changed);
        Str out;
        free(last_bang_cmd);
        last_bang_cmd = xstrdup(cmd);
        if (changed && !silent) {
            out_str("!");
            out_str(cmd);
            out_str("\n");
        }
        str_init(&out);
        run_shell(cmd, 0, 0, 0, &out, 0);
        free(cmd);
        n = insert_output(&out, after);
        str_free(&out);
        if (n > 0) {
            cur = (vi_active) ? after + 1 : after + n;
            set_nonblank();
        }
        report_changes(0, "more", 0);
        return;
    }
    {
        char *file = expand_file_arg(c->arg, 0, NULL, NULL);
        const char *path = file ? file : curpath;
        long before = change_count;
        if (!path)
            ex_err("No current filename");
        if (file) {
            if (!curpath) {
                curpath = xstrdup(file);
                path_changed_by_file = 1;
            } else if (strcmp(file, curpath) != 0) {
                free(altpath);
                altpath = xstrdup(file);
            }
        }
        if (read_file_into(path, after, &nl, &nb, &incomplete, &exists) < 0) {
            int e = errno;
            char *pp = xstrdup(path);
            free(file);
            ex_err("\"%s\": %s", pp, strerror(e));
        }
        msg("\"%s\"%s %ld line%s, %ld byte%s", path, incomplete ? " [Incomplete last line]" : "", nl,
            nl == 1 ? "" : "s", nb, nb == 1 ? "" : "s");
        free(file);
        if (nl > 0) {
            cur = (vi_active) ? after + 1 : after + (int)nl;
            set_nonblank();
        }
        (void)before;
        c->no_autoprint = 1;
    }
}

static void do_quit(int force)
{
    int prev = ex_quit_pending;
    ex_quit_pending = 1;
    if (!force && modified)
        ex_err("File modified since last complete write; write or use ! to override");
    if (!force && argidx + 1 < nargs && !prev)
        ex_err("%d more file%s to edit", nargs - argidx - 1, nargs - argidx - 1 == 1 ? "" : "s");
    ex_exit(0);
}

void c_quit(Cmd *c)
{
    end_args(c->arg);
    do_quit(c->bang);
}

/* the write command; returns after writing */
static void do_write(Cmd *c, const char *argp)
{
    const char *p = skipbl(argp);
    int append = 0, whole, specified;
    char *file;
    const char *path;
    if (c->shell) {
        int changed;
        char *cmd = expand_bang(c->arg, &changed);
        Str out;
        free(last_bang_cmd);
        last_bang_cmd = xstrdup(cmd);
        if (changed && !silent && !vi_active) {
            out_str("!");
            out_str(cmd);
            out_str("\n");
        }
        str_init(&out);
        run_shell(cmd, 1, c->a1, c->a2, &out, 0);
        free(cmd);
        if (out.len) {
            out_write(out.s, out.len);
            if (out.s[out.len - 1] != '\n')
                out_str("\n");
        }
        str_free(&out);
        return;
    }
    if (p[0] == '>' && p[1] == '>') {
        append = 1;
        p = skipbl(p + 2);
    }
    file = expand_file_arg(p, 0, NULL, NULL);
    specified = file != NULL;
    path = file ? file : curpath;
    if (!path)
        ex_err("No current filename");
    whole = nlines == 0 || (c->a1 <= 1 && c->a2 >= nlines);
    if (OV(OP_READONLY) && !c->bang) {
        free(file);
        ex_err("Read-only file; not written (use ! to override)");
    }
    if (!append && !c->bang && !OV(OP_WRITEANY) && file_exists(path)) {
        if (specified && !same_file(path, curpath)) {
            char *pp = xstrdup(path);
            free(file);
            ex_err("%s exists, not written (use ! to override)", pp);
        }
        if ((!specified || same_file(path, curpath)) && path_changed_by_file) {
            char *pp = xstrdup(path);
            free(file);
            ex_err("%s exists, not written (use ! to override)", pp);
        }
        if (!whole) {
            free(file);
            ex_err("Use w! to write a partial buffer");
        }
    }
    if (specified) {
        if (!curpath)
            curpath = xstrdup(file);
        else if (!same_file(file, curpath)) {
            free(altpath);
            altpath = xstrdup(file);
        }
    }
    {
        char *pp = xstrdup(path);
        free(file);
        file_write(c->a1, c->a2, pp, append, whole, c->bang);
        free(pp);
    }
}

void c_write(Cmd *c)
{
    int wq = c->def->name[1] == 'q';
    do_write(c, c->arg);
    if (wq)
        do_quit(c->bang);
}

void c_xit(Cmd *c)
{
    if (!modified) {
        char *file = expand_file_arg(c->arg, 0, NULL, NULL);
        if (file && !curpath)
            curpath = file;
        else
            free(file);
        do_quit(c->bang);
    }
    do_write(c, c->arg);
    do_quit(c->bang);
}

/* ---------- shell escapes ---------- */

void c_bang(Cmd *c)
{
    int changed, n;
    char *cmd = expand_bang(c->arg, &changed);
    free(last_bang_cmd);
    last_bang_cmd = xstrdup(cmd);
    if (changed && !silent && !vi_active) {
        out_str("!");
        out_str(cmd);
        out_str("\n");
    }
    if (c->naddr == 0) {
        jmp_buf jb, *saved = errjmp;
        errjmp = &jb;
        if (setjmp(jb)) {
            errjmp = saved;
            free(cmd);
            ex_err_silent();
        }
        if (modified && OV(OP_AUTOWRITE))
            autowrite_check(0);
        else if (modified && OV(OP_WARN))
            msg("File modified since last complete write.");
        errjmp = saved;
        run_shell(cmd, 0, 0, 0, NULL, 1);
        free(cmd);
        if (!vi_active && !silent)
            out_str("!\n");
        return;
    }
    {
        long a0 = rep_added, d0 = rep_deleted, a, d;
        int a1 = c->a1;
        n = filter_lines(c->a1, c->a2, cmd, 1);
        free(cmd);
        if (n > 0)
            cur = a1 + n - 1;
        else if (a1 - 1 >= 1)
            cur = a1 - 1;
        else
            cur = nlines > 0 ? 1 : 0;
        if (cur > nlines)
            cur = nlines;
        set_nonblank();
        a = rep_added - a0;
        d = rep_deleted - d0;
        (void)a;
        (void)d;
        if (!in_global && !in_exec_buffer && (c->a2 - c->a1 + 1) >= OV(OP_REPORT))
            msg("%d line%s filtered", c->a2 - c->a1 + 1, c->a2 - c->a1 == 0 ? "" : "s");
        if (!silent && !vi_active)
            out_str("!\n");
    }
}

void c_shell(Cmd *c)
{
    const char *sh = OS(OP_SHELL) && *OS(OP_SHELL) ? OS(OP_SHELL) : "sh";
    pid_t pid;
    int status;
    struct sigaction sa, oint, oquit;
    end_args(c->arg);
    out_flush();
    if (vi_active)
        vs_suspend();
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa, &oint);
    sigaction(SIGQUIT, &sa, &oquit);
    restore_stdin_position();
    pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGWINCH, SIG_DFL);
        execl(sh, sh, "-i", (char *)NULL);
        if (!strchr(sh, '/'))
            execlp(sh, sh, "-i", (char *)NULL);
        _exit(127);
    }
    if (pid > 0)
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
    sigaction(SIGINT, &oint, NULL);
    sigaction(SIGQUIT, &oquit, NULL);
    intr = 0;
    if (vi_active) {
        vs_resume();
        vs_redraw_all();
        vs_wait_needed = 0;    /* no prompt after the shell command */
    }
}

static int job_control(void)
{
    pid_t pg = getpgrp();
    if (!interactive)
        return 0;
    if (tcgetpgrp(0) != pg)
        return 0;
    return getpgid(getppid()) != pg;
}

void c_suspend(Cmd *c)
{
    end_args(c->arg);
    if (!job_control())
        ex_err("Job control is not enabled");
    autowrite_check(c->bang);
    out_flush();
    if (vi_active)
        vs_suspend();
    else if (term_raw)
        term_raw_off();
    {
        struct sigaction sa, old;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = SIG_DFL;
        sigaction(SIGTSTP, &sa, &old);
        kill(0, SIGTSTP);
        sigaction(SIGTSTP, &old, NULL);
    }
    if (vi_active) {
        vs_resume();
        vs_redraw_all();
    }
}

/* ---------- misc commands ---------- */

void c_cd(Cmd *c)
{
    char *dir = expand_file_arg(c->arg, 0, NULL, NULL);
    if (!dir) {
        const char *h = getenv("HOME");
        if (!h || !*h)
            h = "/";
        dir = xstrdup(h);
    }
    if (!c->bang && modified && curpath && curpath[0] != '/') {
        free(dir);
        ex_err("File modified since last write; use ! to override");
    }
    if (chdir(dir) < 0) {
        int e = errno;
        char *d = dir;
        ex_err("%s: %s", d, strerror(e));
    }
    free(dir);
}

void c_source(Cmd *c)
{
    char *file = expand_file_arg(c->arg, 0, NULL, NULL);
    int fd;
    Str text;
    char buf[4096];
    ssize_t n;
    if (!file)
        ex_err("No file name");
    fd = open(file, O_RDONLY);
    if (fd < 0) {
        int e = errno;
        ex_err("%s: %s", file, strerror(e));
    }
    free(file);
    str_init(&text);
    while ((n = read(fd, buf, sizeof buf)) > 0)
        str_addn(&text, buf, (size_t)n);
    close(fd);
    {
        int r = run_rc_text(text.s ? text.s : "");
        str_free(&text);
        if (r < 0)
            ex_err_silent();
    }
}

/* execute the text of a .exrc or sourced file: blank lines are ignored */
int run_rc_text(const char *text)
{
    Str t;
    const char *p = text;
    int r;
    str_init(&t);
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        int blank = 1;
        for (size_t i = 0; i < l; i++)
            if (p[i] != ' ' && p[i] != '\t')
                blank = 0;
        if (!blank) {
            str_addn(&t, p, l);
            str_addc(&t, '\n');
        }
        p += l;
        if (*p)
            p++;
    }
    r = t.s ? ex_run(t.s, NULL) : 0;
    str_free(&t);
    return r;
}

void c_set(Cmd *c) { cmd_set(c->arg); }

void c_version(Cmd *c)
{
    end_args(c->arg);
    if (vi_active)
        vs_message(VERSION, 0);
    else {
        out_str(VERSION);
        out_str("\n");
    }
}

void c_map(Cmd *c) { cmd_map(c->arg, c->bang, 0); }
void c_unmap(Cmd *c) { cmd_map(c->arg, c->bang, 1); }
void c_abbrev(Cmd *c) { cmd_abbrev(c->arg, 0); }
void c_unabbrev(Cmd *c) { cmd_abbrev(c->arg, 1); }

void c_preserve(Cmd *c)
{
    end_args(c->arg);
    preserve_buffer(1);
    msg("File preserved");
}

void c_recover(Cmd *c)
{
    char *file = expand_file_arg(c->arg, 0, NULL, NULL);
    int older = 0;
    if (!file && !curpath)
        ex_err("No current filename");
    if (!c->bang && modified) {
        free(file);
        ex_err("No write since last change (:recover! overrides)");
    }
    if (!file)
        file = xstrdup(curpath);
    {
        /* is there recovery information? */
        int saved_r = rflag;
        edit_ctx_tag = 0;
        if (!recover_available(file)) {
            msg("No recovery information for %s; editing normally", file);
            edit_file(file, 1, NULL, 0);
            free(file);
            return;
        }
        rflag = 0;
        edit_file(file, 1, NULL, 0);
        rflag = saved_r;
        if (recover_file(curpath, &older)) {
            modified = 1;
            if (nlines > 0) {
                cur = vi_active ? 1 : nlines;
                set_nonblank();
            }
            msg("File recovered%s", older ? "; there are older versions" : "");
        }
    }
    free(file);
}

void c_visual(Cmd *c)
{
    if (vi_active) {
        /* behaves like edit */
        c_edit(c);
        return;
    }
    {
        const char *p = skipbl(c->arg);
        int type = 0, count = 0, has;
        if (*p == '-' || *p == '+' || *p == '.' || *p == '^')
            type = *p++;
        has = parse_count(&p, &count);
        parse_flags(&p, &c->flags, &c->flagoff);
        end_args(p);
        c->flags = 0;
        c->flagoff = 0;
        if (!interactive || !isatty(1))
            ex_err("Visual mode requires a terminal");
        if (!term_capable_visual())
            ex_err("The %s terminal type does not support visual mode", OS(OP_TERM));
        if (has)
            opt_window_set(count), opts[OP_WINDOW].changed = 1;
        if (c->naddr && c->a1 >= 1)
            cur = c->a1;
        mode = MODE_VI;
        vi_main(cur, type ? type : '+', 0);
        mode = MODE_EX;
        c->no_autoprint = 1;
    }
}

void c_open(Cmd *c)
{
    const char *p = skipbl(c->arg);
    if (vi_active) {
        /* from visual mode: switch to open mode */
        if (c->a1 >= 1)
            cur = c->a1;
        set_nonblank();
        mode = MODE_OPEN;
        vs_redraw_all();
        c->no_autoprint = 1;
        return;
    }
    if (!interactive || !isatty(1))
        ex_err("Open mode requires a terminal");
    Regex *re = NULL;
    if (*p && !isalnum((unsigned char)*p) && *p != '|' && *p != '"' && *p != '\\') {
        int d = (unsigned char)*p++;
        re = get_re(&p, d, 1);
    }
    parse_flags(&p, &c->flags, &c->flagoff);
    end_args(p);
    c->flags = 0;
    c->flagoff = 0;
    if (c->a1 >= 1)
        cur = c->a1;
    set_nonblank();
    if (re && cur >= 1) {
        /* historically the cursor is placed on the match */
        Match m;
        if (re_exec(re, LT(cur), LL(cur), 0, 0, &m) && m.so[0] < LL(cur)) {
            cur_off = m.so[0];
            want_col = col_of(LT(cur), LL(cur), cur_off);
        }
    }
    re_free(re);
    if (!term_capable_visual())
        ex_err("The terminal does not support open mode");
    mode = MODE_OPEN;
    vi_main(cur, '+', 1);
    mode = MODE_EX;
    c->no_autoprint = 1;
}

/* ---------- preservation and recovery ---------- */

static const char REC_MAGIC[] = "ex-recover-1\n";

static char *rec_dir(void)
{
    const char *d = OS(OP_DIRECTORY);
    if (!d || !*d)
        d = "/tmp";
    return xstrdup(d);
}

static char *abs_path(const char *p)
{
    char buf[PATH_MAX];
    Str b;
    if (!p)
        return xstrdup("");
    if (p[0] == '/')
        return xstrdup(p);
    str_init(&b);
    if (getcwd(buf, sizeof buf))
        str_adds(&b, buf);
    str_addc(&b, '/');
    str_adds(&b, p);
    return str_detach(&b);
}

void mail_user(const char *subject, const char *body)
{
    const char *user = getenv("LOGNAME");
    struct passwd *pw;
    Str cmd;
    FILE *f;
    if (!user || !*user) {
        pw = getpwuid(getuid());
        user = pw ? pw->pw_name : NULL;
    }
    if (!user)
        return;
    str_init(&cmd);
    str_adds(&cmd, "mailx -s ");
    shquote(&cmd, subject);
    str_addc(&cmd, ' ');
    shquote(&cmd, user);
    str_adds(&cmd, " >/dev/null 2>&1");
    {
        sigset_t old, blk;
        sigemptyset(&blk);
        sigaddset(&blk, SIGCHLD);
        sigprocmask(SIG_BLOCK, &blk, &old);
        f = popen(cmd.s, "w");
        if (f) {
            fputs(body, f);
            pclose(f);
        }
        sigprocmask(SIG_SETMASK, &old, NULL);
    }
    str_free(&cmd);
}

void preserve_buffer(int mail)
{
    char *dir = rec_dir(), *ap = abs_path(curpath);
    int fd;
    Str hdr;
    time_t now = time(NULL);
    if (!preserve_path) {
        Str p;
        str_init(&p);
        str_fmt(&p, "%s/ex-%ld-%ld-XXXXXX", dir, (long)getuid(), (long)getpid());
        preserve_path = str_detach(&p);
        fd = mkstemp(preserve_path);
    } else
        fd = open(preserve_path, O_WRONLY | O_TRUNC | O_CREAT, 0600);
    free(dir);
    if (fd < 0) {
        free(ap);
        free(preserve_path);
        preserve_path = NULL;
        if (!got_hup)
            ex_err("Unable to preserve: %s", strerror(errno));
        return;
    }
    fchmod(fd, 0600);
    str_init(&hdr);
    str_adds(&hdr, REC_MAGIC);
    str_fmt(&hdr, "%s\n%ld\n", ap, (long)now);
    if (write(fd, hdr.s, hdr.len) < 0) {
        /* reported below via the write of lines */
    }
    write_lines_fd(fd, 1, nlines);
    close(fd);
    str_free(&hdr);
    if (mail) {
        Str body;
        char tb[64];
        struct tm *tm = localtime(&now);
        strftime(tb, sizeof tb, "%Y-%m-%d %H:%M:%S", tm);
        str_init(&body);
        str_fmt(&body,
                "The file %s was preserved by the editor at %s.\n"
                "To recover it, use the command:\n\n\tex -r %s\n",
                *ap ? ap : "(unnamed buffer)", tb, curpath ? curpath : "");
        mail_user("editor buffer preserved", body.s);
        str_free(&body);
    }
    free(ap);
}

typedef struct RecInfo {
    char *file, *path;
    long when;
} RecInfo;

static int read_rec_header(const char *file, RecInfo *ri)
{
    FILE *f = fopen(file, "r");
    char line[PATH_MAX + 64];
    struct stat st;
    if (!f)
        return 0;
    if (fstat(fileno(f), &st) < 0 || st.st_uid != getuid() || !S_ISREG(st.st_mode)) {
        fclose(f);
        return 0;
    }
    if (!fgets(line, sizeof line, f) || strcmp(line, REC_MAGIC) != 0) {
        fclose(f);
        return 0;
    }
    if (!fgets(line, sizeof line, f)) {
        fclose(f);
        return 0;
    }
    line[strcspn(line, "\n")] = 0;
    ri->path = xstrdup(line);
    if (!fgets(line, sizeof line, f)) {
        fclose(f);
        free(ri->path);
        return 0;
    }
    ri->when = atol(line);
    ri->file = xstrdup(file);
    fclose(f);
    return 1;
}

static int scan_recovery(RecInfo **out)
{
    char *dir = rec_dir();
    DIR *d = opendir(dir);
    struct dirent *e;
    RecInfo *list = NULL;
    int n = 0;
    char prefix[64];
    snprintf(prefix, sizeof prefix, "ex-%ld-", (long)getuid());
    if (d) {
        while ((e = readdir(d))) {
            RecInfo ri;
            Str f;
            if (strncmp(e->d_name, prefix, strlen(prefix)) != 0)
                continue;
            str_init(&f);
            str_fmt(&f, "%s/%s", dir, e->d_name);
            if (read_rec_header(f.s, &ri)) {
                list = xrealloc(list, (size_t)(n + 1) * sizeof *list);
                list[n++] = ri;
            }
            str_free(&f);
        }
        closedir(d);
    }
    free(dir);
    *out = list;
    return n;
}

static void free_recs(RecInfo *l, int n)
{
    for (int i = 0; i < n; i++) {
        free(l[i].file);
        free(l[i].path);
    }
    free(l);
}

void recover_list(void)
{
    RecInfo *l;
    int n = scan_recovery(&l);
    if (n == 0) {
        printf("No files to recover\n");
    }
    for (int i = 0; i < n; i++) {
        char tb[64];
        time_t t = (time_t)l[i].when;
        strftime(tb, sizeof tb, "%Y-%m-%d %H:%M:%S", localtime(&t));
        printf("%s: %s\n", tb, *l[i].path ? l[i].path : "(unnamed)");
    }
    fflush(stdout);
    free_recs(l, n);
}

int recover_available(const char *path)
{
    RecInfo *l;
    char *ap = abs_path(path);
    int n = scan_recovery(&l), found = 0;
    for (int i = 0; i < n; i++)
        if (strcmp(l[i].path, ap) == 0)
            found = 1;
    free_recs(l, n);
    free(ap);
    return found;
}

/* load the most recent recovery file for path into the (empty) buffer */
int recover_file(const char *path, int *older)
{
    RecInfo *l;
    char *ap = abs_path(path);
    int n = scan_recovery(&l), best = -1, count = 0;
    *older = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(l[i].path, ap) != 0)
            continue;
        count++;
        if (best < 0 || l[i].when > l[best].when ||
            (l[i].when == l[best].when && strcmp(l[i].file, l[best].file) > 0))
            best = i;
    }
    free(ap);
    if (best < 0) {
        free_recs(l, n);
        return 0;
    }
    {
        FILE *f = fopen(l[best].file, "r");
        char line[PATH_MAX + 64];
        long off;
        long nl, nb;
        int inc, ex;
        if (!f) {
            free_recs(l, n);
            return 0;
        }
        /* skip the three header lines */
        for (int k = 0; k < 3; k++)
            if (!fgets(line, sizeof line, f))
                break;
        off = ftell(f);
        fclose(f);
        {
            /* read the whole file and drop the header */
            buf_reset();
            if (read_file_into(l[best].file, -1, &nl, &nb, &inc, &ex) == 0 && nlines >= 3) {
                Str dummy;
                (void)dummy;
                (void)off;
                buf_delete(1, 3);
                undo_discard();
            }
        }
    }
    free(recover_path);
    recover_path = xstrdup(l[best].file);
    *older = count > 1;
    free_recs(l, n);
    modified = 1;
    return 1;
}

/* the buffer was written completely: recovery files are obsolete */
void recover_cleanup(void)
{
    if (preserve_path) {
        unlink(preserve_path);
        free(preserve_path);
        preserve_path = NULL;
    }
    if (recover_path) {
        unlink(recover_path);
        free(recover_path);
        recover_path = NULL;
    }
}

/* ---------- tags ---------- */

int tag_lookup(const char *tag, char **file, char **addr)
{
    const char *tl = OS(OP_TAGS) ? OS(OP_TAGS) : "tags";
    char *list = xstrdup(tl), *save = NULL, *tf;
    int tlen = (int)OV(OP_TAGLENGTH), err = 0;
    for (tf = strtok_r(list, " \t", &save); tf; tf = strtok_r(NULL, " \t", &save)) {
        FILE *f = fopen(tf, "r");
        char *line = NULL;
        size_t cap = 0;
        ssize_t n;
        if (!f) {
            if (!err && errno != ENOENT) {
                err = 1;
                msg("%s: %s", tf, strerror(errno));
            }
            continue;
        }
        while ((n = getline(&line, &cap, f)) > 0) {
            char *t1, *t2, *t3;
            size_t nl;
            if (line[n - 1] == '\n')
                line[--n] = 0;
            t1 = line;
            t2 = strchr(t1, '\t');
            if (!t2)
                continue;
            nl = (size_t)(t2 - t1);
            *t2++ = 0;
            t3 = strchr(t2, '\t');
            if (!t3)
                continue;
            *t3++ = 0;
            if (tlen > 0) {
                size_t a = strlen(tag), b = nl;
                if (a > (size_t)tlen)
                    a = (size_t)tlen;
                if (b > (size_t)tlen)
                    b = (size_t)tlen;
                if (a != b || strncmp(tag, t1, a) != 0)
                    continue;
            } else if (strcmp(tag, t1) != 0)
                continue;
            *file = xstrdup(t2);
            *addr = xstrdup(t3);
            free(line);
            fclose(f);
            free(list);
            return 1;
        }
        free(line);
        fclose(f);
    }
    free(list);
    return 0;
}

/* compile a tag search pattern: only ^, $ and \ are special */
static Regex *tag_pattern(const char *addr, int *dir)
{
    int d = (unsigned char)addr[0];
    const char *p = addr + 1, *e;
    char *err = NULL;
    Regex *re;
    Str s;
    *dir = d == '?' ? -1 : 1;
    str_init(&s);
    /* the pattern ends at the last delimiter */
    e = p + strlen(p);
    while (e > p && e[-1] != d)
        e--;
    if (e > p)
        e--;
    for (const char *q = p; q < e; q++) {
        if (*q == '\\' && q + 1 < e && (q[1] == d || q[1] == '\\')) {
            str_addc(&s, '\\');
            str_addc(&s, q[1]);
            q++;
            continue;
        }
        str_addc(&s, *q);
    }
    re = re_compile(s.s ? s.s : "", 0, NULL, 0, &err);
    str_free(&s);
    return re;
}

void tag_goto(const char *tagarg, int force)
{
    static const char *tag;
    char *file = NULL, *addr = NULL;
    tag = tagarg;
    while (*tag == ' ' || *tag == '\t')
        tag++;
    if (!*tag)
        ex_err("Tag name required");
    if (!tag_lookup(tag, &file, &addr))
        ex_err("%s: tag not found", tag);
    if (!curpath || !same_file(file, curpath)) {
        jmp_buf jb, *saved = errjmp;
        errjmp = &jb;
        if (setjmp(jb)) {
            errjmp = saved;
            free(file);
            free(addr);
            ex_err_silent();
        }
        if (!force && modified) {
            if (OV(OP_AUTOWRITE))
                autowrite_check(0);
            else
                ex_err("No write since last change (:tag! overrides)");
        }
        errjmp = saved;
        edit_ctx_tag = 1;
        edit_file(file, 1, NULL, 0);
        edit_ctx_tag = 0;
    }
    if (c_cmds_done == 2) {
        c_cmds_done = 1;
        run_c_cmds();
    }
    free(file);
    set_context_mark();
    if (isdigit((unsigned char)addr[0])) {
        int n = atoi(addr);
        free(addr);
        if (n < 1 || n > nlines)
            ex_err("Tag line %d is past the end of the file", n);
        cur = n;
        set_nonblank();
    } else {
        int dir, found = 0;
        Regex *re = tag_pattern(addr, &dir);
        free(addr);
        if (!re)
            ex_err("Invalid tag pattern");
        set_last_re(re);
        search_dir = 1;
        for (int l = 1; l <= nlines; l++)
            if (re_exec(re, LT(l), LL(l), 0, 0, NULL)) {
                cur = l;
                found = 1;
                break;
            }
        re_free(re);
        if (!found)
            ex_err("%s: tag pattern not found", tag);
        set_nonblank();
    }
    if (!vi_active && !in_global && !in_exec_buffer && OV(OP_AUTOPRINT) && !silent)
        print_lines(cur, cur, 0);
}

void c_tag(Cmd *c)
{
    const char *p = skipbl(c->arg);
    char *t = xstrdup(p);
    size_t l = strlen(t);
    while (l && (t[l - 1] == ' ' || t[l - 1] == '\t'))
        t[--l] = 0;
    {
        jmp_buf jb, *saved = errjmp;
        errjmp = &jb;
        if (setjmp(jb)) {
            errjmp = saved;
            free(t);
            ex_err_silent();
        }
        tag_goto(t, c->bang);
        errjmp = saved;
    }
    free(t);
    c->no_autoprint = 1;
}
