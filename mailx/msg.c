/*
 * msg.c - mailboxes: reading mbox files, message states, header
 * fields, header summaries, message lists, writing messages, and the
 * processing done when a mailbox is closed (quit).
 */
#include "mailx.h"

Mailbox mb;
int cur, cur_shown;
char *prev_file;
static char *last_search;

/* ---------- locking ---------- */

static char *dotlock_path;

static void dotlock(const char *path)
{
    Buf b = { 0 };
    buf_fmt(&b, "%s.lock", path);
    for (int tries = 0; tries < 10; tries++) {
        int fd = open(b.s, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            close(fd);
            dotlock_path = buf_take(&b);
            return;
        }
        if (errno != EEXIST)
            break;   /* no permission in the directory: rely on fcntl */
        {
            struct stat st;
            /* a stale lock file older than five minutes is removed */
            if (stat(b.s, &st) == 0 && time(NULL) - st.st_mtime > 300)
                unlink(b.s);
        }
        sleep(1);
    }
    buf_free(&b);
}

static void dotunlock(void)
{
    if (dotlock_path) {
        unlink(dotlock_path);
        free(dotlock_path);
        dotlock_path = NULL;
    }
}

static int lock_fd(int fd, int type)
{
    struct flock fl;
    memset(&fl, 0, sizeof fl);
    fl.l_type = (short)type;
    fl.l_whence = SEEK_SET;
    while (fcntl(fd, F_SETLKW, &fl) < 0) {
        if (errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

static char *read_all(int fd, size_t *len)
{
    Buf b = { 0 };
    char tmp[65536];
    ssize_t n;
    while ((n = read(fd, tmp, sizeof tmp)) != 0) {
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        buf_add(&b, tmp, (size_t)n);
    }
    *len = b.len;
    return b.s ? buf_take(&b) : xstrdup("");
}

/* ---------- parsing ---------- */

static int is_from_line(const char *p, const char *end)
{
    return end - p >= 5 && memcmp(p, "From ", 5) == 0;
}

static void parse_messages(void)
{
    const char *t = mb.text, *end = t + mb.len, *p = t;
    int cap = 0;
    mb.n = 0;
    free(mb.m);
    mb.m = NULL;
    while (p < end) {
        Msg m;
        const char *q, *hdr;
        int prev_empty = 0;
        memset(&m, 0, sizeof m);
        m.off = (size_t)(p - t);
        /* the header ends at the first empty line */
        q = p;
        if (is_from_line(q, end)) {
            q = memchr(q, '\n', (size_t)(end - q));
            q = q ? q + 1 : end;
        }
        hdr = q;
        while (q < end && *q != '\n') {
            const char *nl = memchr(q, '\n', (size_t)(end - q));
            q = nl ? nl + 1 : end;
        }
        if (q < end)
            q++;   /* the empty line */
        m.hdr_end = (size_t)(q - t);
        (void)hdr;
        /* the body ends where a From line follows an empty line */
        prev_empty = 1;
        while (q < end) {
            const char *nl;
            if (prev_empty && is_from_line(q, end) && q != t + m.off)
                break;
            nl = memchr(q, '\n', (size_t)(end - q));
            prev_empty = (*q == '\n');
            q = nl ? nl + 1 : end;
        }
        m.end = (size_t)(q - t);
        if (mb.n + 1 > cap) {
            cap = cap ? cap * 2 : 64;
            mb.m = xrealloc(mb.m, (size_t)cap * sizeof(Msg));
        }
        for (const char *r = t + m.off; r < t + m.end; r++)
            if (*r == '\n')
                m.lines++;
        if (m.end > m.off && t[m.end - 1] != '\n')
            m.lines++;
        mb.m[mb.n++] = m;
        p = q;
    }
    for (int i = 0; i < mb.n; i++) {
        char *st = msg_header(&mb.m[i], "Status");
        Msg *m = &mb.m[i];
        if (st && strchr(st, 'R')) {
            m->state = M_READ;
            m->was_read = 1;
        } else if (st && strchr(st, 'O'))
            m->state = M_UNREAD;
        else
            m->state = M_NEW;
        free(st);
    }
}

int mailbox_open(const char *path, int is_system, int quiet_missing)
{
    int fd;
    struct stat st;
    (void)quiet_missing;
    free(mb.path);
    free(mb.text);
    free(mb.m);
    memset(&mb, 0, sizeof mb);
    mb.path = xstrdup(path);
    mb.is_system = is_system;
    cur = 0;
    cur_shown = 0;
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (errno != ENOENT) {
            err("%s: %s", path, strerror(errno));
            mb.text = xstrdup("");
            return -1;
        }
        mb.text = xstrdup("");
        return 0;
    }
    if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
        err("%s: is a directory", path);
        close(fd);
        mb.text = xstrdup("");
        return -1;
    }
    lock_fd(fd, F_RDLCK);
    mb.text = read_all(fd, &mb.len);
    if (fstat(fd, &st) == 0) {
        mb.mtime = st.st_mtim;
        mb.size = st.st_size;
    }
    close(fd);
    mb.readonly = access(path, W_OK) != 0;
    parse_messages();
    cur = 0;
    for (int i = 0; i < mb.n && !cur; i++)
        if (mb.m[i].state == M_NEW)
            cur = i + 1;
    for (int i = 0; i < mb.n && !cur; i++)
        if (mb.m[i].state == M_UNREAD)
            cur = i + 1;
    if (!cur && mb.n)
        cur = 1;
    cur_shown = 0;
    return 0;
}

/* ---------- header fields ---------- */

char *msg_from_line(Msg *m)
{
    const char *p = mb.text + m->off, *end = mb.text + m->hdr_end;
    const char *nl;
    if (!is_from_line(p, end))
        return NULL;
    nl = memchr(p, '\n', (size_t)(end - p));
    return xstrndup(p, nl ? (size_t)(nl - p) : (size_t)(end - p));
}

static const char *header_start(Msg *m)
{
    const char *p = mb.text + m->off, *end = mb.text + m->hdr_end;
    if (is_from_line(p, end)) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        p = nl ? nl + 1 : end;
    }
    return p;
}

char *msg_header(Msg *m, const char *field)
{
    const char *p = header_start(m), *end = mb.text + m->hdr_end;
    size_t fl = strlen(field);
    while (p < end && *p != '\n') {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *le = nl ? nl : end;
        if ((size_t)(le - p) > fl && strncasecmp(p, field, fl) == 0 && p[fl] == ':') {
            Buf v = { 0 };
            const char *s = p + fl + 1;
            while (s < le && (*s == ' ' || *s == '\t'))
                s++;
            buf_add(&v, s, (size_t)(le - s));
            p = nl ? nl + 1 : end;
            /* continuation lines */
            while (p < end && (*p == ' ' || *p == '\t')) {
                const char *n2 = memchr(p, '\n', (size_t)(end - p));
                const char *e2 = n2 ? n2 : end;
                buf_addc(&v, ' ');
                while (p < e2 && (*p == ' ' || *p == '\t'))
                    p++;
                buf_add(&v, p, (size_t)(e2 - p));
                p = n2 ? n2 + 1 : end;
            }
            return buf_take(&v);
        }
        p = nl ? nl + 1 : end;
    }
    return NULL;
}

char *msg_sender(Msg *m)
{
    char *f = msg_header(m, "From");
    char *s;
    if (f) {
        Words w = { 0 };
        split_addresses(f, &w);
        s = addr_spec(w.n ? w.v[0] : f);
        words_free(&w);
        free(f);
        if (*s)
            return s;
        free(s);
    }
    f = msg_from_line(m);
    if (f) {
        char *sp = strchr(f + 5, ' ');
        s = xstrndup(f + 5, sp ? (size_t)(sp - f - 5) : strlen(f + 5));
        free(f);
        return s;
    }
    return xstrdup("");
}

/* ---------- summaries ---------- */

void summary_line(FILE *fp, int i)
{
    Msg *m = &mb.m[i - 1];
    char *from = msg_sender(m), *subj = msg_header(m, "Subject"), *fl = msg_from_line(m);
    char date[32] = "";
    char state;
    const char *who = from;
    char *to = NULL;
    switch (m->state) {
    case M_NEW:
        state = 'N';
        break;
    case M_UNREAD:
        state = 'U';
        break;
    case M_PRESERVED:
        state = 'P';
        break;
    case M_SAVED:
        state = '*';
        break;
    case M_DELETED:
        state = 'D';
        break;
    default:
        state = m->was_read ? ' ' : 'R';
    }
    if (m->to_mbox)
        state = 'M';
    if (fl) {
        /* From sender date */
        char *sp = strchr(fl + 5, ' ');
        if (sp) {
            while (*sp == ' ')
                sp++;
            snprintf(date, sizeof date, "%.16s", sp);
        }
    }
    if (!*date) {
        char *d = msg_header(m, "Date");
        if (d) {
            snprintf(date, sizeof date, "%.16s", d);
            free(d);
        }
    }
    if (var_isset("showto") && addr_is_me(from)) {
        char *t = msg_header(m, "To");
        if (t) {
            Words w = { 0 };
            Buf b = { 0 };
            split_addresses(t, &w);
            if (w.n) {
                char *a = addr_spec(w.v[0]);
                buf_fmt(&b, "To %s", a);
                free(a);
                to = buf_take(&b);
                who = to;
            }
            words_free(&w);
            free(t);
        }
    }
    {
        char *ds = subj ? decode_header(subj) : xstrdup("");
        fprintf(fp, "%c%c%3d %-18.18s %-16.16s %4d/%-5ld %s\n", i == cur ? '>' : ' ', state, i, who, date,
                m->lines, (long)(m->end - m->off), ds);
        free(ds);
    }
    free(to);
    free(from);
    free(subj);
    free(fl);
}

void mark_read(int i)
{
    Msg *m = &mb.m[i - 1];
    if (m->state == M_NEW || m->state == M_UNREAD)
        m->state = M_READ;
}

/* ---------- writing messages ---------- */

/* the body without the empty line that separates it from the next message */
static void body_range(Msg *m, const char **b, const char **e)
{
    *b = mb.text + m->hdr_end;
    *e = mb.text + m->end;
    if (*e > *b && (*e)[-1] == '\n' && *e - 1 > *b && (*e)[-2] == '\n')
        (*e)--;
    else if (*e - *b == 1 && **b == '\n')
        *e = *b;
}

static char *status_value(Msg *m)
{
    switch (m->state) {
    case M_NEW:
        return "O";
    case M_UNREAD:
        return "O";
    case M_PRESERVED:
        return m->prev_state == M_NEW || m->prev_state == M_UNREAD ? "O" : "RO";
    default:
        return "RO";
    }
}

/*
 * Write a message.  Lines are counted for top (maxlines < 0: all) and
 * prefixed (~m) when prefix is set (only non-empty lines).
 */
void msg_write_lines(Msg *m, FILE *fp, int mode, const char *prefix, int maxlines)
{
    const char *p = mb.text + m->off, *hend = mb.text + m->hdr_end, *b, *e;
    int count = 0, skipping = 0;
#define EMIT(s, l)                                                     \
    do {                                                               \
        if (maxlines >= 0 && count >= maxlines)                        \
            return;                                                    \
        if (prefix && (l) > 1)                                         \
            fputs(prefix, fp);                                         \
        fwrite((s), 1, (l), fp);                                       \
        count++;                                                       \
    } while (0)
    if (mode == W_SAVE) {
        /* the message as stored, with its Status field brought up to date */
        if (!is_from_line(p, hend)) {
            time_t now = time(NULL);
            fprintf(fp, "From %s %s", login_name(), ctime(&now));
        }
        int blank = 0;
        for (; p < hend;) {
            const char *nl = memchr(p, '\n', (size_t)(hend - p));
            size_t l = nl ? (size_t)(nl - p + 1) : (size_t)(hend - p);
            if (strncasecmp(p, "Status:", 7) == 0) {
                p += l;
                while (p < hend && (*p == ' ' || *p == '\t')) {
                    nl = memchr(p, '\n', (size_t)(hend - p));
                    p = nl ? nl + 1 : hend;
                }
                continue;
            }
            if (l == 1 && *p == '\n') {
                fprintf(fp, "Status: %s\n", status_value(m));
                fputc('\n', fp);
                p += l;
                blank = 1;
                break;
            }
            fwrite(p, 1, l, fp);
            if (!nl)
                fputc('\n', fp);
            p += l;
        }
        if (!blank) {
            /* a message without an empty line after its header */
            fprintf(fp, "Status: %s\n\n", status_value(m));
        }
        body_range(m, &b, &e);
        fwrite(b, 1, (size_t)(e - b), fp);
        if (e > b && e[-1] != '\n')
            fputc('\n', fp);
        fputc('\n', fp);
        return;
    }
    if (mode != W_BODY) {
        if (mode == W_SHOW || mode == W_ALL) {
            if (is_from_line(p, hend)) {
                const char *nl = memchr(p, '\n', (size_t)(hend - p));
                size_t l = nl ? (size_t)(nl - p + 1) : (size_t)(hend - p);
                EMIT(p, l);
            }
        }
        p = header_start(m);
        while (p < hend) {
            const char *nl = memchr(p, '\n', (size_t)(hend - p));
            size_t l = nl ? (size_t)(nl - p + 1) : (size_t)(hend - p);
            if (*p == '\n')
                break;
            if (*p != ' ' && *p != '\t') {
                const char *colon = memchr(p, ':', l);
                skipping = 0;
                if (colon && (mode == W_SHOW || mode == W_FORWARD)) {
                    char *fname = xstrndup(p, (size_t)(colon - p));
                    skipping = !header_shown(fname);
                    free(fname);
                }
            }
            if (!skipping)
                EMIT(p, l);
            p += l;
        }
        EMIT("\n", 1);
    }
    body_range(m, &b, &e);
    for (p = b; p < e;) {
        const char *nl = memchr(p, '\n', (size_t)(e - p));
        size_t l = nl ? (size_t)(nl - p + 1) : (size_t)(e - p);
        EMIT(p, l);
        if (!nl)
            fputc('\n', fp);
        p += l;
    }
#undef EMIT
}

void msg_write(Msg *m, FILE *fp, int mode)
{
    msg_write_lines(m, fp, mode, NULL, -1);
}

int message_mbox_append(const char *path, int *sel, int mode, int *newfile)
{
    FILE *fp;
    struct stat st;
    long lines = 0, chars = 0;
    *newfile = stat(path, &st) != 0;
    fp = fopen(path, "a");
    if (!fp) {
        err("%s: %s", path, strerror(errno));
        return -1;
    }
    if (*newfile)
        fchmod(fileno(fp), 0600);
    lock_fd(fileno(fp), F_WRLCK);
    {
        long start = ftell(fp);
        for (int i = 1; i <= mb.n; i++)
            if (sel[i]) {
                /* the copy is a read message */
                int was = mb.m[i - 1].state;
                mb.m[i - 1].state = M_READ;
                msg_write(&mb.m[i - 1], fp, mode);
                mb.m[i - 1].state = was;
            }
        fflush(fp);
        chars = ftell(fp) - start;
    }
    if (ferror(fp)) {
        err("%s: write error", path);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    {
        /* count the lines that were written */
        FILE *r = fopen(path, "r");
        if (r) {
            int c;
            fseek(r, -chars, SEEK_END);
            while ((c = getc(r)) != EOF)
                if (c == '\n')
                    lines++;
            fclose(r);
        }
    }
    printf("\"%s\" %s %ld/%ld\n", path, *newfile ? "[New file]" : "[Appended]", lines, chars);
    return 0;
}

/* ---------- message lists ---------- */

static int selectable(int i, int for_undelete)
{
    int del = mb.m[i - 1].state == M_DELETED;
    return for_undelete ? del : !del;
}

static int matches_type(Msg *m, int c)
{
    switch (c) {
    case 'd':
        return m->state == M_DELETED;
    case 'n':
        return m->state == M_NEW;
    case 'o':
        return m->state != M_READ && m->state != M_NEW;
    case 'r':
        return m->state == M_READ;
    case 'u':
        return m->state == M_UNREAD;
    }
    return 0;
}

static int matches_sender(Msg *m, const char *who)
{
    char *s = msg_sender(m);
    int r = strcasecmp(s, who) == 0;
    if (!r) {
        char *a = addr_login(s), *b = addr_login(who);
        if (strcasecmp(a, who) == 0 || (var_isset("allnet") && strcasecmp(a, b) == 0))
            r = 1;
        free(a);
        free(b);
    }
    free(s);
    return r;
}

static int contains_icase(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    for (; *hay; hay++)
        if (strncasecmp(hay, needle, nl) == 0)
            return 1;
    return nl == 0;
}

static int parse_num(const char *s, int *v)
{
    char *end;
    long n;
    if (!isdigit((unsigned char)*s))
        return 0;
    n = strtol(s, &end, 10);
    if (*end)
        return 0;
    *v = (int)n;
    return 1;
}

int parse_msglist(Words *w, int from, int to, int *sel, int for_undelete, int dflt_current)
{
    int count = 0;
    for (int i = 0; i <= mb.n; i++)
        sel[i] = 0;
    if (mb.n == 0) {
        err("No messages");
        return -1;
    }
    if (from >= to) {
        if (!dflt_current)
            return 0;
        if (cur < 1 || cur > mb.n || !selectable(cur, for_undelete)) {
            err("No applicable messages");
            return -1;
        }
        sel[cur] = 1;
        return 1;
    }
    for (int k = from; k < to; k++) {
        const char *t = w->v[k];
        int n, m;
        const char *dash;
        if (parse_num(t, &n)) {
            if (n < 1 || n > mb.n) {
                err("%d: Invalid message number", n);
                return -1;
            }
            if (!selectable(n, for_undelete)) {
                err("%d: Inappropriate message", n);
                return -1;
            }
            sel[n] = 1;
            continue;
        }
        if ((dash = strchr(t, '-')) && dash != t && isdigit((unsigned char)*t)) {
            char *a = xstrndup(t, (size_t)(dash - t));
            int ok = parse_num(a, &n) && parse_num(dash + 1, &m);
            free(a);
            if (ok) {
                if (n < 1 || m > mb.n || n > m) {
                    err("%s: Invalid range", t);
                    return -1;
                }
                for (int i = n; i <= m; i++)
                    if (selectable(i, for_undelete))
                        sel[i] = 1;
                continue;
            }
        }
        if (strcmp(t, ".") == 0) {
            if (cur >= 1 && cur <= mb.n)
                sel[cur] = 1;
            continue;
        }
        if (strcmp(t, "+") == 0 || strcmp(t, "-") == 0) {
            int step = *t == '+' ? 1 : -1, i;
            for (i = cur + step; i >= 1 && i <= mb.n; i += step)
                if (selectable(i, for_undelete))
                    break;
            if (i < 1 || i > mb.n) {
                err("No %s message", *t == '+' ? "next" : "previous");
                return -1;
            }
            sel[i] = 1;
            continue;
        }
        if (strcmp(t, "^") == 0) {
            for (int i = 1; i <= mb.n; i++)
                if (selectable(i, for_undelete)) {
                    sel[i] = 1;
                    break;
                }
            continue;
        }
        if (strcmp(t, "$") == 0) {
            for (int i = mb.n; i >= 1; i--)
                if (selectable(i, for_undelete)) {
                    sel[i] = 1;
                    break;
                }
            continue;
        }
        if (strcmp(t, "*") == 0) {
            for (int i = 1; i <= mb.n; i++)
                if (selectable(i, for_undelete))
                    sel[i] = 1;
            continue;
        }
        if (t[0] == '/') {
            const char *s = t + 1;
            if (!*s && last_search)
                s = last_search;
            else {
                free(last_search);
                last_search = xstrdup(s);
            }
            for (int i = 1; i <= mb.n; i++) {
                char *subj;
                if (!selectable(i, for_undelete))
                    continue;
                subj = msg_header(&mb.m[i - 1], "Subject");
                if (subj) {
                    char *d = decode_header(subj);
                    if (contains_icase(d, s))
                        sel[i] = 1;
                    free(d);
                }
                free(subj);
            }
            continue;
        }
        if (t[0] == ':' && t[1] && !t[2] && strchr("dnoru", t[1])) {
            for (int i = 1; i <= mb.n; i++)
                if ((t[1] == 'd' || selectable(i, for_undelete)) && matches_type(&mb.m[i - 1], t[1]))
                    sel[i] = 1;
            continue;
        }
        if (t[0] == ':') {
            err("%s: unknown message type", t);
            return -1;
        }
        /* an address */
        for (int i = 1; i <= mb.n; i++)
            if (selectable(i, for_undelete) && matches_sender(&mb.m[i - 1], t))
                sel[i] = 1;
    }
    for (int i = 1; i <= mb.n; i++)
        count += sel[i];
    if (!count) {
        err("No applicable messages");
        return -1;
    }
    return count;
}

int first_selected(int *sel)
{
    for (int i = 1; i <= mb.n; i++)
        if (sel[i])
            return i;
    return 0;
}

int last_selected(int *sel)
{
    for (int i = mb.n; i >= 1; i--)
        if (sel[i])
            return i;
    return 0;
}

void set_current_after_delete(int highest)
{
    for (int i = highest + 1; i <= mb.n; i++)
        if (mb.m[i - 1].state != M_DELETED) {
            cur = i;
            return;
        }
    for (int i = highest - 1; i >= 1; i--)
        if (mb.m[i - 1].state != M_DELETED) {
            cur = i;
            return;
        }
    cur = 0;
}

/* ---------- closing a mailbox ---------- */

static int write_fully(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t r = write(fd, p, n);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

/* the text of a message for writing back, as by save */
static void append_message(Buf *b, Msg *m)
{
    char *s = NULL;
    size_t sz = 0;
    FILE *mem = open_memstream(&s, &sz);
    msg_write(m, mem, W_SAVE);
    fclose(mem);
    buf_add(b, s, sz);
    free(s);
}

static int status_changed(Msg *m)
{
    char *st = msg_header(m, "Status");
    int r = !st || strcmp(st, status_value(m)) != 0;
    free(st);
    return r;
}

/* replace the contents of path, or remove it if empty and keep is not set */
static int rewrite_file(const char *path, const char *text, size_t len, int fd)
{
    if (len == 0 && !var_isset("keep")) {
        if (unlink(path) == 0)
            return 0;
    }
    if (lseek(fd, 0, SEEK_SET) < 0 || write_fully(fd, text, len) < 0 || ftruncate(fd, (off_t)len) < 0) {
        err("%s: %s", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int add_to_mbox(Buf *moved, int count)
{
    char *path = mbox_path();
    int fd = open(path, O_RDWR | O_CREAT, 0600);
    int r = 0;
    if (fd < 0) {
        err("%s: %s", path, strerror(errno));
        free(path);
        return -1;
    }
    lock_fd(fd, F_WRLCK);
    if (var_isset("append")) {
        lseek(fd, 0, SEEK_END);
        if (write_fully(fd, moved->s, moved->len) < 0)
            r = -1;
    } else {
        size_t ol;
        char *old;
        lseek(fd, 0, SEEK_SET);
        old = read_all(fd, &ol);
        {
            Buf all = { 0 };
            buf_add(&all, moved->s, moved->len);
            buf_add(&all, old, ol);
            if (lseek(fd, 0, SEEK_SET) < 0 || write_fully(fd, all.s, all.len) < 0 ||
                ftruncate(fd, (off_t)all.len) < 0)
                r = -1;
            buf_free(&all);
        }
        free(old);
    }
    if (r < 0)
        err("%s: %s", path, strerror(errno));
    else
        printf("Saved %d message%s in %s\n", count, count == 1 ? "" : "s", path);
    close(fd);
    free(path);
    return r;
}

int mailbox_close(int do_quit)
{
    Buf keep = { 0 }, moved = { 0 };
    int nkeep = 0, nmoved = 0, changed = 0, fd, r = 0;
    char *mboxp;
    if (!do_quit || !mb.path)
        return 0;
    mboxp = mbox_path();
    for (int i = 0; i < mb.n; i++) {
        Msg *m = &mb.m[i];
        int to_mbox = 0, drop = 0;
        if (m->state == M_DELETED)
            drop = 1;
        else if (mb.is_system) {
            if (m->state == M_SAVED)
                drop = !var_isset("keepsave"), to_mbox = !drop;
            else if (m->to_mbox)
                to_mbox = 1;
            else if (m->state == M_PRESERVED)
                ;
            else if (m->touched || m->state == M_READ)
                to_mbox = !var_isset("hold");
        }
        if (drop) {
            changed = 1;
            continue;
        }
        if (to_mbox) {
            int st = m->state;
            m->state = M_READ;
            append_message(&moved, m);
            m->state = st;
            nmoved++;
            changed = 1;
            continue;
        }
        if (status_changed(m))
            changed = 1;
        append_message(&keep, m);
        nkeep++;
    }
    if (!changed) {
        if (!mb.is_system && mb.n)
            printf("\"%s\" complete\n", mb.path);
        free(mboxp);
        buf_free(&keep);
        buf_free(&moved);
        return 0;
    }
    if (mb.readonly) {
        err("%s: not writable; changes discarded", mb.path);
        free(mboxp);
        buf_free(&keep);
        buf_free(&moved);
        return -1;
    }
    if (nmoved && strcmp(mboxp, mb.path) != 0 && add_to_mbox(&moved, nmoved) < 0) {
        /* keep everything where it was rather than lose messages */
        free(mboxp);
        buf_free(&keep);
        buf_free(&moved);
        return -1;
    }
    if (mb.is_system)
        dotlock(mb.path);
    fd = open(mb.path, O_RDWR);
    if (fd < 0) {
        if (errno == ENOENT)
            fd = open(mb.path, O_RDWR | O_CREAT, 0600);
        if (fd < 0) {
            err("%s: %s", mb.path, strerror(errno));
            dotunlock();
            free(mboxp);
            buf_free(&keep);
            buf_free(&moved);
            return -1;
        }
    }
    lock_fd(fd, F_WRLCK);
    {
        /* mail that arrived while the mailbox was open is kept */
        struct stat st;
        if (fstat(fd, &st) == 0 && st.st_size > mb.size) {
            size_t nl;
            char *now;
            lseek(fd, 0, SEEK_SET);
            now = read_all(fd, &nl);
            if (nl > (size_t)mb.size) {
                const char *extra = now + mb.size;
                size_t el = nl - (size_t)mb.size;
                if (keep.len && keep.s[keep.len - 1] == '\n' && !(keep.len > 1 && keep.s[keep.len - 2] == '\n') &&
                    el >= 5 && memcmp(extra, "From ", 5) == 0)
                    buf_addc(&keep, '\n');
                buf_add(&keep, extra, el);
                printf("New mail has arrived.\n");
            }
            free(now);
        }
    }
    if (rewrite_file(mb.path, keep.s ? keep.s : "", keep.len, fd) < 0)
        r = -1;
    close(fd);
    dotunlock();
    if (mb.is_system && nkeep)
        printf("Held %d message%s in %s\n", nkeep, nkeep == 1 ? "" : "s", mb.path);
    else if (!mb.is_system)
        printf("\"%s\" complete\n", mb.path);
    free(mboxp);
    buf_free(&keep);
    buf_free(&moved);
    return r;
}
