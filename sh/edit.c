/*
 * edit.c - command history, the fc utility, and vi-mode command line
 * editing (User Portability Utilities option).
 */
#include "sh.h"
#include <termios.h>

/* ------------------------------------------------------------------ */
/* history                                                            */
/* ------------------------------------------------------------------ */

typedef struct HEnt { char *text; int num; } HEnt;
static HEnt *hist;
static int nhist, hcap, hnext = 1, hmax = 500;
static int hist_loaded;
static int continuing_entry;

static int hist_limit(void)
{
    const char *s = var_get("HISTSIZE");
    int ok;
    long v = s ? parse_long(s, &ok) : 0;
    if (s && ok && v >= 128)
        return (int)v;
    return hmax;
}

static void hist_push(const char *text)
{
    int lim = hist_limit();
    if (nhist == hcap) {
        hcap = hcap ? hcap * 2 : 128;
        hist = xrealloc(hist, (size_t)hcap * sizeof *hist);
    }
    hist[nhist].text = xstrdup(text);
    hist[nhist].num = hnext++;
    nhist++;
    while (nhist > lim) {
        free(hist[0].text);
        memmove(hist, hist + 1, (size_t)(nhist - 1) * sizeof *hist);
        nhist--;
    }
}

static const char *histfile(void)
{
    const char *f = var_get("HISTFILE");
    static char buf[PATH_MAX];
    if (f && *f)
        return f;
    f = var_get("HOME");
    if (!f)
        return NULL;
    snprintf(buf, sizeof buf, "%s/.sh_history", f);
    return buf;
}

void history_init(void)
{
    const char *f = histfile();
    FILE *fp;
    char line[8192];
    if (hist_loaded || !f)
        return;
    hist_loaded = 1;
    fp = fopen(f, "r");
    if (!fp)
        return;
    while (fgets(line, sizeof line, fp)) {
        size_t n = strlen(line);
        if (n && line[n - 1] == '\n')
            line[--n] = 0;
        if (n)
            hist_push(line);
    }
    fclose(fp);
}

void history_save(void)
{
    const char *f = histfile();
    FILE *fp;
    int i;
    if (!f || !nhist)
        return;
    fp = fopen(f, "w");
    if (!fp)
        return;
    for (i = 0; i < nhist; i++) {
        const char *t = hist[i].text;
        size_t n = strlen(t);
        fputs(t, fp);
        if (!n || t[n - 1] != '\n')
            fputc('\n', fp);
    }
    fclose(fp);
}

int history_next_number(void) { return hnext; }

/* add a line read interactively; continuation lines join the entry */
void history_add(const char *line)
{
    StrBuf b;
    if (!continuing_entry) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n' || !*p)
            return;
        hist_push(line);
        continuing_entry = 1;
        return;
    }
    if (!nhist) {
        hist_push(line);
        return;
    }
    sb_init(&b);
    sb_adds(&b, hist[nhist - 1].text);
    sb_adds(&b, line);
    free(hist[nhist - 1].text);
    hist[nhist - 1].text = sb_take(&b);
}

void history_new_entry(void) { continuing_entry = 0; }

static void hist_remove_last(void)
{
    if (nhist) {
        free(hist[--nhist].text);
        hnext--;
    }
}

static int hist_skip_current;   /* the fc command is the last entry */

/* find an entry: number, -offset, or prefix string; returns index or -1 */
static int hist_find(const char *spec)
{
    int ok;
    long v;
    if (!nhist)
        return -1;
    v = parse_long(spec, &ok);
    if (ok) {
        int i;
        if (v < 0) {
            int idx = nhist - hist_skip_current + (int)v;
            return idx < 0 ? 0 : idx;
        }
        if (spec[0] == '+') v = labs(v);
        for (i = 0; i < nhist; i++)
            if (hist[i].num == v)
                return i;
        if (v < hist[0].num) return 0;
        return nhist - 1;
    }
    {
        int i;
        size_t n = strlen(spec);
        for (i = nhist - 1; i >= 0; i--)
            if (strncmp(hist[i].text, spec, n) == 0)
                return i;
    }
    return -1;
}

static void strip_nl(char *s)
{
    size_t n = strlen(s);
    while (n && s[n - 1] == '\n')
        s[--n] = 0;
}

int bi_fc(int argc, char **argv)
{
    int c, lflag = 0, nflag = 0, rflag = 0, sflag = 0, i;
    const char *editor = NULL;
    int first, last;
    bopt_ind = 1;
    while (bopt_ind < argc && !(argv[bopt_ind][0] == '-' && isdigit((unsigned char)argv[bopt_ind][1])) &&
           (c = bgetopt(argc, argv, "e:lnrs")) != -1) {
        switch (c) {
        case 'e': editor = bopt_arg; break;
        case 'l': lflag = 1; break;
        case 'n': nflag = 1; break;
        case 'r': rflag = 1; break;
        case 's': sflag = 1; break;
        default: return 2;
        }
    }
    /* the fc command itself is not entered into the history */
    if (interactive && nhist && strncmp(hist[nhist - 1].text, "fc", 2) == 0 && !lflag)
        hist_remove_last();
    if (sflag) {
        const char *old = NULL, *new = NULL;
        char *cmd;
        int idx;
        if (bopt_ind < argc && strchr(argv[bopt_ind], '=')) {
            char *eq = strchr(argv[bopt_ind], '=');
            old = ststrndup(argv[bopt_ind], (size_t)(eq - argv[bopt_ind]));
            new = eq + 1;
            bopt_ind++;
        }
        idx = bopt_ind < argc ? hist_find(argv[bopt_ind]) : nhist - 1;
        if (idx < 0) {
            sh_warn("no command found");
            return 1;
        }
        cmd = xstrdup(hist[idx].text);
        if (old && *old) {
            char *p = strstr(cmd, old);
            if (p) {
                StrBuf b;
                sb_init(&b);
                sb_add(&b, cmd, (size_t)(p - cmd));
                sb_adds(&b, new);
                sb_adds(&b, p + strlen(old));
                free(cmd);
                cmd = sb_take(&b);
            }
        }
        outs(&out2, cmd);
        if (!*cmd || cmd[strlen(cmd) - 1] != '\n')
            outc(&out2, '\n');
        outflush(&out2);
        hist_push(cmd);
        eval_string(cmd, 0);
        free(cmd);
        return exitstatus;
    }
    if (!nhist) {
        if (lflag)
            return 0;
        sh_warn("history is empty");
        return 1;
    }
    hist_skip_current = lflag && interactive && nhist &&
                        strncmp(hist[nhist - 1].text, "fc", 2) == 0;
    if (lflag) {
        first = bopt_ind < argc ? hist_find(argv[bopt_ind]) : nhist - hist_skip_current - 16;
        last = bopt_ind + 1 < argc ? hist_find(argv[bopt_ind + 1]) : nhist - 1 - hist_skip_current;
        if (first < 0) first = 0;
    } else {
        first = bopt_ind < argc ? hist_find(argv[bopt_ind]) : nhist - 1;
        last = bopt_ind + 1 < argc ? hist_find(argv[bopt_ind + 1]) : first;
    }
    if (first < 0 || last < 0) {
        sh_warn("history specification out of range");
        return 1;
    }
    if (first > last) {
        int t = first; first = last; last = t;
        rflag = !rflag;
    }
    if (lflag) {
        for (i = 0; i <= last - first; i++) {
            int k = rflag ? last - i : first + i;
            char *t = xstrdup(hist[k].text);
            strip_nl(t);
            if (nflag)
                outfmt(&out1, "\t%s\n", t);
            else
                outfmt(&out1, "%d\t%s\n", hist[k].num, t);
            free(t);
        }
        return 0;
    }
    {
        char tmpl[PATH_MAX];
        const char *td = getenv("TMPDIR");
        int fd;
        FILE *fp;
        StrBuf cmd, content;
        int st;
        snprintf(tmpl, sizeof tmpl, "%s/shfcXXXXXX", td && *td ? td : "/tmp");
        fd = mkstemp(tmpl);
        if (fd < 0) {
            sh_warn("cannot create temporary file: %s", strerror(errno));
            return 1;
        }
        fp = fdopen(fd, "w");
        for (i = 0; i <= last - first; i++) {
            int k = rflag ? last - i : first + i;
            const char *t = hist[k].text;
            fputs(t, fp);
            if (!*t || t[strlen(t) - 1] != '\n')
                fputc('\n', fp);
        }
        fclose(fp);
        if (!editor) {
            editor = var_get("FCEDIT");
            if (!editor || !*editor)
                editor = "ed";
        }
        sb_init(&cmd);
        sb_adds(&cmd, editor);
        sb_addc(&cmd, ' ');
        sb_adds(&cmd, quote_for_reinput(tmpl));
        {
            char *c2 = sb_take(&cmd);
            st = eval_string_status(c2);
            free(c2);
        }
        if (st != 0) {
            unlink(tmpl);
            return st;
        }
        fp = fopen(tmpl, "r");
        unlink(tmpl);
        if (!fp)
            return 1;
        sb_init(&content);
        {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, fp)) > 0)
                sb_add(&content, buf, n);
        }
        fclose(fp);
        if (content.len) {
            char *text = sb_take(&content);
            outs(&out2, text);
            outflush(&out2);
            hist_push(text);
            eval_string(text, 0);
            free(text);
        } else
            sb_free(&content);
        return exitstatus;
    }
}

/* ------------------------------------------------------------------ */
/* plain line reading                                                 */
/* ------------------------------------------------------------------ */

static char *read_plain(const char *prompt)
{
    StrBuf b;
    outs(&out2, prompt);
    outflush(&out2);
    sb_init(&b);
    for (;;) {
        char c;
        ssize_t n = read(0, &c, 1);
        if (n < 0) {
            if (errno == EINTR) {
                if (intpending) {
                    intpending = 0;
                    gotsig = 0;
                    pending_sig[SIGINT] = 0;
                    sb_free(&b);
                    outc(&out2, '\n');
                    outflush(&out2);
                    raise_exc(EX_INT);
                }
                if (notify_pending()) {
                    outs(&out2, prompt);
                    outn(&out2, b.s ? b.s : "", b.len);
                    outflush(&out2);
                }
                if (gotsig)
                    run_pending_traps();
                continue;
            }
            break;
        }
        if (n == 0)
            break;
        sb_addc(&b, c);
        if (c == '\n')
            break;
    }
    if (!b.len) {
        sb_free(&b);
        return NULL;
    }
    return sb_take(&b);
}

/* ------------------------------------------------------------------ */
/* vi-mode editing                                                    */
/* ------------------------------------------------------------------ */

typedef struct Line { char *s; size_t len, cap; } Line;

static Line L;              /* the line being edited */
static size_t cur;          /* cursor (byte index) */
static Line savebuf;        /* the save buffer for y/d/p */
static Line undo_line;
static size_t undo_cur;
static Line orig_line;      /* for U */
static int insert_mode;
static int replace_mode;
static struct termios saved_tio;
static const char *cur_prompt;
static int hidx;            /* history position while browsing (nhist: edit line) */
static Line editline;       /* saved edit line while browsing history */
static int last_find_cmd, last_find_char;
static char last_search[256];
static int last_search_dir;
static StrBuf last_change;  /* key sequence of the last change (for '.') */
static StrBuf recording;
static int is_recording;
static const char *replay;  /* pending key input */
static size_t replay_len, replay_pos;
static int ve_erase = 0x7f, ve_kill = 025, ve_eof = 4;

static void line_set(Line *l, const char *s, size_t n)
{
    if (n + 1 > l->cap) {
        l->cap = n + 64;
        l->s = xrealloc(l->s, l->cap);
    }
    memcpy(l->s, s, n);
    l->len = n;
    l->s[n] = 0;
}

static void line_insert(size_t at, const char *s, size_t n)
{
    if (L.len + n + 1 > L.cap) {
        L.cap = L.len + n + 64;
        L.s = xrealloc(L.s, L.cap);
    }
    memmove(L.s + at + n, L.s + at, L.len - at);
    memcpy(L.s + at, s, n);
    L.len += n;
    L.s[L.len] = 0;
}

static void line_delete(size_t a, size_t b)
{
    if (b > L.len) b = L.len;
    if (a >= b) return;
    memmove(L.s + a, L.s + b, L.len - b);
    L.len -= b - a;
    L.s[L.len] = 0;
}

static void bell(void) { (void)!write(2, "\a", 1); }

static void redraw(void)
{
    StrBuf b;
    const char *pl = strrchr(cur_prompt, '\n');
    size_t i;
    sb_init(&b);
    sb_addc(&b, '\r');
    sb_adds(&b, pl ? pl + 1 : cur_prompt);
    sb_add(&b, L.s ? L.s : "", L.len);
    sb_adds(&b, "\033[K");
    for (i = L.len; i > cur; i--)
        if (!mb_mode_cont(L.s[i - 1]))
            sb_addc(&b, '\b');
    (void)!write(2, b.s, b.len);
    sb_free(&b);
}

int mb_mode_cont(int c)
{
    return MB_CUR_MAX > 1 && ((unsigned char)c & 0xC0) == 0x80;
}

static size_t next_char(size_t i)
{
    if (i >= L.len) return L.len;
    i++;
    while (i < L.len && mb_mode_cont(L.s[i])) i++;
    return i;
}

static size_t prev_char(size_t i)
{
    if (i == 0) return 0;
    i--;
    while (i > 0 && mb_mode_cont(L.s[i])) i--;
    return i;
}

static size_t last_char_pos(void)
{
    return L.len ? prev_char(L.len) : 0;
}

static void save_undo(void)
{
    line_set(&undo_line, L.s ? L.s : "", L.len);
    undo_cur = cur;
}

static int readkey(void)
{
    unsigned char c;
    if (replay && replay_pos < replay_len)
        return (unsigned char)replay[replay_pos++];
    replay = NULL;
    for (;;) {
        ssize_t n = read(0, &c, 1);
        if (n == 1) {
            if (is_recording)
                sb_addc(&recording, c);
            return c;
        }
        if (n < 0 && errno == EINTR) {
            if (intpending)
                return -2;
            if (notify_pending()) {
                outflush(&out2);
                redraw();
            }
            continue;
        }
        return -1;
    }
}

static int is_wordc(int c) { return isalnum(c) || c == '_' || (unsigned char)c >= 0x80; }
static int is_blankc(int c) { return c == ' ' || c == '\t'; }
static int cls(int c, int big)
{
    if (is_blankc(c)) return 0;
    if (big) return 1;
    return is_wordc(c) ? 1 : 2;
}

/* word motions; return new position */
static size_t mv_w(size_t i, int big)
{
    int c0;
    if (i >= L.len) return i;
    c0 = cls((unsigned char)L.s[i], big);
    while (i < L.len && cls((unsigned char)L.s[i], big) == c0 && c0) i++;
    while (i < L.len && is_blankc(L.s[i])) i++;
    return i;
}

static size_t mv_e(size_t i, int big)
{
    int c;
    if (i + 1 >= L.len) return i;
    i++;
    while (i < L.len && is_blankc(L.s[i])) i++;
    if (i >= L.len) return L.len ? L.len - 1 : 0;
    c = cls((unsigned char)L.s[i], big);
    while (i + 1 < L.len && cls((unsigned char)L.s[i + 1], big) == c) i++;
    return i;
}

static size_t mv_b(size_t i, int big)
{
    int c;
    if (i == 0) return 0;
    i--;
    while (i > 0 && is_blankc(L.s[i])) i--;
    c = cls((unsigned char)L.s[i], big);
    while (i > 0 && cls((unsigned char)L.s[i - 1], big) == c && c) i--;
    return i;
}

static int do_find(int cmd, int ch, int count, size_t *pos)
{
    size_t i = cur;
    int fwd = cmd == 'f' || cmd == 't';
    while (count-- > 0) {
        size_t j = i;
        int found = 0;
        if (fwd) {
            size_t start = j + 1;
            if ((cmd == 't') && start < L.len && (unsigned char)L.s[start] == ch && count >= 0)
                start++;
            for (j = start; j < L.len; j++)
                if ((unsigned char)L.s[j] == ch) { found = 1; break; }
        } else {
            size_t start = j;
            if (cmd == 'T' && start > 0 && (unsigned char)L.s[start - 1] == ch)
                start--;
            while (start > 0) {
                start--;
                if ((unsigned char)L.s[start] == ch) { found = 1; j = start; break; }
            }
        }
        if (!found) return 0;
        i = j;
    }
    if (cmd == 't') i--;
    else if (cmd == 'T') i++;
    *pos = i;
    return 1;
}

/* evaluate a motion; returns 0 on failure */
static int motion(int key, int count, size_t *pos, int *inclusive)
{
    size_t i = cur;
    int n;
    *inclusive = 0;
    switch (key) {
    case 'l': case ' ':
        if (cur + 1 >= L.len && L.len) { *pos = L.len; return cur < L.len; }
        for (n = 0; n < count && i < L.len; n++) i = next_char(i);
        *pos = i; return 1;
    case 'h':
        if (cur == 0) return 0;
        for (n = 0; n < count && i > 0; n++) i = prev_char(i);
        *pos = i; return 1;
    case 'w': case 'W':
        for (n = 0; n < count; n++) i = mv_w(i, key == 'W');
        *pos = i; return 1;
    case 'e': case 'E':
        for (n = 0; n < count; n++) i = mv_e(i, key == 'E');
        *pos = i; *inclusive = 1; return 1;
    case 'b': case 'B':
        if (cur == 0) return 0;
        for (n = 0; n < count; n++) i = mv_b(i, key == 'B');
        *pos = i; return 1;
    case '^':
        i = 0;
        while (i < L.len && is_blankc(L.s[i])) i++;
        *pos = i; return 1;
    case '$':
        *pos = L.len ? L.len - 1 : 0; *inclusive = 1; return 1;
    case '0':
        *pos = 0; return 1;
    case '|':
        *pos = (size_t)(count - 1) < L.len ? (size_t)(count - 1) : (L.len ? L.len - 1 : 0);
        return 1;
    case 'f': case 'F': case 't': case 'T': {
        int ch = readkey();
        if (ch < 0) return 0;
        last_find_cmd = key;
        last_find_char = ch;
        if (!do_find(key, ch, count, pos)) return 0;
        *inclusive = key == 'f' || key == 't';
        return 1;
    }
    case ';': case ',': {
        int cmd = last_find_cmd;
        if (!cmd) return 0;
        if (key == ',')
            cmd = cmd == 'f' ? 'F' : cmd == 'F' ? 'f' : cmd == 't' ? 'T' : 't';
        if (!do_find(cmd, last_find_char, count, pos)) return 0;
        *inclusive = cmd == 'f' || cmd == 't';
        return 1;
    }
    }
    return 0;
}

static void yank(size_t a, size_t b)
{
    if (b > L.len) b = L.len;
    if (a > b) { size_t t = a; a = b; b = t; }
    line_set(&savebuf, L.s + a, b - a);
}

static void load_hist(int idx)
{
    if (hidx == nhist && idx != nhist)
        line_set(&editline, L.s ? L.s : "", L.len);
    hidx = idx;
    if (idx == nhist)
        line_set(&L, editline.s ? editline.s : "", editline.len);
    else {
        char *t = xstrdup(hist[idx].text);
        strip_nl(t);
        line_set(&L, t, strlen(t));
        free(t);
    }
    cur = 0;
    line_set(&orig_line, L.s, L.len);
}

static int hist_search(const char *pat, int dir)
{
    int i;
    StrBuf p;
    int anchored = pat[0] == '^';
    sb_init(&p);
    if (!anchored) sb_addc(&p, '*');
    sb_adds(&p, anchored ? pat + 1 : pat);
    sb_addc(&p, '*');
    for (i = dir < 0 ? hidx - 1 : hidx + 1; i >= 0 && i < nhist; i += dir) {
        char *t = xstrdup(hist[i].text);
        strip_nl(t);
        if (pattern_match(p.s, t)) {
            free(t);
            sb_free(&p);
            load_hist(i);
            return 1;
        }
        free(t);
    }
    sb_free(&p);
    return 0;
}

static char *bigword_bounds(size_t *a, size_t *b)
{
    size_t s = cur, e;
    if (s >= L.len && s > 0) s = L.len - 1;
    while (s > 0 && !is_blankc(L.s[s - 1])) s--;
    e = s;
    while (e < L.len && !is_blankc(L.s[e])) e++;
    *a = s;
    *b = e;
    return ststrndup(L.s + s, e - s);
}

static void glob_word(ArgList *res, const char *word)
{
    StrBuf pat;
    Word w;
    sb_init(&pat);
    sb_adds(&pat, word);
    if (!has_glob_chars(word))
        sb_addc(&pat, '*');
    w.s = sb_stack(&pat);
    w.subs = NULL;
    w.nsubs = 0;
    w.quoted = 0;
    w.next = NULL;
    expand_word_full(&w, res);
    if (res->n == 1 && strcmp(res->v[0], w.s) == 0)
        res->n = 0;
}

static void vi_expand(int key)
{
    size_t a, b;
    char *word;
    ArgList res = { NULL, 0, 0 };
    StackMark m;
    int i;
    setstackmark(&m);
    word = bigword_bounds(&a, &b);
    glob_word(&res, word);
    if (key == '=') {
        (void)!write(2, "\r\n", 2);
        for (i = 0; i < res.n; i++) {
            struct stat st;
            outfmt(&out2, "%s%s\n", res.v[i],
                   stat(res.v[i], &st) == 0 && S_ISDIR(st.st_mode) ? "/" : "");
        }
        outflush(&out2);
    } else if (res.n > 0) {
        save_undo();
        line_delete(a, b);
        if (key == '*') {
            StrBuf t;
            sb_init(&t);
            for (i = 0; i < res.n; i++) {
                if (i) sb_addc(&t, ' ');
                sb_adds(&t, res.v[i]);
            }
            line_insert(a, t.s, t.len);
            cur = a + t.len;
            sb_free(&t);
            if (cur < L.len) { line_insert(cur, " ", 1); cur++; }
        } else {
            /* longest common prefix */
            size_t k = strlen(res.v[0]);
            struct stat st;
            for (i = 1; i < res.n; i++) {
                size_t j = 0;
                while (j < k && res.v[i][j] == res.v[0][j]) j++;
                k = j;
            }
            line_insert(a, res.v[0], k);
            cur = a + k;
            if (res.n == 1) {
                if (stat(res.v[0], &st) == 0 && S_ISDIR(st.st_mode)) {
                    line_insert(cur, "/", 1);
                    cur++;
                } else {
                    line_insert(cur, " ", 1);
                    cur++;
                }
            }
        }
        insert_mode = 1;
    } else
        bell();
    free(res.v);
    popstackmark(&m);
}

/* the editor proper: returns heap line with '\n', or NULL at EOF */
static char *vi_read(const char *prompt)
{
    struct termios t;
    int done = 0, eof = 0;
    char *result;
    cur_prompt = prompt;
    if (tcgetattr(0, &saved_tio) < 0)
        return read_plain(prompt);
    t = saved_tio;
    ve_erase = saved_tio.c_cc[VERASE];
    ve_kill = saved_tio.c_cc[VKILL];
    ve_eof = saved_tio.c_cc[VEOF];
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSADRAIN, &t);
    line_set(&L, "", 0);
    line_set(&orig_line, "", 0);
    cur = 0;
    insert_mode = 1;
    replace_mode = 0;
    hidx = nhist;
    outs(&out2, prompt);
    outflush(&out2);
    while (!done) {
        int key = readkey(), count = 0;
        if (key == -1) {
            if (L.len == 0) { eof = 1; break; }
            done = 1;
            break;
        }
        if (key == -2) {
            intpending = 0;
            gotsig = 0;
            pending_sig[SIGINT] = 0;
            tcsetattr(0, TCSADRAIN, &saved_tio);
            (void)!write(2, "\n", 1);
            raise_exc(EX_INT);
        }
        if (insert_mode) {
            if (key == '\r' || key == '\n') { done = 1; break; }
            if (key == 033) {
                insert_mode = 0;
                replace_mode = 0;
                if (is_recording) {
                    is_recording = 0;
                    sb_free(&last_change);
                    last_change = recording;
                    sb_init(&recording);
                }
                if (cur > 0) cur = prev_char(cur);
                redraw();
                continue;
            }
            if (key == ve_eof && L.len == 0) { eof = 1; break; }
            if (key == ve_erase || key == 0x7f || key == 010) {
                if (cur > 0) {
                    size_t p = prev_char(cur);
                    line_delete(p, cur);
                    cur = p;
                }
                redraw();
                continue;
            }
            if (key == ve_kill) {
                line_set(&L, "", 0);
                cur = 0;
                redraw();
                continue;
            }
            if (key == 027) {   /* ^W */
                size_t p = cur;
                while (p > 0 && (is_blankc(L.s[p - 1]) || ispunct((unsigned char)L.s[p - 1]))) p--;
                while (p > 0 && !is_blankc(L.s[p - 1]) && !ispunct((unsigned char)L.s[p - 1])) p--;
                line_delete(p, cur);
                cur = p;
                redraw();
                continue;
            }
            if (key == 026) {   /* ^V */
                key = readkey();
                if (key < 0) continue;
            }
            {
                char ch = (char)key;
                if (replace_mode && cur < L.len)
                    line_delete(cur, next_char(cur));
                line_insert(cur, &ch, 1);
                cur++;
            }
            redraw();
            continue;
        }

        /* command mode */
        if (key >= '1' && key <= '9') {
            while (key >= '0' && key <= '9') {
                count = count * 10 + (key - '0');
                key = readkey();
            }
        }
        {
            int cnt = count ? count : 1;
            size_t pos;
            int incl;
            int startrec = 0;
            switch (key) {
            case '\r': case '\n':
                done = 1;
                break;
            case 014:   /* ^L */
                (void)!write(2, "\n", 1);
                redraw();
                break;
            case '#':
                line_insert(0, "#", 1);
                done = 1;
                break;
            case '=': case '\\': case '*':
                vi_expand(key);
                if (key == '=') outs(&out2, ""), redraw();
                redraw();
                break;
            case '@': {
                int ch = readkey();
                char nm[3] = { '_', (char)ch, 0 };
                Alias *a = alias_lookup(nm);
                if (a) {
                    replay = a->val;
                    replay_len = strlen(a->val);
                    replay_pos = 0;
                }
                break;
            }
            case '~': {
                int n;
                save_undo();
                for (n = 0; n < cnt && cur < L.len; n++) {
                    unsigned char c = (unsigned char)L.s[cur];
                    L.s[cur] = (char)(islower(c) ? toupper(c) : isupper(c) ? tolower(c) : c);
                    if (cur + 1 < L.len) cur++;
                    else break;
                }
                redraw();
                break;
            }
            case '.':
                if (last_change.len) {
                    replay = ststrndup(last_change.s, last_change.len);
                    replay_len = last_change.len;
                    replay_pos = 0;
                }
                break;
            case 'v': {
                StrBuf cmd;
                tcsetattr(0, TCSADRAIN, &saved_tio);
                (void)!write(2, "\n", 1);
                sb_init(&cmd);
                if (count)
                    sb_printf(&cmd, "fc -e \"${VISUAL:-${EDITOR:-vi}}\" %d", count);
                else {
                    hist_push(L.s ? L.s : "");
                    sb_adds(&cmd, "fc -e \"${VISUAL:-${EDITOR:-vi}}\"");
                }
                {
                    char *c2 = sb_take(&cmd);
                    eval_string(c2, 0);
                    free(c2);
                }
                result = xstrdup("\n");
                return result;
            }
            case 'k': case '-':
                if (hidx - cnt < 0) { bell(); break; }
                load_hist(hidx - cnt);
                redraw();
                break;
            case 'j': case '+':
                if (hidx + cnt > nhist) { bell(); break; }
                load_hist(hidx + cnt);
                redraw();
                break;
            case 'G':
                if (!nhist) { bell(); break; }
                if (count) {
                    int i, f = -1;
                    for (i = 0; i < nhist; i++) if (hist[i].num == count) f = i;
                    if (f < 0) { bell(); break; }
                    load_hist(f);
                } else
                    load_hist(0);
                redraw();
                break;
            case '/': case '?': {
                StrBuf p;
                int ch;
                sb_init(&p);
                (void)!write(2, "\r\033[K", 4);
                (void)!write(2, key == '/' ? "/" : "?", 1);
                while ((ch = readkey()) >= 0 && ch != '\r' && ch != '\n') {
                    if (ch == ve_erase || ch == 0x7f || ch == 010) {
                        if (p.len) { p.len--; p.s[p.len] = 0; (void)!write(2, "\b \b", 3); }
                        continue;
                    }
                    sb_addc(&p, ch);
                    {
                        char c = (char)ch;
                        (void)!write(2, &c, 1);
                    }
                }
                if (p.len) {
                    snprintf(last_search, sizeof last_search, "%s", p.s);
                    last_search_dir = key == '/' ? -1 : 1;
                }
                sb_free(&p);
                if (!last_search[0] || !hist_search(last_search, last_search_dir))
                    bell();
                redraw();
                break;
            }
            case 'n': case 'N':
                if (!last_search[0] ||
                    !hist_search(last_search, key == 'n' ? last_search_dir : -last_search_dir))
                    bell();
                redraw();
                break;
            case 'a':
                if (L.len) cur = next_char(cur);
                insert_mode = 1;
                startrec = 1;
                break;
            case 'A':
                cur = L.len;
                insert_mode = 1;
                startrec = 1;
                break;
            case 'i':
                insert_mode = 1;
                startrec = 1;
                break;
            case 'I':
                cur = 0;
                insert_mode = 1;
                startrec = 1;
                break;
            case 'R':
                save_undo();
                insert_mode = replace_mode = 1;
                startrec = 1;
                break;
            case 'S':
                save_undo();
                yank(0, L.len);
                line_set(&L, "", 0);
                cur = 0;
                insert_mode = 1;
                startrec = 1;
                break;
            case 'C':
                save_undo();
                yank(cur, L.len);
                line_delete(cur, L.len);
                insert_mode = 1;
                startrec = 1;
                break;
            case 'D':
                save_undo();
                yank(cur, L.len);
                line_delete(cur, L.len);
                if (cur > 0 && cur >= L.len) cur = prev_char(L.len);
                startrec = 1;
                break;
            case 'x': case 'X': {
                size_t a = cur, b = cur;
                int n;
                save_undo();
                if (key == 'x') {
                    for (n = 0; n < cnt && b < L.len; n++) b = next_char(b);
                } else {
                    for (n = 0; n < cnt && a > 0; n++) a = prev_char(a);
                }
                if (a == b) { bell(); break; }
                yank(a, b);
                line_delete(a, b);
                cur = a;
                if (cur >= L.len && L.len) cur = prev_char(L.len);
                startrec = 1;
                break;
            }
            case 'r': {
                int ch = readkey(), n;
                if (ch < 0 || cur + (size_t)cnt > L.len) { bell(); break; }
                save_undo();
                for (n = 0; n < cnt; n++) L.s[cur + (size_t)n] = (char)ch;
                cur += (size_t)cnt - 1;
                startrec = 1;
                break;
            }
            case 'p': case 'P': {
                int n;
                if (!savebuf.len) { bell(); break; }
                save_undo();
                if (key == 'p' && L.len) cur = next_char(cur);
                for (n = 0; n < cnt; n++) {
                    line_insert(cur, savebuf.s, savebuf.len);
                    cur += savebuf.len;
                }
                if (cur > 0) cur = prev_char(cur);
                startrec = 1;
                break;
            }
            case 'u': {
                Line tmp = L;
                size_t tc = cur;
                L = undo_line;
                cur = undo_cur;
                undo_line = tmp;
                undo_cur = tc;
                if (cur > L.len) cur = L.len;
                break;
            }
            case 'U':
                save_undo();
                line_set(&L, orig_line.s ? orig_line.s : "", orig_line.len);
                cur = 0;
                break;
            case 'Y':
                yank(cur, L.len);
                break;
            case 'c': case 'd': case 'y': {
                int m = readkey(), mc = 0;
                if (m >= '1' && m <= '9') {
                    while (m >= '0' && m <= '9') { mc = mc * 10 + (m - '0'); m = readkey(); }
                }
                if (mc) cnt = cnt * mc;
                if (m == key) {
                    /* cc, dd, yy: whole line */
                    if (key != 'y') save_undo();
                    yank(0, L.len);
                    if (key != 'y') {
                        line_set(&L, "", 0);
                        cur = 0;
                    }
                    if (key == 'c') insert_mode = 1;
                    startrec = key != 'y';
                    break;
                }
                if (!motion(m, (m == '0' || m == '^' || m == '$') ? 1 : cnt, &pos, &incl)) {
                    bell();
                    break;
                }
                {
                    size_t a = cur, b = pos;
                    if (b < a) { size_t t = a; a = b; b = t; }
                    else if (incl || (m == 'w' || m == 'W') ? (incl ? 1 : 0) : 0)
                        b = next_char(b);
                    if ((key == 'c') && (m == 'w' || m == 'W')) {
                        /* cw behaves like ce */
                        size_t e = cur;
                        int n;
                        if (cur < L.len && !is_blankc(L.s[cur])) {
                            for (n = 0; n < cnt; n++) {
                                if (n == 0) {
                                    int c0 = cls((unsigned char)L.s[e], m == 'W');
                                    while (e + 1 < L.len && cls((unsigned char)L.s[e + 1], m == 'W') == c0) e++;
                                } else
                                    e = mv_e(e, m == 'W');
                            }
                            b = next_char(e);
                        }
                    }
                    if (b > L.len) b = L.len;
                    yank(a, b);
                    if (key != 'y') {
                        save_undo();
                        line_delete(a, b);
                    }
                    cur = a;
                    if (key == 'c')
                        insert_mode = 1;
                    else if (cur >= L.len && L.len)
                        cur = prev_char(L.len);
                    startrec = key != 'y';
                }
                break;
            }
            default:
                if (motion(key, cnt, &pos, &incl)) {
                    cur = pos;
                    if (cur >= L.len && L.len) cur = last_char_pos();
                } else
                    bell();
            }
            if (startrec && !replay) {
                /* record this change for '.' */
                sb_free(&recording);
                sb_init(&recording);
                if (count) sb_printf(&recording, "%d", count);
                sb_addc(&recording, key);
                if (insert_mode)
                    is_recording = 1;
                else {
                    sb_free(&last_change);
                    last_change = recording;
                    sb_init(&recording);
                }
            }
            if (!done)
                redraw();
        }
    }
    tcsetattr(0, TCSADRAIN, &saved_tio);
    (void)!write(2, "\r\n", 2);
    if (eof)
        return NULL;
    {
        StrBuf b;
        sb_init(&b);
        sb_add(&b, L.s ? L.s : "", L.len);
        sb_addc(&b, '\n');
        return sb_take(&b);
    }
}

char *read_line_interactive(const char *prompt)
{
    char *line;
    if (opts[O_VI] && isatty(0))
        line = vi_read(prompt);
    else
        line = read_plain(prompt);
    if (line && interactive)
        history_add(line);
    return line;
}

void edit_reset_mode(void) { insert_mode = 1; }
