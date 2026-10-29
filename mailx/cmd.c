/*
 * cmd.c - command mode: reading lines, interrupts, the command table
 * with its abbreviations, and the commands themselves.
 */
#include "mailx.h"

int in_startup;
sigjmp_buf cmd_jmp;
volatile sig_atomic_t cmd_jmp_ok;
volatile sig_atomic_t got_sigint;
static char *prev_bang;
static int page_start = 1;

/* conditional execution: a stack of states */
static int cond_active[64], cond_depth, cond_seen_else[64];

/* ---------- reading lines ---------- */

static char ibuf[8192];
static size_t ipos, ilen;

int line_read(Buf *b)
{
    for (;;) {
        char *nl = ipos < ilen ? memchr(ibuf + ipos, '\n', ilen - ipos) : NULL;
        ssize_t n;
        if (nl) {
            buf_add(b, ibuf + ipos, (size_t)(nl - ibuf - (ptrdiff_t)ipos));
            ipos = (size_t)(nl - ibuf) + 1;
            if (!b->s)
                buf_adds(b, "");
            return 1;
        }
        if (ipos < ilen)
            buf_add(b, ibuf + ipos, ilen - ipos);
        ipos = ilen = 0;
        n = read(0, ibuf, sizeof ibuf);
        if (n < 0) {
            if (errno == EINTR) {
                if (got_sigint) {
                    got_sigint = 0;
                    b->len = 0;
                    if (b->s)
                        b->s[0] = 0;
                    return -1;
                }
                continue;
            }
            return b->len ? 1 : 0;
        }
        if (n == 0)
            return b->len ? 1 : 0;
        ilen = (size_t)n;
    }
}

/* a command line, with backslash-newline continuation */
static int command_line_read(Buf *b)
{
    int r = line_read(b);
    while (r > 0 && b->len && b->s[b->len - 1] == '\\') {
        size_t k = 0;
        while (k < b->len && b->s[b->len - 1 - k] == '\\')
            k++;
        if (k % 2 == 0)
            break;
        b->s[--b->len] = 0;
        r = line_read(b);
        if (r == 0)
            r = 1;
    }
    return r;
}

/* ---------- helpers ---------- */

static void set_cur(int i)
{
    if (i != cur) {
        cur = i;
        cur_shown = 0;
    }
}

static int *new_sel(void)
{
    return xcalloc((size_t)mb.n + 2, sizeof(int));
}

static int need_mailbox(void)
{
    if (!mb.path) {
        err("No mailbox");
        return 0;
    }
    return 1;
}

static int screen_size(void)
{
    int s = var_num("screen", 0);
    if (s > 0)
        return s;
    if (is_tty_out())
        return screen_rows() > 5 ? screen_rows() - 3 : 2;
    return 20;
}

void headers_page(int msg)
{
    int size = screen_size(), idx = 0, k = 0, *und = xmalloc(((size_t)mb.n + 1) * sizeof(int)), nu = 0;
    for (int i = 1; i <= mb.n; i++)
        if (mb.m[i - 1].state != M_DELETED)
            und[nu++] = i;
    if (!nu) {
        printf("No more mail.\n");
        free(und);
        return;
    }
    for (int i = 0; i < nu; i++)
        if (und[i] <= msg)
            idx = i;
    idx = (idx / size) * size;
    page_start = und[idx];
    for (int i = idx; i < nu && k < size; i++, k++)
        summary_line(stdout, und[i]);
    free(und);
}

static void print_messages(int *sel, int mode)
{
    int lines = 0;
    pid_t pid;
    FILE *fp;
    for (int i = 1; i <= mb.n; i++)
        if (sel[i])
            lines += mb.m[i - 1].lines + 1;
    fp = open_pager(lines, &pid);
    for (int i = 1; i <= mb.n; i++)
        if (sel[i]) {
            fprintf(fp, "Message %d:\n", i);
            msg_write(&mb.m[i - 1], fp, mode);
            mark_read(i);
            set_cur(i);
            cur_shown = 1;
        }
    close_pager(fp, pid);
}

static void print_one(int i)
{
    int *sel = new_sel();
    sel[i] = 1;
    print_messages(sel, W_SHOW);
    free(sel);
}

/* the arguments split into msglist words and a final file/command */
static int msgs_and_last(Words *w, int *sel, char **last, int dflt_to_current)
{
    *last = NULL;
    if (w->n == 0)
        return parse_msglist(w, 0, 0, sel, 0, dflt_to_current);
    *last = w->v[w->n - 1];
    return parse_msglist(w, 0, w->n - 1, sel, 0, 1);
}

char *bang_expand(const char *cmd)
{
    Buf b = { 0 };
    while (*cmd == ' ' || *cmd == '\t')
        cmd++;
    if (!var_isset("bang"))
        buf_adds(&b, cmd);
    else
        for (const char *p = cmd; *p; p++) {
            if (*p == '\\' && p[1] == '!') {
                buf_addc(&b, '!');
                p++;
            } else if (*p == '!') {
                if (!prev_bang) {
                    err("No previous command");
                    buf_free(&b);
                    return NULL;
                }
                buf_adds(&b, prev_bang);
            } else
                buf_addc(&b, *p);
        }
    free(prev_bang);
    prev_bang = xstrdup(b.s ? b.s : "");
    return buf_take(&b);
}

static char *author_of(int i)
{
    char *s = msg_sender(&mb.m[i - 1]);
    return s;
}

/* ---------- composing ---------- */

static int do_compose(Compose *c)
{
    int saved = cmd_jmp_ok, r;
    cmd_jmp_ok = 0;
    r = compose_and_send(c, 1);
    cmd_jmp_ok = saved;
    if (r < 0)
        exit_status = 1;
    return 0;
}

static void add_unique(Words *w, const char *a)
{
    char *s = addr_spec(a);
    for (int i = 0; i < w->n; i++) {
        char *t = addr_spec(w->v[i]);
        int same = strcasecmp(s, t) == 0;
        free(t);
        if (same) {
            free(s);
            return;
        }
    }
    free(s);
    words_add(w, a);
}

static void add_header_addrs(Words *w, Msg *m, const char *field)
{
    char *v = msg_header(m, field);
    Words a = { 0 };
    if (!v)
        return;
    split_addresses(v, &a);
    for (int i = 0; i < a.n; i++)
        if (var_isset("metoo") || !addr_is_me(a.v[i]))
            add_unique(w, a.v[i]);
    words_free(&a);
    free(v);
}

static void reply_subject(Compose *c, Msg *m)
{
    char *s = msg_header(m, "Subject");
    if (!s)
        return;
    if (strncasecmp(s, "Re: ", 4) == 0)
        c->subject = s;
    else {
        Buf b = { 0 };
        buf_fmt(&b, "Re: %s", s);
        c->subject = buf_take(&b);
        free(s);
    }
}

static void reply_refs(Compose *c, Msg *m)
{
    char *id = msg_header(m, "Message-ID"), *refs = msg_header(m, "References");
    if (id) {
        buf_fmt(&c->extra, "In-Reply-To: %s\n", id);
        buf_fmt(&c->extra, "References: %s%s%s\n", refs ? refs : "", refs ? " " : "", id);
    }
    free(id);
    free(refs);
}

/* lowercase reply: to the sender and all recipients */
static int reply_all(int i, char *record)
{
    Compose c;
    Msg *m = &mb.m[i - 1];
    char *rt = msg_header(m, "Reply-To"), *from = msg_header(m, "From");
    memset(&c, 0, sizeof c);
    if (rt)
        add_header_addrs(&c.to, m, "Reply-To");
    else if (from)
        add_header_addrs(&c.to, m, "From");
    else {
        char *s = msg_sender(m);
        if (*s && (var_isset("metoo") || !addr_is_me(s)))
            add_unique(&c.to, s);
        free(s);
    }
    add_header_addrs(&c.to, m, "To");
    add_header_addrs(&c.cc, m, "Cc");
    {
        /* Cc: without duplicates and without those already in To */
        Words cc = { 0 };
        for (int k = 0; k < c.cc.n; k++) {
            int dup = 0;
            char *a = addr_spec(c.cc.v[k]);
            for (int j = 0; j < c.to.n && !dup; j++) {
                char *b = addr_spec(c.to.v[j]);
                dup = strcasecmp(a, b) == 0;
                free(b);
            }
            free(a);
            if (!dup)
                add_unique(&cc, c.cc.v[k]);
        }
        words_free(&c.cc);
        c.cc = cc;
    }
    for (int k = 0; k < c.to.n; k++)
        words_add(&c.noalias, c.to.v[k]);
    for (int k = 0; k < c.cc.n; k++)
        words_add(&c.noalias, c.cc.v[k]);
    reply_subject(&c, m);
    reply_refs(&c, m);
    c.record = record;
    if (!c.to.n && !c.cc.n) {
        char *s = msg_sender(m);
        add_unique(&c.to, s);
        words_add(&c.noalias, s);
        free(s);
    }
    free(rt);
    free(from);
    printf("To: ");
    for (int k = 0; k < c.to.n; k++)
        printf("%s%s", k ? ", " : "", c.to.v[k]);
    printf("\n");
    if (c.cc.n) {
        printf("Cc: ");
        for (int k = 0; k < c.cc.n; k++)
            printf("%s%s", k ? ", " : "", c.cc.v[k]);
        printf("\n");
    }
    if (c.subject)
        printf("Subject: %s\n", c.subject);
    fflush(stdout);
    do_compose(&c);
    compose_free(&c);
    return 0;
}

/* capitalized Reply: to the authors of the messages */
static int reply_authors(int *sel, char *record)
{
    Compose c;
    int first = first_selected(sel);
    memset(&c, 0, sizeof c);
    for (int i = 1; i <= mb.n; i++) {
        Msg *m;
        char *rt;
        if (!sel[i])
            continue;
        m = &mb.m[i - 1];
        rt = msg_header(m, "Reply-To");
        if (rt)
            add_header_addrs(&c.to, m, "Reply-To");
        else {
            char *f = msg_header(m, "From");
            if (f)
                add_header_addrs(&c.to, m, "From");
            else {
                char *s = msg_sender(m);
                if (var_isset("metoo") || !addr_is_me(s))
                    add_unique(&c.to, s);
                free(s);
            }
            free(f);
        }
        free(rt);
    }
    for (int k = 0; k < c.to.n; k++)
        words_add(&c.noalias, c.to.v[k]);
    reply_subject(&c, &mb.m[first - 1]);
    reply_refs(&c, &mb.m[first - 1]);
    c.record = record;
    printf("To: ");
    for (int k = 0; k < c.to.n; k++)
        printf("%s%s", k ? ", " : "", c.to.v[k]);
    printf("\n");
    if (c.subject)
        printf("Subject: %s\n", c.subject);
    fflush(stdout);
    do_compose(&c);
    compose_free(&c);
    return 0;
}

/* ---------- the commands ---------- */

typedef int (*Fn)(const char *raw, Words *w);

static int c_alias(const char *raw, Words *w)
{
    (void)raw;
    if (w->n == 0)
        alias_list(stdout, NULL);
    else if (w->n == 1)
        alias_list(stdout, w->v[0]);
    else {
        Words a = { 0 };
        for (int i = 1; i < w->n; i++)
            split_addresses(w->v[i], &a);
        alias_add(w->v[0], &a);
        words_free(&a);
    }
    return 0;
}

static int c_alternates(const char *raw, Words *w)
{
    (void)raw;
    if (w->n == 0) {
        for (int i = 0; i < alternates.n; i++)
            printf("%s%s", i ? " " : "", alternates.v[i]);
        if (alternates.n)
            putchar('\n');
        return 0;
    }
    words_free(&alternates);
    for (int i = 0; i < w->n; i++)
        words_add(&alternates, w->v[i]);
    return 0;
}

static int c_cd(const char *raw, Words *w)
{
    char *d;
    (void)raw;
    d = w->n ? expand_filename(w->v[0]) : xstrdup(home_dir());
    if (!d)
        return 1;
    if (chdir(d) < 0) {
        err("%s: %s", d, strerror(errno));
        free(d);
        return 1;
    }
    free(d);
    return 0;
}

static int save_common(Words *w, int mode, int mark_saved, int by_author)
{
    int *sel = new_sel(), newfile, r = 0;
    char *last = NULL, *path;
    if (!need_mailbox()) {
        free(sel);
        return 1;
    }
    if (by_author) {
        if (parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
            free(sel);
            return 1;
        }
        {
            char *a = author_of(first_selected(sel));
            last = record_name_for(a);
            free(a);
        }
        path = expand_filename(last);
        free(last);
    } else {
        if (msgs_and_last(w, sel, &last, 1) < 0) {
            free(sel);
            return 1;
        }
        if (!last) {
            if (mode == W_BODY) {
                err("No file specified");
                free(sel);
                return 1;
            }
            path = mbox_path();
        } else
            path = expand_filename(last);
    }
    if (!path) {
        free(sel);
        return 1;
    }
    if (message_mbox_append(path, sel, mode, &newfile) < 0)
        r = 1;
    else
        for (int i = 1; i <= mb.n; i++)
            if (sel[i]) {
                if (mark_saved)
                    mb.m[i - 1].state = M_SAVED;
                else
                    mark_read(i);
            }
    set_cur(last_selected(sel));
    free(path);
    free(sel);
    return r;
}

static int c_copy(const char *raw, Words *w)
{
    (void)raw;
    return save_common(w, W_SAVE, 0, 0);
}

static int c_Copy(const char *raw, Words *w)
{
    (void)raw;
    return save_common(w, W_SAVE, 0, 1);
}

static int c_save(const char *raw, Words *w)
{
    (void)raw;
    return save_common(w, W_SAVE, 1, 0);
}

static int c_Save(const char *raw, Words *w)
{
    (void)raw;
    return save_common(w, W_SAVE, 1, 1);
}

static int c_write(const char *raw, Words *w)
{
    (void)raw;
    if (w->n == 0) {
        err("No file specified");
        return 1;
    }
    return save_common(w, W_BODY, 1, 0);
}

static int delete_sel(int *sel)
{
    for (int i = 1; i <= mb.n; i++)
        if (sel[i]) {
            Msg *m = &mb.m[i - 1];
            if (m->state != M_DELETED)
                m->prev_state = m->state;
            m->state = M_DELETED;
            m->to_mbox = m->touched = 0;
        }
    return last_selected(sel);
}

static int c_delete(const char *raw, Words *w)
{
    int *sel = new_sel(), hi;
    (void)raw;
    if (!need_mailbox() || parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
        free(sel);
        return 1;
    }
    hi = delete_sel(sel);
    cur = 0;
    set_current_after_delete(hi);
    cur_shown = 0;
    if (var_isset("autoprint") && cur)
        print_one(cur);
    free(sel);
    return 0;
}

static int c_dp(const char *raw, Words *w)
{
    int *sel = new_sel(), hi;
    (void)raw;
    if (!need_mailbox() || parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
        free(sel);
        return 1;
    }
    hi = delete_sel(sel);
    cur = 0;
    set_current_after_delete(hi);
    cur_shown = 0;
    if (cur > hi)
        print_one(cur);
    else
        printf("At EOF\n");
    free(sel);
    return 0;
}

static int c_discard(const char *raw, Words *w)
{
    (void)raw;
    if (w->n == 0) {
        for (int i = 0; i < ignored.n; i++)
            printf("%s\n", ignored.v[i]);
        return 0;
    }
    for (int i = 0; i < w->n; i++)
        if (!words_has(&ignored, w->v[i], 1))
            words_add(&ignored, w->v[i]);
    return 0;
}

static int c_retain(const char *raw, Words *w)
{
    (void)raw;
    if (w->n == 0) {
        for (int i = 0; i < retained.n; i++)
            printf("%s\n", retained.v[i]);
        return 0;
    }
    for (int i = 0; i < w->n; i++)
        if (!words_has(&retained, w->v[i], 1))
            words_add(&retained, w->v[i]);
    return 0;
}

static int c_echo(const char *raw, Words *w)
{
    (void)raw;
    for (int i = 0; i < w->n; i++) {
        if (i)
            putchar(' ');
        for (const char *p = w->v[i]; *p; p++) {
            if (*p != '\\' || !p[1]) {
                putchar(*p);
                continue;
            }
            switch (*++p) {
            case 'a': putchar('\a'); break;
            case 'b': putchar('\b'); break;
            case 'c': fflush(stdout); return 0;
            case 'f': putchar('\f'); break;
            case 'n': putchar('\n'); break;
            case 'r': putchar('\r'); break;
            case 't': putchar('\t'); break;
            case 'v': putchar('\v'); break;
            case '\\': putchar('\\'); break;
            case '0': {
                int v = 0, k = 0;
                while (k < 3 && p[1] >= '0' && p[1] <= '7') {
                    v = v * 8 + (*++p - '0');
                    k++;
                }
                putchar(v);
                break;
            }
            default:
                putchar('\\');
                putchar(*p);
            }
        }
    }
    putchar('\n');
    return 0;
}

static int edit_common(Words *w, const char *var, const char *dflt)
{
    int *sel = new_sel();
    const char *ed = var_get(var);
    if (!need_mailbox() || parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
        free(sel);
        return 1;
    }
    for (int i = 1; i <= mb.n; i++)
        if (sel[i]) {
            int fd;
            char *t = make_temp(&fd);
            FILE *fp;
            if (!t)
                break;
            fp = fdopen(fd, "w");
            msg_write(&mb.m[i - 1], fp, W_ALL);
            fclose(fp);
            run_interactive(ed && *ed ? ed : dflt, t);
            unlink(t);
            free(t);
            set_cur(i);
        }
    free(sel);
    return 0;
}

static int c_edit(const char *raw, Words *w)
{
    (void)raw;
    return edit_common(w, "EDITOR", "ed");
}

static int c_visual(const char *raw, Words *w)
{
    (void)raw;
    return edit_common(w, "VISUAL", "vi");
}

static int c_exit(const char *raw, Words *w)
{
    (void)raw;
    (void)w;
    return 3;
}

static int c_quit(const char *raw, Words *w)
{
    (void)raw;
    (void)w;
    return 2;
}

static void mailbox_status(void)
{
    int nnew = 0, nunread = 0, ndel = 0;
    for (int i = 0; i < mb.n; i++) {
        if (mb.m[i].state == M_NEW)
            nnew++;
        else if (mb.m[i].state == M_UNREAD)
            nunread++;
        else if (mb.m[i].state == M_DELETED)
            ndel++;
    }
    printf("\"%s\": %d message%s", mb.path, mb.n, mb.n == 1 ? "" : "s");
    if (nnew)
        printf(" %d new", nnew);
    if (nunread)
        printf(" %d unread", nunread);
    if (ndel)
        printf(" %d deleted", ndel);
    if (mb.readonly)
        printf(" [Read only]");
    putchar('\n');
}

static int c_file(const char *raw, Words *w)
{
    char *path, *sys;
    int is_sys;
    (void)raw;
    if (w->n == 0) {
        if (!need_mailbox())
            return 1;
        mailbox_status();
        return 0;
    }
    path = expand_filename(w->v[0]);
    if (!path)
        return 1;
    sys = system_mailbox(NULL);
    is_sys = strcmp(path, sys) == 0 || w->v[0][0] == '%';
    free(sys);
    if (mb.path) {
        mailbox_close(1);
        free(prev_file);
        prev_file = xstrdup(mb.path);
    }
    if (mailbox_open(path, is_sys, 0) < 0) {
        free(path);
        return 1;
    }
    free(path);
    mailbox_status();
    return 0;
}

static int c_folders(const char *raw, Words *w)
{
    char *dir = folder_dir();
    const char *lister = var_get("LISTER");
    pid_t pid;
    (void)raw;
    (void)w;
    if (!dir) {
        err("No value set for \"folder\"");
        return 1;
    }
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        if (chdir(dir) < 0) {
            fprintf(stderr, "mailx: %s: %s\n", dir, strerror(errno));
            _exit(1);
        }
        signal(SIGINT, SIG_DFL);
        execl("/bin/sh", "sh", "-c", lister && *lister ? lister : "ls", (char *)NULL);
        _exit(127);
    }
    if (pid > 0)
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
            ;
    free(dir);
    return 0;
}

static int c_from(const char *raw, Words *w)
{
    int *sel = new_sel();
    (void)raw;
    if (!need_mailbox() || parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
        free(sel);
        return 1;
    }
    for (int i = 1; i <= mb.n; i++)
        if (sel[i])
            summary_line(stdout, i);
    set_cur(last_selected(sel));
    free(sel);
    return 0;
}

static int c_headers(const char *raw, Words *w)
{
    (void)raw;
    if (!need_mailbox())
        return 1;
    if (!mb.n) {
        printf("No messages.\n");
        return 0;
    }
    if (w->n) {
        int *sel = new_sel(), m;
        if (parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
            free(sel);
            return 1;
        }
        m = first_selected(sel);
        free(sel);
        headers_page(m);
        set_cur(page_start);
        return 0;
    }
    headers_page(cur ? cur : 1);
    return 0;
}

static int c_z(const char *raw, Words *w)
{
    int size = screen_size(), target = page_start, k = 0;
    int *und, nu = 0, idx = 0;
    (void)raw;
    if (!need_mailbox() || !mb.n)
        return 1;
    und = xmalloc(((size_t)mb.n + 1) * sizeof(int));
    for (int i = 1; i <= mb.n; i++)
        if (mb.m[i - 1].state != M_DELETED)
            und[nu++] = i;
    for (int i = 0; i < nu; i++)
        if (und[i] <= target)
            idx = i;
    if (w->n && strcmp(w->v[0], "-") == 0) {
        if (idx == 0) {
            printf("On first screenful of messages\n");
            free(und);
            return 0;
        }
        idx = idx >= size ? idx - size : 0;
    } else {
        if (idx + size >= nu) {
            printf("On last screenful of messages\n");
            free(und);
            return 0;
        }
        idx += size;
    }
    page_start = und[idx];
    for (int i = idx; i < nu && k < size; i++, k++)
        summary_line(stdout, und[i]);
    free(und);
    return 0;
}

static const char help_text[] =
    "    mailx commands\n"
    "type [msglist]          print messages (also print)\n"
    "Type [msglist]          print messages with all header fields (also Print)\n"
    "next [message]          go to and print the next message\n"
    "top [msglist]           print the first lines of messages\n"
    "from [msglist]          header summaries of messages\n"
    "headers [message]       a page of header summaries; z [-] scrolls\n"
    "delete [msglist]        delete messages (dp, dt: and print the next)\n"
    "undelete [msglist]      undelete messages\n"
    "save [msglist] file     append messages to a file (Save: file named after author)\n"
    "copy [msglist] file     like save, without marking them saved (Copy)\n"
    "write [msglist] file    append the bodies of messages to a file\n"
    "reply [message]         reply to the sender and all recipients\n"
    "Reply [msglist]         reply to the senders only\n"
    "followup, Followup      reply and record the answer\n"
    "mail addresses          send a message\n"
    "preserve [msglist]      keep messages in the system mailbox (also hold)\n"
    "mbox [msglist]          move messages to mbox on quit; touch: unless hold\n"
    "pipe [msglist] command  pipe messages through a command (also |)\n"
    "size [msglist]          the sizes of messages\n"
    "file [file]             change mailbox (also folder); folders lists them\n"
    "set, unset              internal variables\n"
    "alias, unalias          address aliases (also group); alternates\n"
    "ignore, retain          header fields to show (also discard)\n"
    "edit, visual            edit messages\n"
    "source file             read commands from a file\n"
    "cd [directory]          change directory\n"
    "!command, shell         shell commands\n"
    "=                       the current message number\n"
    "quit                    quit, saving changes; exit, xit: without saving\n"
    "A msglist is: n  n-m  +  -  .  ^  $  *  address  /string  :d :n :o :r :u\n";

static int c_help(const char *raw, Words *w)
{
    (void)raw;
    (void)w;
    fputs(help_text, stdout);
    return 0;
}

static int hold_common(Words *w, int what)
{
    int *sel = new_sel();
    if (!need_mailbox())
        return 1;
    if (!mb.is_system) {
        err("Allowed only in the system mailbox");
        free(sel);
        return 1;
    }
    if (parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
        free(sel);
        return 1;
    }
    for (int i = 1; i <= mb.n; i++)
        if (sel[i]) {
            Msg *m = &mb.m[i - 1];
            if (what == 0) {
                /* preserve */
                if (m->state != M_PRESERVED && m->state != M_DELETED)
                    m->prev_state = m->state;
                m->state = M_PRESERVED;
                m->to_mbox = m->touched = 0;
            } else if (what == 1) {
                /* mbox */
                if (m->state == M_PRESERVED)
                    m->state = m->prev_state == M_NEW || m->prev_state == M_UNREAD ? M_READ : m->prev_state;
                m->to_mbox = 1;
                m->touched = 0;
            } else {
                /* touch */
                if (m->state == M_PRESERVED)
                    m->state = m->prev_state;
                m->touched = 1;
            }
        }
    set_cur(last_selected(sel));
    cur_shown = 1;
    free(sel);
    return 0;
}

static int c_hold(const char *raw, Words *w)
{
    (void)raw;
    return hold_common(w, 0);
}

static int c_mbox(const char *raw, Words *w)
{
    (void)raw;
    return hold_common(w, 1);
}

static int c_touch(const char *raw, Words *w)
{
    (void)raw;
    return hold_common(w, 2);
}

static int c_list(const char *raw, Words *w);

static int c_mail(const char *raw, Words *w)
{
    Compose c;
    (void)raw;
    if (w->n == 0) {
        err("No recipients specified");
        return 1;
    }
    memset(&c, 0, sizeof c);
    for (int i = 0; i < w->n; i++)
        split_addresses(w->v[i], &c.to);
    do_compose(&c);
    compose_free(&c);
    return 0;
}

static int c_next(const char *raw, Words *w)
{
    int target = 0;
    (void)raw;
    if (!need_mailbox())
        return 1;
    if (w->n) {
        int *sel = new_sel();
        if (parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
            free(sel);
            return 1;
        }
        target = first_selected(sel);
        free(sel);
    } else if (!mb.n) {
        printf("No messages\n");
        return 0;
    } else if (cur >= 1 && cur <= mb.n && !cur_shown && mb.m[cur - 1].state != M_DELETED)
        target = cur;
    else {
        for (int i = cur + 1; i <= mb.n; i++)
            if (mb.m[i - 1].state != M_DELETED) {
                target = i;
                break;
            }
    }
    if (!target) {
        printf("At EOF\n");
        return 0;
    }
    print_one(target);
    return 0;
}

static int c_pipe(const char *raw, Words *w)
{
    int *sel = new_sel(), fd, st;
    char *cmd = NULL, *t;
    FILE *fp;
    (void)raw;
    if (!need_mailbox()) {
        free(sel);
        return 1;
    }
    if (w->n == 0) {
        const char *c = var_get("cmd");
        if (!c || !*c) {
            err("No command specified (set cmd)");
            free(sel);
            return 1;
        }
        cmd = (char *)c;
        if (parse_msglist(w, 0, 0, sel, 0, 1) < 0) {
            free(sel);
            return 1;
        }
    } else if (msgs_and_last(w, sel, &cmd, 1) < 0) {
        free(sel);
        return 1;
    }
    t = make_temp(&fd);
    if (!t) {
        free(sel);
        return 1;
    }
    unlink(t);
    free(t);
    fp = fdopen(dup(fd), "w");
    for (int i = 1; i <= mb.n; i++)
        if (sel[i]) {
            msg_write(&mb.m[i - 1], fp, W_SHOW);
            if (var_isset("page"))
                fputc('\f', fp);
            mark_read(i);
        }
    fclose(fp);
    lseek(fd, 0, SEEK_SET);
    st = run_shell_cmd(cmd, fd, -1);
    close(fd);
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0))
        printf("\"%s\" exited with status %d\n", cmd, WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st));
    set_cur(last_selected(sel));
    free(sel);
    return 0;
}

static int c_Print(const char *raw, Words *w)
{
    int *sel = new_sel();
    (void)raw;
    if (!need_mailbox() || parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
        free(sel);
        return 1;
    }
    print_messages(sel, W_ALL);
    free(sel);
    return 0;
}

static int c_print(const char *raw, Words *w)
{
    int *sel = new_sel();
    (void)raw;
    if (!need_mailbox() || parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
        free(sel);
        return 1;
    }
    print_messages(sel, W_SHOW);
    free(sel);
    return 0;
}

static int reply_cmd(Words *w, int capital, int follow)
{
    int *sel = new_sel(), r;
    char *rec = NULL;
    if (!need_mailbox() || parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
        free(sel);
        return 1;
    }
    if (follow) {
        char *a = author_of(first_selected(sel));
        rec = record_name_for(a);
        free(a);
    } else {
        const char *rv = var_get("record");
        if (rv && *rv)
            rec = xstrdup(rv);
    }
    if (capital)
        r = reply_authors(sel, rec);
    else {
        int i = first_selected(sel);
        set_cur(i);
        r = reply_all(i, rec);
    }
    if (capital)
        set_cur(last_selected(sel));
    free(sel);
    return r;
}

static int c_reply(const char *raw, Words *w)
{
    (void)raw;
    return reply_cmd(w, var_isset("flipr"), 0);
}

static int c_Reply(const char *raw, Words *w)
{
    (void)raw;
    return reply_cmd(w, !var_isset("flipr"), 0);
}

static int c_followup(const char *raw, Words *w)
{
    (void)raw;
    return reply_cmd(w, 0, 1);
}

static int c_Followup(const char *raw, Words *w)
{
    (void)raw;
    return reply_cmd(w, 1, 1);
}

static int c_set(const char *raw, Words *w)
{
    (void)raw;
    if (w->n == 0) {
        var_list(stdout);
        return 0;
    }
    for (int i = 0; i < w->n; i++) {
        char *a = w->v[i], *eq = strchr(a, '=');
        if (eq) {
            char *name = xstrndup(a, (size_t)(eq - a));
            if (!*name) {
                err("Non-null variable name required");
                free(name);
                return 1;
            }
            var_set(name, eq + 1);
            free(name);
        } else if (strncmp(a, "no", 2) == 0 && a[2])
            var_unset(a + 2);
        else
            var_set(a, "");
    }
    return 0;
}

static int c_unset(const char *raw, Words *w)
{
    (void)raw;
    for (int i = 0; i < w->n; i++)
        var_unset(w->v[i]);
    return 0;
}

static int c_shell(const char *raw, Words *w)
{
    const char *sh = var_get("SHELL");
    (void)raw;
    (void)w;
    run_interactive(sh && *sh ? sh : "sh", NULL);
    return 0;
}

static int c_size(const char *raw, Words *w)
{
    int *sel = new_sel();
    (void)raw;
    if (!need_mailbox() || parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
        free(sel);
        return 1;
    }
    for (int i = 1; i <= mb.n; i++)
        if (sel[i])
            printf("%d: %ld\n", i, (long)(mb.m[i - 1].end - mb.m[i - 1].off));
    set_cur(last_selected(sel));
    free(sel);
    return 0;
}

static int c_source(const char *raw, Words *w)
{
    char *path;
    int r;
    (void)raw;
    if (w->n == 0) {
        err("source: file name required");
        return 1;
    }
    path = expand_filename(w->v[0]);
    if (!path)
        return 1;
    r = source_file(path, 0);
    free(path);
    return r < 0 ? 1 : 0;
}

static int c_top(const char *raw, Words *w)
{
    int *sel = new_sel(), n = var_num("toplines", 5);
    (void)raw;
    if (!need_mailbox() || parse_msglist(w, 0, w->n, sel, 0, 1) < 0) {
        free(sel);
        return 1;
    }
    for (int i = 1; i <= mb.n; i++)
        if (sel[i]) {
            printf("Message %d:\n", i);
            msg_write_lines(&mb.m[i - 1], stdout, W_FORWARD, NULL, n);
            mark_read(i);
            set_cur(i);
            cur_shown = 1;
        }
    fflush(stdout);
    free(sel);
    return 0;
}

static int c_unalias(const char *raw, Words *w)
{
    (void)raw;
    for (int i = 0; i < w->n; i++)
        alias_remove(w->v[i]);
    return 0;
}

static int c_undelete(const char *raw, Words *w)
{
    int *sel = new_sel(), last;
    (void)raw;
    if (!need_mailbox()) {
        free(sel);
        return 1;
    }
    if (!mb.n) {
        err("No messages");
        free(sel);
        return 1;
    }
    if (w->n == 0) {
        int pick = 0;
        for (int i = cur + 1; i <= mb.n && !pick; i++)
            if (mb.m[i - 1].state == M_DELETED)
                pick = i;
        for (int i = cur - 1; i >= 1 && !pick; i--)
            if (mb.m[i - 1].state == M_DELETED)
                pick = i;
        if (cur >= 1 && cur <= mb.n && mb.m[cur - 1].state == M_DELETED && !pick)
            pick = cur;
        if (!pick) {
            err("No deleted messages");
            free(sel);
            return 1;
        }
        sel[pick] = 1;
    } else if (parse_msglist(w, 0, w->n, sel, 1, 1) < 0) {
        free(sel);
        return 1;
    }
    for (int i = 1; i <= mb.n; i++)
        if (sel[i])
            mb.m[i - 1].state = M_READ;
    last = last_selected(sel);
    set_cur(last);
    if (var_isset("autoprint"))
        print_one(last);
    free(sel);
    return 0;
}

static int c_bang(const char *raw, Words *w)
{
    char *cmd = bang_expand(raw);
    (void)w;
    if (!cmd)
        return 1;
    run_shell_cmd(cmd, -1, -1);
    free(cmd);
    if (is_tty_in())
        printf("!\n");
    return 0;
}

static int c_comment(const char *raw, Words *w)
{
    (void)raw;
    (void)w;
    return 0;
}

static int c_equals(const char *raw, Words *w)
{
    (void)raw;
    (void)w;
    printf("%d\n", cur);
    return 0;
}

/* if/else/endif are handled in execute() */
static int c_cond(const char *raw, Words *w)
{
    (void)raw;
    (void)w;
    return 0;
}

enum { F_RAW = 1, F_NOSTARTUP = 2 };

static const struct Cmd {
    const char *name;
    int abbrev;
    Fn fn;
    int flags;
} cmds[] = {
    { "alias", 1, c_alias, 0 },        { "group", 1, c_alias, 0 },
    { "alternates", 3, c_alternates, 0 },
    { "cd", 2, c_cd, 0 },              { "chdir", 2, c_cd, 0 },
    { "copy", 1, c_copy, 0 },          { "Copy", 1, c_Copy, F_NOSTARTUP },
    { "delete", 1, c_delete, 0 },      { "discard", 2, c_discard, 0 },
    { "ignore", 2, c_discard, 0 },     { "dp", 2, c_dp, 0 },
    { "dt", 2, c_dp, 0 },              { "echo", 2, c_echo, 0 },
    { "edit", 1, c_edit, F_NOSTARTUP }, { "exit", 2, c_exit, 0 },
    { "xit", 1, c_exit, 0 },           { "file", 2, c_file, 0 },
    { "folders", 7, c_folders, 0 },    { "folder", 4, c_file, 0 },
    { "followup", 2, c_followup, F_NOSTARTUP },
    { "Followup", 1, c_Followup, F_NOSTARTUP },
    { "from", 1, c_from, 0 },          { "headers", 1, c_headers, 0 },
    { "help", 3, c_help, 0 },          { "?", 1, c_help, 0 },
    { "hold", 2, c_hold, F_NOSTARTUP }, { "preserve", 3, c_hold, F_NOSTARTUP },
    { "if", 1, c_cond, 0 },            { "else", 2, c_cond, 0 },
    { "endif", 2, c_cond, 0 },         { "list", 1, c_list, 0 },
    { "mail", 1, c_mail, F_NOSTARTUP }, { "mbox", 2, c_mbox, 0 },
    { "next", 1, c_next, 0 },          { "pipe", 2, c_pipe, 0 },
    { "|", 1, c_pipe, 0 },             { "Print", 1, c_Print, 0 },
    { "Type", 1, c_Print, 0 },         { "print", 1, c_print, 0 },
    { "type", 1, c_print, 0 },         { "quit", 1, c_quit, 0 },
    { "reply", 1, c_reply, F_NOSTARTUP }, { "respond", 1, c_reply, F_NOSTARTUP },
    { "Reply", 1, c_Reply, F_NOSTARTUP }, { "Respond", 1, c_Reply, F_NOSTARTUP },
    { "retain", 3, c_retain, 0 },      { "save", 1, c_save, 0 },
    { "Save", 1, c_Save, F_NOSTARTUP }, { "set", 2, c_set, 0 },
    { "shell", 2, c_shell, F_NOSTARTUP }, { "size", 2, c_size, 0 },
    { "source", 2, c_source, 0 },      { "top", 2, c_top, 0 },
    { "touch", 3, c_touch, 0 },        { "unalias", 3, c_unalias, 0 },
    { "undelete", 1, c_undelete, 0 },  { "unset", 3, c_unset, 0 },
    { "visual", 1, c_visual, F_NOSTARTUP }, { "write", 1, c_write, 0 },
    { "z", 1, c_z, 0 },                { "!", 1, c_bang, F_RAW | F_NOSTARTUP },
    { "#", 1, c_comment, F_RAW },      { "=", 1, c_equals, 0 },
    { NULL, 0, NULL, 0 },
};

static int c_list(const char *raw, Words *w)
{
    int col = 0;
    (void)raw;
    (void)w;
    for (const struct Cmd *c = cmds; c->name; c++) {
        size_t l = strlen(c->name);
        if (col + (int)l + 1 > 72) {
            putchar('\n');
            col = 0;
        }
        printf("%s%s", col ? " " : "", c->name);
        col += (int)l + (col ? 1 : 0);
    }
    putchar('\n');
    return 0;
}

static const struct Cmd *lookup(const char *word)
{
    size_t l = strlen(word);
    for (const struct Cmd *c = cmds; c->name; c++)
        if (strcmp(c->name, word) == 0)
            return c;
    for (const struct Cmd *c = cmds; c->name; c++)
        if (l >= (size_t)c->abbrev && strncmp(c->name, word, l) == 0)
            return c;
    return NULL;
}

int execute(const char *line, int interactive)
{
    const char *p = line, *args;
    char word[64];
    size_t wl = 0;
    const struct Cmd *c;
    Words w = { 0 };
    int r, active = cond_depth == 0 || cond_active[cond_depth - 1];
    while (*p == ' ' || *p == '\t')
        p++;
    if (!*p) {
        if (!interactive || !active)
            return 0;
        p = "next";
    }
    if (*p == '#')
        return 0;
    if (strchr("!|=?#", *p)) {
        word[0] = *p;
        word[1] = 0;
        args = p + 1;
    } else {
        while (isalpha((unsigned char)p[wl]) && wl < sizeof word - 1) {
            word[wl] = p[wl];
            wl++;
        }
        word[wl] = 0;
        args = p + wl;
        if (!wl)
            strcpy(word, "next");
    }
    c = lookup(word);
    /* conditionals */
    if (c && c->fn == c_cond) {
        if (strcmp(c->name, "if") == 0) {
            Words a = { 0 };
            int cond = 0;
            split_args(args, &a);
            if (a.n && (strcmp(a.v[0], "s") == 0 || strcmp(a.v[0], "r") == 0))
                cond = (a.v[0][0] == 's') == (send_mode != 0);
            else
                err("if: s or r expected");
            words_free(&a);
            if (cond_depth >= 64) {
                err("if: nested too deeply");
                return 1;
            }
            cond_active[cond_depth] = active && cond;
            cond_seen_else[cond_depth] = 0;
            cond_depth++;
            return 0;
        }
        if (cond_depth == 0) {
            err("%s without if", c->name);
            return 1;
        }
        if (strcmp(c->name, "else") == 0) {
            int outer = cond_depth < 2 || cond_active[cond_depth - 2];
            if (cond_seen_else[cond_depth - 1]) {
                err("else after else");
                return 1;
            }
            cond_seen_else[cond_depth - 1] = 1;
            cond_active[cond_depth - 1] = outer && !cond_active[cond_depth - 1];
            return 0;
        }
        cond_depth--;
        return 0;
    }
    if (!active)
        return 0;
    if (!c) {
        err("Unknown command: \"%s\"", word);
        return 1;
    }
    if (in_startup && (c->flags & F_NOSTARTUP)) {
        err("%s: not valid in a start-up file", c->name);
        return 1;
    }
    if (c->flags & F_RAW)
        return c->fn(args, &w);
    if (split_args(args, &w) < 0) {
        err("Unmatched quote");
        return 1;
    }
    r = c->fn(args, &w);
    words_free(&w);
    return r;
}

/* ---------- start-up files and the command loop ---------- */

int source_file(const char *path, int startup)
{
    FILE *fp = fopen(path, "r");
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int save = in_startup, depth = cond_depth, r = 0;
    Buf acc = { 0 };
    if (!fp) {
        if (!startup)
            err("%s: %s", path, strerror(errno));
        return -1;
    }
    in_startup = startup;
    while ((n = getline(&line, &cap, fp)) > 0) {
        int res;
        if (line[n - 1] == '\n')
            line[--n] = 0;
        if (n && line[n - 1] == '\\') {
            buf_add(&acc, line, (size_t)n - 1);
            continue;
        }
        buf_adds(&acc, line);
        res = execute(acc.s, 0);
        acc.len = 0;
        acc.s[0] = 0;
        if (res == 1 && startup) {
            err("%s: the rest of the file is ignored", path);
            r = -1;
            break;
        }
        if (res == 2 || res == 3) {
            /* quit and exit in a start-up file */
            if (!startup) {
                free(line);
                buf_free(&acc);
                fclose(fp);
                in_startup = save;
                return res;
            }
        }
    }
    free(line);
    buf_free(&acc);
    fclose(fp);
    in_startup = save;
    if (cond_depth > depth) {
        err("%s: missing endif", path);
        cond_depth = depth;
    }
    return r;
}

static void on_sigint(int sig)
{
    (void)sig;
    got_sigint = 1;
    if (cmd_jmp_ok) {
        cmd_jmp_ok = 0;
        siglongjmp(cmd_jmp, 1);
    }
}

void install_sigint(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;     /* reads are interrupted */
    sigaction(SIGINT, &sa, NULL);
}

void command_loop(void)
{
    Buf line = { 0 };
    int interactive = is_tty_in();
    for (;;) {
        int r, res;
        if (sigsetjmp(cmd_jmp, 1)) {
            got_sigint = 0;
            fflush(stdout);
            putchar('\n');
        }
        cmd_jmp_ok = 0;
        if (interactive) {
            const char *pr = var_get("prompt");
            if (pr && *pr) {
                fputs(pr, stdout);
            }
        }
        fflush(stdout);
        line.len = 0;
        if (line.s)
            line.s[0] = 0;
        r = command_line_read(&line);
        if (r < 0) {
            putchar('\n');
            continue;
        }
        if (r == 0)
            res = 2;
        else {
            cmd_jmp_ok = 1;
            res = execute(line.s, 1);
            cmd_jmp_ok = 0;
        }
        if (res == 1)
            exit_status = 1;
        if (res == 2) {
            if (mailbox_close(1) < 0)
                exit_status = 1;
            fflush(stdout);
            exit(exit_status);
        }
        if (res == 3) {
            fflush(stdout);
            exit(exit_status);
        }
    }
}
