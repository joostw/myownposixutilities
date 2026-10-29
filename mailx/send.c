/*
 * send.c - composing messages (input mode and the command escapes),
 * aliases, recording, the dead-letter file and passing messages to the
 * mail delivery software.
 */
#include "mailx.h"

int opt_E, opt_F;
int send_mode;

void compose_free(Compose *c)
{
    words_free(&c->to);
    words_free(&c->cc);
    words_free(&c->bcc);
    words_free(&c->noalias);
    free(c->subject);
    free(c->record);
    buf_free(&c->body);
    buf_free(&c->extra);
    memset(c, 0, sizeof *c);
}

/* ---------- prompts ---------- */

static int prompt_line(const char *prompt, Buf *out)
{
    int r;
    fputs(prompt, stdout);
    fflush(stdout);
    out->len = 0;
    if (out->s)
        out->s[0] = 0;
    r = line_read(out);
    return r;
}

static void add_names(Words *w, const char *s)
{
    split_addresses(s, w);
}

static void join_words(Buf *b, Words *w)
{
    for (int i = 0; i < w->n; i++) {
        if (i)
            buf_adds(b, ", ");
        buf_adds(b, w->v[i]);
    }
}

/* ~h: edit the header fields */
static void edit_headers(Compose *c)
{
    static const char *const names[] = { "To: ", "Subject: ", "Cc: ", "Bcc: " };
    for (int k = 0; k < 4; k++) {
        Buf cv = { 0 }, in = { 0 };
        int stuffed = 0;
        if (k == 0)
            join_words(&cv, &c->to);
        else if (k == 1)
            buf_adds(&cv, c->subject ? c->subject : "");
        else if (k == 2)
            join_words(&cv, &c->cc);
        else
            join_words(&cv, &c->bcc);
        fputs(names[k], stdout);
        fflush(stdout);
        /* the current value is typed in for editing, where the terminal allows it */
        if (cv.s) {
            stuffed = 1;
            for (char *p = cv.s; *p; p++)
                if (ioctl(0, TIOCSTI, p) < 0) {
                    stuffed = 0;
                    break;
                }
            if (!stuffed && cv.len) {
                printf("[%s] ", cv.s);
                fflush(stdout);
            }
        }
        if (line_read(&in) < 0) {
            buf_free(&cv);
            buf_free(&in);
            putchar('\n');
            return;
        }
        if (!stuffed && (!in.s || !*in.s)) {
            /* empty reply: keep the value */
            buf_free(&in);
            buf_free(&cv);
            continue;
        }
        if (k == 1) {
            free(c->subject);
            c->subject = xstrdup(in.s ? in.s : "");
        } else {
            Words *w = k == 0 ? &c->to : k == 2 ? &c->cc : &c->bcc;
            words_free(w);
            add_names(w, in.s ? in.s : "");
        }
        buf_free(&cv);
        buf_free(&in);
    }
}

/* ---------- the message so far ---------- */

static void print_draft(Compose *c)
{
    Buf h = { 0 };
    printf("-------\nMessage contains:\n");
    if (c->to.n) {
        join_words(&h, &c->to);
        printf("To: %s\n", h.s);
        buf_free(&h);
    }
    if (c->subject && *c->subject)
        printf("Subject: %s\n", c->subject);
    if (c->cc.n) {
        join_words(&h, &c->cc);
        printf("Cc: %s\n", h.s);
        buf_free(&h);
    }
    if (c->bcc.n) {
        join_words(&h, &c->bcc);
        printf("Bcc: %s\n", h.s);
        buf_free(&h);
    }
    putchar('\n');
    {
        int lines = 0;
        pid_t pid;
        FILE *fp;
        for (size_t i = 0; i < c->body.len; i++)
            if (c->body.s[i] == '\n')
                lines++;
        fp = open_pager(lines, &pid);
        if (c->body.len)
            fwrite(c->body.s, 1, c->body.len, fp);
        close_pager(fp, pid);
    }
}

static void insert_file(Compose *c, const char *name)
{
    char *path = expand_filename(name);
    FILE *fp;
    int ch, lines = 0;
    long chars = 0;
    if (!path)
        return;
    fp = fopen(path, "r");
    if (!fp) {
        err("%s: %s", path, strerror(errno));
        free(path);
        return;
    }
    while ((ch = getc(fp)) != EOF) {
        buf_addc(&c->body, ch);
        chars++;
        if (ch == '\n')
            lines++;
    }
    if (chars && c->body.s[c->body.len - 1] != '\n') {
        buf_addc(&c->body, '\n');
        lines++;
    }
    fclose(fp);
    printf("\"%s\" %d/%ld\n", path, lines, chars);
    free(path);
}

static void insert_command(Compose *c, const char *cmd)
{
    char *t;
    int fd, st;
    t = make_temp(&fd);
    if (!t)
        return;
    unlink(t);
    st = run_shell_cmd(cmd, -1, fd);
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0))
        err("\"%s\" exited with status %d", cmd, WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st));
    lseek(fd, 0, SEEK_SET);
    {
        char b[4096];
        ssize_t n;
        size_t before = c->body.len;
        while ((n = read(fd, b, sizeof b)) > 0)
            buf_add(&c->body, b, (size_t)n);
        if (c->body.len > before && c->body.s[c->body.len - 1] != '\n')
            buf_addc(&c->body, '\n');
    }
    close(fd);
    free(t);
}

/* ~e, ~v */
static void edit_body(Compose *c, const char *editor)
{
    int fd;
    char *t = make_temp(&fd);
    FILE *fp;
    if (!t)
        return;
    if (c->body.len && write(fd, c->body.s, c->body.len) < 0)
        err("%s: %s", t, strerror(errno));
    close(fd);
    run_interactive(editor, t);
    fp = fopen(t, "r");
    if (fp) {
        int ch;
        c->body.len = 0;
        if (c->body.s)
            c->body.s[0] = 0;
        while ((ch = getc(fp)) != EOF)
            buf_addc(&c->body, ch);
        if (c->body.len && c->body.s[c->body.len - 1] != '\n')
            buf_addc(&c->body, '\n');
        fclose(fp);
    }
    unlink(t);
    free(t);
}

/* ~| */
static void pipe_body(Compose *c, const char *cmd)
{
    int infd, outfd, st;
    char *ti = make_temp(&infd), *to;
    if (!ti)
        return;
    to = make_temp(&outfd);
    if (!to) {
        close(infd);
        unlink(ti);
        free(ti);
        return;
    }
    unlink(ti);
    unlink(to);
    if (c->body.len && write(infd, c->body.s, c->body.len) < 0)
        err("%s", strerror(errno));
    lseek(infd, 0, SEEK_SET);
    st = run_shell_cmd(cmd, infd, outfd);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
        char b[4096];
        ssize_t n;
        c->body.len = 0;
        if (c->body.s)
            c->body.s[0] = 0;
        lseek(outfd, 0, SEEK_SET);
        while ((n = read(outfd, b, sizeof b)) > 0)
            buf_add(&c->body, b, (size_t)n);
        if (c->body.len && c->body.s[c->body.len - 1] != '\n')
            buf_addc(&c->body, '\n');
    } else
        err("\"%s\" failed with exit status %d; message unchanged", cmd,
            WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st));
    close(infd);
    close(outfd);
    free(ti);
    free(to);
}

/* insert a variable with \t and \n recognised */
static void insert_var(Compose *c, const char *name)
{
    const char *v = var_get(name);
    if (!v || !*v)
        return;
    for (const char *p = v; *p; p++) {
        if (*p == '\\' && p[1] == 't') {
            buf_addc(&c->body, '\t');
            p++;
        } else if (*p == '\\' && p[1] == 'n') {
            buf_addc(&c->body, '\n');
            p++;
        } else
            buf_addc(&c->body, *p);
    }
    buf_addc(&c->body, '\n');
}

/* ~f ~F ~m ~M */
static void insert_messages(Compose *c, const char *arg, int mode, int indent)
{
    Words w = { 0 };
    int *sel;
    if (!mb.path || send_mode) {
        err("No messages to insert from");
        return;
    }
    if (split_args(arg, &w) < 0) {
        err("unbalanced quotes");
        return;
    }
    sel = xcalloc((size_t)mb.n + 1, sizeof(int));
    if (parse_msglist(&w, 0, w.n, sel, 0, 1) > 0) {
        char *s = NULL;
        size_t sz = 0;
        FILE *mem = open_memstream(&s, &sz);
        const char *prefix = NULL;
        if (indent) {
            prefix = var_get("indentprefix");
            if (!prefix)
                prefix = "\t";
        }
        for (int i = 1; i <= mb.n; i++)
            if (sel[i]) {
                printf("Interpolating: %d\n", i);
                msg_write_lines(&mb.m[i - 1], mem, mode, prefix, -1);
                mark_read(i);
            }
        fclose(mem);
        buf_add(&c->body, s, sz);
        free(s);
    }
    free(sel);
    words_free(&w);
}

static const char tilde_help[] =
    "-------------------- ~ Escapes --------------------\n"
    "~!command      run a shell command\n"
    "~.             end of input\n"
    "~: command     run a mailx command (also ~_)\n"
    "~?             this summary\n"
    "~A, ~a         insert the Sign or sign variable\n"
    "~b names       add names to the Bcc list\n"
    "~c names       add names to the Cc list\n"
    "~d             read in the dead-letter file\n"
    "~e, ~v         edit the message with EDITOR or VISUAL\n"
    "~f, ~F msgs    insert messages (~F: all header fields)\n"
    "~h             edit the header fields\n"
    "~i variable    insert the value of a variable\n"
    "~m, ~M msgs    insert messages, indented (~M: all header fields)\n"
    "~p             write the message so far\n"
    "~q             abort, saving the message in the dead-letter file\n"
    "~r file        read in a file (~r !command: its output; also ~<)\n"
    "~s subject     set the subject\n"
    "~t names       add names to the To list\n"
    "~w file        write the message to a file\n"
    "~x             abort without saving\n"
    "~| command     pipe the message through a command\n"
    "---------------------------------------------------\n";

/* returns 0 to continue, 1 end of input, 2 abort with save, 3 abort */
static int escape_cmd(Compose *c, const char *line, int interactive)
{
    int ch = (unsigned char)line[1];
    const char *arg = line + 2;
    while (*arg == ' ' || *arg == '\t')
        arg++;
    switch (ch) {
    case '!': {
        char *cmd = bang_expand(line + 2);
        if (cmd) {
            run_shell_cmd(cmd, -1, -1);
            free(cmd);
            if (interactive)
                printf("!\n");
        }
        break;
    }
    case '.':
        return 1;
    case ':':
    case '_':
        execute(arg, 0);
        break;
    case '?':
        fputs(tilde_help, stdout);
        break;
    case 'A':
        insert_var(c, "Sign");
        break;
    case 'a':
        insert_var(c, "sign");
        break;
    case 'b':
        add_names(&c->bcc, arg);
        break;
    case 'c':
        add_names(&c->cc, arg);
        break;
    case 't':
        add_names(&c->to, arg);
        break;
    case 'd': {
        char *d = dead_path();
        insert_file(c, d);
        free(d);
        break;
    }
    case 'e': {
        const char *e = var_get("EDITOR");
        edit_body(c, e && *e ? e : "ed");
        if (interactive)
            printf("(continue)\n");
        break;
    }
    case 'v': {
        const char *e = var_get("VISUAL");
        edit_body(c, e && *e ? e : "vi");
        if (interactive)
            printf("(continue)\n");
        break;
    }
    case 'f':
        insert_messages(c, arg, W_FORWARD, 0);
        break;
    case 'F':
        insert_messages(c, arg, W_FORWARD_ALL, 0);
        break;
    case 'm':
        insert_messages(c, arg, W_FORWARD, 1);
        break;
    case 'M':
        insert_messages(c, arg, W_FORWARD_ALL, 1);
        break;
    case 'h':
        if (is_tty_in())
            edit_headers(c);
        break;
    case 'i':
        insert_var(c, arg);
        break;
    case 'p':
        print_draft(c);
        if (interactive)
            printf("(continue)\n");
        break;
    case 'q':
        return 2;
    case 'x':
        return 3;
    case 'r':
    case '<':
        if (!*arg)
            err("~%c: file name required", ch);
        else if (*arg == '!')
            insert_command(c, arg + 1);
        else
            insert_file(c, arg);
        break;
    case 's':
        free(c->subject);
        c->subject = xstrdup(arg);
        break;
    case 'w': {
        char *path;
        FILE *fp;
        if (!*arg) {
            err("~w: file name required");
            break;
        }
        path = expand_filename(arg);
        if (!path)
            break;
        fp = fopen(path, "a");
        if (!fp)
            err("%s: %s", path, strerror(errno));
        else {
            int lines = 0;
            for (size_t i = 0; i < c->body.len; i++)
                if (c->body.s[i] == '\n')
                    lines++;
            if (c->body.len)
                fwrite(c->body.s, 1, c->body.len, fp);
            fclose(fp);
            printf("\"%s\" %d/%zu\n", path, lines, c->body.len);
        }
        free(path);
        break;
    }
    case '|':
        if (!*arg)
            err("~|: command required");
        else
            pipe_body(c, arg);
        if (interactive)
            printf("(continue)\n");
        break;
    default:
        err("Unknown tilde escape: ~%c", ch ? ch : ' ');
        break;
    }
    return 0;
}

/* ---------- input mode ---------- */

int collect(Compose *c, int receive_mode)
{
    int interactive = is_tty_in();
    int intr_pending = 0;
    Buf line = { 0 };
    (void)receive_mode;
    if (interactive && !c->subject && var_isset("asksub")) {
        int r = prompt_line("Subject: ", &line);
        if (r < 0) {
            buf_free(&line);
            putchar('\n');
            return 1;
        }
        c->subject = xstrdup(line.s ? line.s : "");
    }
    for (;;) {
        int r, esc;
        line.len = 0;
        if (line.s)
            line.s[0] = 0;
        r = line_read(&line);
        if (r < 0) {
            /* SIGINT */
            if (var_isset("ignore")) {
                printf("@\n");
                fflush(stdout);
                continue;
            }
            if (intr_pending) {
                putchar('\n');
                if (c->body.len)
                    save_dead(c);
                buf_free(&line);
                return 1;
            }
            printf("\n(Interrupt -- one more to kill letter)\n");
            fflush(stdout);
            intr_pending = 1;
            continue;
        }
        intr_pending = 0;
        if (r == 0) {
            if (interactive && var_isset("ignoreeof")) {
                printf("Use \".\" to terminate letter\n");
                clearerr(stdin);
                continue;
            }
            break;
        }
        if (interactive && strcmp(line.s, ".") == 0 && (var_isset("dot") || var_isset("ignoreeof")))
            break;
        esc = escape_char();
        if (esc >= 0 && (unsigned char)line.s[0] == esc && line.s[1]) {
            if ((unsigned char)line.s[1] == esc) {
                buf_adds(&c->body, line.s + 1);
                buf_addc(&c->body, '\n');
                continue;
            }
            r = escape_cmd(c, line.s, interactive);
            if (r == 1)
                break;
            if (r == 2) {
                if (c->body.len)
                    save_dead(c);
                buf_free(&line);
                return 1;
            }
            if (r == 3) {
                buf_free(&line);
                return 1;
            }
            continue;
        }
        buf_add(&c->body, line.s, line.len);
        buf_addc(&c->body, '\n');
    }
    buf_free(&line);
    if (interactive && var_isset("askcc")) {
        Buf in = { 0 };
        if (prompt_line("Cc: ", &in) > 0)
            add_names(&c->cc, in.s);
        buf_free(&in);
    }
    if (interactive && var_isset("askbcc")) {
        Buf in = { 0 };
        if (prompt_line("Bcc: ", &in) > 0)
            add_names(&c->bcc, in.s);
        buf_free(&in);
    }
    if (interactive)
        printf("EOT\n");
    return 0;
}

/* ---------- aliases ---------- */

static void expand_one(const char *name, Words *out, Words *seen, int depth, int in_group)
{
    Words *a;
    if (name[0] == '\\') {
        if (!words_has(out, name + 1, 0))
            words_add(out, name + 1);
        return;
    }
    a = depth < 64 ? alias_get(name) : NULL;
    if (a && !words_has(seen, name, 0)) {
        words_add(seen, name);
        for (int i = 0; i < a->n; i++)
            expand_one(a->v[i], out, seen, depth + 1, 1);
        return;
    }
    if (in_group && !var_isset("metoo") && addr_is_me(name))
        return;
    if (!words_has(out, name, 0))
        words_add(out, name);
}

void expand_aliases(Words *in, Words *out)
{
    for (int i = 0; i < in->n; i++) {
        Words seen = { 0 };
        expand_one(in->v[i], out, &seen, 0, 0);
        words_free(&seen);
    }
}

static void expand_list(Words *w, Words *noalias)
{
    Words out = { 0 };
    for (int i = 0; i < w->n; i++) {
        if (words_has(noalias, w->v[i], 0)) {
            if (!words_has(&out, w->v[i], 0))
                words_add(&out, w->v[i]);
            continue;
        }
        {
            Words one = { 0 }, res = { 0 };
            words_add(&one, w->v[i]);
            expand_aliases(&one, &res);
            for (int k = 0; k < res.n; k++)
                if (!words_has(&out, res.v[k], 0))
                    words_add(&out, res.v[k]);
            words_free(&one);
            words_free(&res);
        }
    }
    words_free(w);
    *w = out;
}

/* ---------- writing the message ---------- */

static int has_8bit(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] >= 0x80)
            return 1;
    return 0;
}

static void header_text(Compose *c, Buf *h, int with_bcc)
{
    Buf v = { 0 };
    char date[64];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (c->to.n) {
        join_words(&v, &c->to);
        buf_fmt(h, "To: %s\n", v.s);
        buf_free(&v);
    }
    if (c->cc.n) {
        join_words(&v, &c->cc);
        buf_fmt(h, "Cc: %s\n", v.s);
        buf_free(&v);
    }
    if (with_bcc && c->bcc.n) {
        join_words(&v, &c->bcc);
        buf_fmt(h, "Bcc: %s\n", v.s);
        buf_free(&v);
    }
    if (c->subject && *c->subject)
        buf_fmt(h, "Subject: %s\n", c->subject);
    {
        /* RFC 5322 date, in the C locale */
        static const char *const wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        static const char *const mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        long off = tm->tm_gmtoff / 60;
        snprintf(date, sizeof date, "%s, %d %s %d %02d:%02d:%02d %c%02ld%02ld", wd[tm->tm_wday], tm->tm_mday,
                 mo[tm->tm_mon], tm->tm_year + 1900, tm->tm_hour, tm->tm_min, tm->tm_sec, off < 0 ? '-' : '+',
                 labs(off) / 60, labs(off) % 60);
        buf_fmt(h, "Date: %s\n", date);
    }
    if (c->extra.len)
        buf_add(h, c->extra.s, c->extra.len);
    if (has_8bit(c->body.s ? c->body.s : "", c->body.len) ||
        (c->subject && has_8bit(c->subject, strlen(c->subject)))) {
        const char *cs = nl_langinfo(CODESET);
        if (!cs || !*cs || strcmp(cs, "ANSI_X3.4-1968") == 0)
            cs = "UTF-8";
        buf_fmt(h, "MIME-Version: 1.0\nContent-Type: text/plain; charset=%s\nContent-Transfer-Encoding: 8bit\n", cs);
    }
}

/* the message in mbox format: From line, header, body with From escaped */
static void mbox_text(Compose *c, Buf *out)
{
    time_t now = time(NULL);
    const char *p, *e;
    int prev_empty = 1;
    buf_fmt(out, "From %s %s", login_name(), ctime(&now));
    header_text(c, out, 1);
    buf_addc(out, '\n');
    p = c->body.s ? c->body.s : "";
    e = p + c->body.len;
    while (p < e) {
        const char *nl = memchr(p, '\n', (size_t)(e - p));
        size_t l = nl ? (size_t)(nl - p + 1) : (size_t)(e - p);
        if (l >= 5 && memcmp(p, "From ", 5) == 0)
            buf_addc(out, '>');
        (void)prev_empty;
        buf_add(out, p, l);
        p += l;
    }
    if (c->body.len && c->body.s[c->body.len - 1] != '\n')
        buf_addc(out, '\n');
    buf_addc(out, '\n');
}

void save_dead(Compose *c)
{
    char *path;
    Buf t = { 0 };
    int fd;
    if (!var_isset("save"))
        return;
    path = dead_path();
    if (strcmp(path, "/dev/null") == 0) {
        free(path);
        return;
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        free(path);
        return;
    }
    mbox_text(c, &t);
    if (write(fd, t.s, t.len) < 0)
        err("%s: %s", path, strerror(errno));
    close(fd);
    {
        int lines = 0;
        for (size_t i = 0; i < t.len; i++)
            if (t.s[i] == '\n')
                lines++;
        printf("\"%s\" %d/%zu\n", path, lines, t.len);
    }
    buf_free(&t);
    free(path);
}

char *record_name_for(const char *addr)
{
    char *l = addr_login(addr);
    Buf b = { 0 };
    char *fd = folder_dir();
    if (var_isset("outfolder") && fd) {
        buf_adds(&b, fd);
        buf_addc(&b, '/');
    }
    buf_adds(&b, l);
    free(l);
    free(fd);
    return buf_take(&b);
}

static void record_message(Compose *c, const char *file)
{
    char *path = expand_filename(file);
    Buf t = { 0 };
    int fd;
    if (!path)
        return;
    if (path[0] != '/' && var_isset("outfolder")) {
        char *fd2 = folder_dir();
        if (fd2) {
            Buf p = { 0 };
            buf_fmt(&p, "%s/%s", fd2, path);
            free(path);
            path = buf_take(&p);
            free(fd2);
        }
    }
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        err("%s: %s", path, strerror(errno));
        free(path);
        return;
    }
    mbox_text(c, &t);
    if (write(fd, t.s, t.len) < 0)
        err("%s: %s", path, strerror(errno));
    close(fd);
    buf_free(&t);
    free(path);
}

static const char *mailer(void)
{
    const char *s = var_get("sendmail");
    if (s && *s)
        return s;
    if (access("/usr/sbin/sendmail", X_OK) == 0)
        return "/usr/sbin/sendmail";
    return "/usr/lib/sendmail";
}

int send_message(Compose *c)
{
    Words rcpt = { 0 };
    Buf msg = { 0 };
    int fd, r = 0;
    char *t;
    expand_list(&c->to, &c->noalias);
    expand_list(&c->cc, &c->noalias);
    expand_list(&c->bcc, &c->noalias);
    for (int k = 0; k < 3; k++) {
        Words *w = k == 0 ? &c->to : k == 1 ? &c->cc : &c->bcc;
        for (int i = 0; i < w->n; i++) {
            char *a = addr_spec(w->v[i]);
            if (*a && !words_has(&rcpt, a, 0))
                words_add(&rcpt, a);
            free(a);
        }
    }
    if (c->body.len == 0) {
        if (opt_E) {
            words_free(&rcpt);
            return 0;
        }
        if (is_tty_in())
            printf("Null message body; hope that's ok\n");
    }
    if (!rcpt.n) {
        err("No recipients specified");
        save_dead(c);
        words_free(&rcpt);
        return -1;
    }
    /* record the message */
    if (opt_F) {
        char *f = record_name_for(c->to.n ? c->to.v[0] : rcpt.v[0]);
        record_message(c, f);
        free(f);
    } else if (c->record) {
        record_message(c, c->record);
    } else {
        const char *rec = var_get("record");
        if (rec && *rec)
            record_message(c, rec);
    }
    header_text(c, &msg, 0);
    buf_addc(&msg, '\n');
    if (c->body.len)
        buf_add(&msg, c->body.s, c->body.len);
    if (var_isset("debug")) {
        printf("Recipients of the message:");
        for (int i = 0; i < rcpt.n; i++)
            printf(" %s", rcpt.v[i]);
        printf("\n%s", msg.s);
        words_free(&rcpt);
        buf_free(&msg);
        return 0;
    }
    t = make_temp(&fd);
    if (!t) {
        save_dead(c);
        words_free(&rcpt);
        buf_free(&msg);
        return -1;
    }
    unlink(t);
    free(t);
    if (write(fd, msg.s, msg.len) < 0) {
        err("temporary file: %s", strerror(errno));
        close(fd);
        save_dead(c);
        words_free(&rcpt);
        buf_free(&msg);
        return -1;
    }
    lseek(fd, 0, SEEK_SET);
    {
        const char *mx = mailer();
        char **argv = xcalloc((size_t)rcpt.n + 4, sizeof(char *));
        int wait_for_it = var_isset("sendwait");
        pid_t pid;
        int status = 0;
        argv[0] = (char *)mx;
        argv[1] = "-oi";
        argv[2] = "--";
        for (int i = 0; i < rcpt.n; i++)
            argv[3 + i] = rcpt.v[i];
        if (!wait_for_it && access(mx, X_OK) != 0 && strchr(mx, '/')) {
            err("%s: %s", mx, strerror(errno));
            r = -1;
        } else {
            fflush(stdout);
            pid = fork();
            if (pid == 0) {
                if (!wait_for_it) {
                    /* a background mailer: the parent does not wait for it */
                    pid_t p2 = fork();
                    if (p2 != 0)
                        _exit(p2 < 0 ? 1 : 0);
                    setsid();
                }
                signal(SIGINT, wait_for_it ? SIG_DFL : SIG_IGN);
                signal(SIGPIPE, SIG_DFL);
                dup2(fd, 0);
                close(fd);
                execvp(mx, argv);
                fprintf(stderr, "mailx: %s: %s\n", mx, strerror(errno));
                _exit(127);
            }
            if (pid < 0) {
                err("fork: %s", strerror(errno));
                r = -1;
            } else {
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                    ;
                if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
                    if (wait_for_it)
                        err("%s: exited with status %d", mx,
                            WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
                    r = -1;
                }
            }
        }
        free(argv);
    }
    close(fd);
    if (r < 0)
        save_dead(c);
    words_free(&rcpt);
    buf_free(&msg);
    return r;
}

int compose_and_send(Compose *c, int receive_mode)
{
    if (collect(c, receive_mode) != 0)
        return 1;
    return send_message(c) == 0 ? 0 : -1;
}
