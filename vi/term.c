/*
 * term.c - terminal modes and output, keyboard input with mapping
 * (map, map!) and the abbreviation table.
 */
#include "ex.h"
#include <poll.h>
#include <sys/ioctl.h>

int rows = 24, cols = 80;
int term_raw;
int tty_fd = 0;
static struct termios orig_tio;
static int have_orig;
struct termios cur_tio;

void term_get_size(void)
{
    struct winsize ws;
    const char *e;
    int r = 0, c = 0;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 || ioctl(0, TIOCGWINSZ, &ws) == 0) {
        r = ws.ws_row;
        c = ws.ws_col;
    }
    if ((e = getenv("LINES")) && atoi(e) > 0)
        r = atoi(e);
    if ((e = getenv("COLUMNS")) && atoi(e) > 0)
        c = atoi(e);
    if (r <= 1)
        r = 24;
    if (c <= 1)
        c = 80;
    rows = r;
    cols = c;
}

int term_capable_visual(void)
{
    const char *t = OS(OP_TERM);
    if (!isatty(1) || !isatty(0))
        return 0;
    if (!t || !*t || strcmp(t, "dumb") == 0 || strcmp(t, "unknown") == 0)
        return 0;
    return 1;
}

int term_capable_standout(void)
{
    return isatty(1) && term_capable_visual();
}

void term_init(void)
{
    if (!have_orig && tcgetattr(0, &orig_tio) == 0)
        have_orig = 1;
}

int erase_char(void) { return have_orig ? orig_tio.c_cc[VERASE] : 0x7f; }
int kill_char(void) { return have_orig ? orig_tio.c_cc[VKILL] : 0x15; }
int werase_char(void) { return have_orig ? orig_tio.c_cc[VWERASE] : 0x17; }
int eof_char(void) { return have_orig ? orig_tio.c_cc[VEOF] : 0x04; }
int susp_char(void) { return have_orig ? orig_tio.c_cc[VSUSP] : 0x1a; }

void term_raw_on(void)
{
    struct termios t;
    term_init();
    if (!have_orig)
        return;
    t = orig_tio;
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ECHONL | IEXTEN);
    t.c_iflag &= ~(tcflag_t)(ICRNL | INLCR | IGNCR);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
#ifdef VDSUSP
    t.c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
#ifdef VLNEXT
    t.c_cc[VLNEXT] = _POSIX_VDISABLE;
#endif
    tcsetattr(0, TCSADRAIN, &t);
    cur_tio = t;
    term_raw = 1;
}

void term_raw_off(void)
{
    tflush();
    if (have_orig)
        tcsetattr(0, TCSADRAIN, &orig_tio);
    term_raw = 0;
}

/* ---------- output ---------- */

static Str tbuf;

void tputs_(const char *s) { str_adds(&tbuf, s); }

void tputn(const char *s, size_t n) { str_addn(&tbuf, s, n); }

void tflush(void)
{
    size_t off = 0;
    out_flush();
    while (off < tbuf.len) {
        ssize_t n = write(1, tbuf.s + off, tbuf.len - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        off += (size_t)n;
    }
    str_clear(&tbuf);
}

void tmove(int r, int c)
{
    char b[32];
    snprintf(b, sizeof b, "\033[%d;%dH", r + 1, c + 1);
    tputs_(b);
}

void tclear_eol(void) { tputs_("\033[K"); }
void tclear(void) { tputs_("\033[H\033[2J"); }
void tstandout(int on) { tputs_(on ? "\033[7m" : "\033[m"); }

/* ---------- keyboard ---------- */

#define KF_NOREMAP 0x200000
#define KF_FROMBUF 0x400000
#define KMASK 0xfffff

static int *kq;
static int kqn, kqcap;

void key_push(const char *s, int n, int flags, int front)
{
    if (kqn + n > kqcap) {
        kqcap = (kqn + n) * 2 + 16;
        kq = xrealloc(kq, (size_t)kqcap * sizeof(int));
    }
    if (front) {
        memmove(kq + n, kq, (size_t)kqn * sizeof(int));
        for (int i = 0; i < n; i++)
            kq[i] = (unsigned char)s[i] | flags;
    } else {
        for (int i = 0; i < n; i++)
            kq[kqn + i] = (unsigned char)s[i] | flags;
    }
    kqn += n;
}

static void key_push_int(int k, int front)
{
    if (kqn + 1 > kqcap) {
        kqcap = kqn * 2 + 16;
        kq = xrealloc(kq, (size_t)kqcap * sizeof(int));
    }
    if (front) {
        memmove(kq + 1, kq, (size_t)kqn * sizeof(int));
        kq[0] = k;
    } else
        kq[kqn] = k;
    kqn++;
}

int key_pending(void) { return kqn; }

int key_pending_mapped(void)
{
    for (int i = 0; i < kqn; i++)
        if (kq[i] >= 0 && (kq[i] & (KF_MAPPED | KF_FROMBUF)))
            return 1;
    return 0;
}

/* discard keys produced by maps and buffer executions (after an error) */
void key_flush_mapped(void)
{
    int j = 0;
    for (int i = 0; i < kqn; i++)
        if (!(kq[i] >= 0 && (kq[i] & (KF_MAPPED | KF_FROMBUF))))
            kq[j++] = kq[i];
    kqn = j;
}

static int tty_ready(int ms)
{
    struct pollfd p;
    p.fd = 0;
    p.events = POLLIN;
    return poll(&p, 1, ms) > 0;
}

int key_peek_timeout(int ms)
{
    struct pollfd p;
    if (kqn)
        return 1;
    p.fd = 0;
    p.events = POLLIN;
    return poll(&p, 1, ms) > 0;
}

/* read one byte from the terminal */
int key_raw(void)
{
    unsigned char c;
    for (;;) {
        ssize_t n;
        tflush();
        if (got_hup)
            return K_EOF;
        n = read(0, &c, 1);
        if (n == 1)
            return c;
        if (n == 0)
            return K_EOF;
        if (errno == EINTR) {
            if (got_hup)
                return K_EOF;
            if (intr) {
                intr = 0;
                return K_INTR;
            }
            if (got_winch || got_cont)
                return K_RESIZE;
            continue;
        }
        return K_EOF;
    }
}

Map *maps;
Map *abbrevs;
static int map_depth;

static Map *map_find_prefix(int mode_input, int *exact, int *partial)
{
    Map *best = NULL;
    *exact = 0;
    *partial = 0;
    for (Map *m = maps; m; m = m->next) {
        int l = (int)strlen(m->lhs), i;
        if (m->input != mode_input)
            continue;
        for (i = 0; i < l && i < kqn; i++) {
            int k = kq[i];
            if (k < 0 || (k & KF_NOREMAP) || (char)(k & 0xff) != m->lhs[i] || (k & KMASK) > 0xff)
                break;
        }
        if (i == l) {
            if (!best || (int)strlen(best->lhs) < l)
                best = m;
            *exact = 1;
        } else if (i == kqn && i < l)
            *partial = 1;
    }
    return best;
}

/*
 * Get a key.  map_mode: 0 no mapping, 1 command mode maps, 2 text input
 * maps.  *flags receives KF_MAPPED when the key came from a map or buffer.
 */
int key_get(int map_mode, int *flags)
{
    for (;;) {
        int k;
        if (kqn == 0) {
            k = key_raw();
            if (k < 0)
                return k;
            key_push_int(k, 0);
            map_depth = 0;
        }
        /* cursor keys typed on the terminal */
        if (kq[0] == 033 && kqn == 1 && tty_ready(25)) {
            int k2 = key_raw();
            if (k2 >= 0)
                key_push_int(k2, 0);
            if ((k2 == '[' || k2 == 'O') && tty_ready(25)) {
                int k3 = key_raw();
                if (k3 >= 0)
                    key_push_int(k3, 0);
            }
        }
        if (map_mode && maps) {
            int exact, partial;
            Map *m = map_find_prefix(map_mode == 2, &exact, &partial);
            while (partial && !(kq[kqn - 1] & (KF_MAPPED | KF_FROMBUF)) && tty_ready(1000)) {
                int k2 = key_raw();
                if (k2 < 0)
                    break;
                key_push_int(k2, 0);
                m = map_find_prefix(map_mode == 2, &exact, &partial);
            }
            if (m) {
                int l = (int)strlen(m->lhs), rl = (int)strlen(m->rhs), nore = OV(OP_REMAP) ? 0 : KF_NOREMAP;
                if (++map_depth > 1000) {
                    kqn = 0;
                    map_depth = 0;
                    vs_message("Map loop", 1);
                    return K_INTR;
                }
                memmove(kq, kq + l, (size_t)(kqn - l) * sizeof(int));
                kqn -= l;
                key_push(m->rhs, rl, KF_MAPPED | nore, 1);
                if (strncmp(m->rhs, m->lhs, (size_t)l) == 0)
                    for (int i = 0; i < l && i < rl; i++)
                        kq[i] |= KF_NOREMAP;
                continue;
            }
        }
        if (kq[0] == 033 && kqn >= 3 && (kq[1] == '[' || kq[1] == 'O') && !(kq[0] & KF_MAPPED)) {
            int c3 = kq[2] & 0xff, mv = 0;
            switch (c3) {
            case 'A': mv = 'k'; break;
            case 'B': mv = 'j'; break;
            case 'C': mv = 'l'; break;
            case 'D': mv = 'h'; break;
            }
            if (mv) {
                memmove(kq, kq + 3, (size_t)(kqn - 3) * sizeof(int));
                kqn -= 3;
                if (map_mode == 2) {
                    key_push_int(mv | KF_NOREMAP, 1);
                    key_push_int(033 | KF_NOREMAP, 1);
                } else
                    key_push_int(mv | KF_NOREMAP, 1);
            }
        }
        k = kq[0];
        memmove(kq, kq + 1, (size_t)(kqn - 1) * sizeof(int));
        kqn--;
        if (flags)
            *flags = k & (KF_MAPPED | KF_FROMBUF);
        return k & KMASK;
    }
}

void key_push_buffer(const char *s, int n)
{
    key_push(s, n, KF_FROMBUF | KF_NOREMAP * 0, 1);
}

/* ---------- map and abbreviate ---------- */

static void show_ctl(Str *b, const char *s)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7f) {
            str_addc(b, '^');
            str_addc(b, c == 0x7f ? '?' : (char)(c + '@'));
        } else
            str_addc(b, (char)c);
    }
}

/* split "lhs rhs" with ^V escapes */
static void split_lhs_rhs(const char *arg, char **lhs, char **rhs)
{
    Str l, r;
    const char *p = skipbl(arg);
    str_init(&l);
    str_init(&r);
    while (*p && *p != ' ' && *p != '\t') {
        if (*p == '\026' && p[1]) {
            p++;
            str_addc(&l, *p++);
            continue;
        }
        str_addc(&l, *p++);
    }
    p = skipbl(p);
    while (*p) {
        if (*p == '\026' && p[1]) {
            p++;
            str_addc(&r, *p++);
            continue;
        }
        str_addc(&r, *p++);
    }
    *lhs = str_detach(&l);
    *rhs = str_detach(&r);
}

static const char *fkey_seq(const char *name)
{
    static const char *seq[] = { NULL,          "\033OP",     "\033OQ",     "\033OR",
                                 "\033OS",      "\033[15~",   "\033[17~",   "\033[18~",
                                 "\033[19~",    "\033[20~",   "\033[21~",   "\033[23~",
                                 "\033[24~" };
    int n;
    if (name[0] != '#' || !isdigit((unsigned char)name[1]))
        return NULL;
    n = atoi(name + 1);
    if (n >= 1 && n <= 12)
        return seq[n];
    return NULL;
}

void cmd_map(const char *arg, int bang, int unmap)
{
    char *lhs, *rhs;
    const char *fk;
    Map **pp;
    split_lhs_rhs(arg, &lhs, &rhs);
    if (!*lhs) {
        free(lhs);
        free(rhs);
        if (unmap)
            ex_err("Usage: unmap[!] lhs");
        for (Map *m = maps; m; m = m->next) {
            Str b;
            if (m->input != bang)
                continue;
            str_init(&b);
            show_ctl(&b, m->lhs);
            str_addc(&b, '\t');
            show_ctl(&b, m->rhs);
            str_addc(&b, '\n');
            out_write(b.s, b.len);
            str_free(&b);
        }
        return;
    }
    if ((fk = fkey_seq(lhs))) {
        free(lhs);
        lhs = xstrdup(fk);
    }
    for (pp = &maps; *pp; pp = &(*pp)->next)
        if ((*pp)->input == bang && strcmp((*pp)->lhs, lhs) == 0)
            break;
    if (unmap) {
        if (!*pp) {
            free(lhs);
            free(rhs);
            ex_err("\"%s\" is not mapped", arg);
        }
        {
            Map *m = *pp;
            *pp = m->next;
            free(m->lhs);
            free(m->rhs);
            free(m);
        }
        free(lhs);
        free(rhs);
        return;
    }
    if (!*rhs) {
        free(lhs);
        free(rhs);
        ex_err("Usage: map[!] lhs rhs");
    }
    if (*pp) {
        free((*pp)->rhs);
        (*pp)->rhs = rhs;
        free(lhs);
        return;
    }
    {
        Map *m = xcalloc(1, sizeof *m);
        m->lhs = lhs;
        m->rhs = rhs;
        m->input = bang;
        m->next = maps;
        maps = m;
    }
}

void cmd_abbrev(const char *arg, int unab)
{
    char *lhs, *rhs;
    Map **pp;
    split_lhs_rhs(arg, &lhs, &rhs);
    if (!*lhs) {
        free(lhs);
        free(rhs);
        if (unab)
            ex_err("Usage: unabbreviate lhs");
        for (Map *m = abbrevs; m; m = m->next) {
            Str b;
            str_init(&b);
            show_ctl(&b, m->lhs);
            str_addc(&b, '\t');
            show_ctl(&b, m->rhs);
            str_addc(&b, '\n');
            out_write(b.s, b.len);
            str_free(&b);
        }
        return;
    }
    for (pp = &abbrevs; *pp; pp = &(*pp)->next)
        if (strcmp((*pp)->lhs, lhs) == 0)
            break;
    if (unab) {
        if (!*pp) {
            free(lhs);
            free(rhs);
            ex_err("\"%s\" is not an abbreviation", arg);
        }
        {
            Map *m = *pp;
            *pp = m->next;
            free(m->lhs);
            free(m->rhs);
            free(m);
        }
        free(lhs);
        free(rhs);
        return;
    }
    if (!*rhs) {
        free(lhs);
        free(rhs);
        ex_err("Usage: abbreviate lhs rhs");
    }
    if (*pp) {
        free((*pp)->rhs);
        (*pp)->rhs = rhs;
        free(lhs);
        return;
    }
    {
        Map *m = xcalloc(1, sizeof *m);
        m->lhs = lhs;
        m->rhs = rhs;
        m->next = abbrevs;
        abbrevs = m;
    }
}

/* ---------- suspension (SIGTSTP) in visual mode ---------- */

static void on_tstp(int sig)
{
    struct sigaction sa;
    sigset_t set;
    int saved = errno;
    (void)sig;
    if (have_orig)
        tcsetattr(0, TCSADRAIN, &orig_tio);
    if (write(1, "\033[999;1H\r\n", 10) < 0) {
        /* ignore */
    }
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigaction(SIGTSTP, &sa, NULL);
    sigemptyset(&set);
    sigaddset(&set, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    kill(getpid(), SIGTSTP);
    /* continued */
    sa.sa_handler = on_tstp;
    sigaction(SIGTSTP, &sa, NULL);
    if (have_orig)
        tcsetattr(0, TCSADRAIN, &cur_tio);
    got_cont = 1;
    errno = saved;
}

static struct sigaction old_tstp;

void term_install_tstp(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_tstp;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTSTP, &sa, &old_tstp);
}

void term_remove_tstp(void)
{
    sigaction(SIGTSTP, &old_tstp, NULL);
}
