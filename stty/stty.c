/*
 * stty.c - stty: set the options for a terminal (POSIX.1-2024, XCU stty)
 *
 * Synopsis:
 *   stty [-a|-g]
 *   stty operand...
 *
 * The terminal is the standard input (tcgetattr(), tcsetattr()).  All the
 * operands of the standard: the control, input, output (XSI) and local
 * modes, the control characters, min and time, the speeds, the
 * combination modes, the saved settings of -g, rows, cols and size.  The
 * operands are applied in order to a copy of the settings, which is then
 * set at once (TCSADRAIN); the result is read back and compared.
 *
 * Choices where the standard leaves room:
 *  - Without operands: the speed, the control characters that differ from
 *    those of sane, and the modes that differ from sane.  -a: the speed and
 *    window size, all the control characters, min and time, and all the
 *    modes of the standard, one line a group.
 *  - -g: "stty1:" then the input, output, control and local flags, the two
 *    speeds and each element of c_cc, in hexadecimal, separated by ':'.
 *    The window size is not included.
 *  - A control character is written as itself if printable, ^X for a
 *    control character, ^? for DEL, M- before one with the high bit set,
 *    and <undef> when disabled.  "undef" and "^-" disable it; a number is
 *    not accepted.
 *  - raw sets what the standard says it is equivalent to: cs8, erase,
 *    kill, intr, quit, eof and eol disabled, -opost and -inpck.  It does
 *    not turn off icanon or isig, as historical raw modes did; use
 *    "-icanon -isig min 1 time 0" for that.  -raw and cooked set erase,
 *    kill, intr, quit and eof back to those of sane, eol undefined, and
 *    opost.
 *  - sane: cread, -ignbrk brkint -inlcr -igncr icrnl -ixoff -ixany,
 *    opost onlcr -ocrnl -onocr -onlret -ofill -ofdel nl0 cr0 tab0 bs0 vt0
 *    ff0, isig icanon iexten echo echoe echok -echonl -noflsh -tostop, and
 *    the control characters intr ^C, quit ^\, erase ^?, kill ^U, eof ^D,
 *    eol undefined, start ^Q, stop ^S, susp ^Z, min 1, time 0.  The
 *    character size, parity and speed are left as they are.  ek sets erase
 *    ^? and kill ^U.
 *  - Speeds: the values of <termios.h> (0 to 38400, and the higher ones the
 *    system defines).
 *  - The window size is read and set with the TIOCGWINSZ and TIOCSWINSZ
 *    requests of ioctl(), as the system has no tcgetwinsize() yet.
 *  - An unknown operand, a missing or bad value: a diagnostic, status 1,
 *    and nothing is set.  When the terminal does not take all the input,
 *    output and local modes and control characters, a diagnostic and
 *    status 1; the control modes and speeds are not checked (a
 *    pseudo-terminal, for one, keeps cs8 and has no parity).  Not a
 *    terminal: status 1.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

enum field { CF, IF, OF, LF };

/* a mode: a flag, or one value of a field of several bits (mask) */
struct mode {
    const char *name;
    enum field field;
    tcflag_t bits, mask;
};

/* is the mode one value of a field of several bits? */
static bool is_value(const struct mode *m)
{
    if (m->field == CF)
        return m->mask == CSIZE;
    return m->field == OF && (m->mask == CRDLY || m->mask == NLDLY || m->mask == TABDLY ||
                              m->mask == BSDLY || m->mask == FFDLY || m->mask == VTDLY);
}

static const struct mode modes[] = {
    /* control modes */
    { "parenb", CF, PARENB, PARENB }, { "parodd", CF, PARODD, PARODD },
    { "cs5", CF, CS5, CSIZE }, { "cs6", CF, CS6, CSIZE }, { "cs7", CF, CS7, CSIZE },
    { "cs8", CF, CS8, CSIZE },
    { "hupcl", CF, HUPCL, HUPCL }, { "cstopb", CF, CSTOPB, CSTOPB }, { "cread", CF, CREAD, CREAD },
    { "clocal", CF, CLOCAL, CLOCAL },
    /* input modes */
    { "ignbrk", IF, IGNBRK, IGNBRK }, { "brkint", IF, BRKINT, BRKINT }, { "ignpar", IF, IGNPAR, IGNPAR },
    { "parmrk", IF, PARMRK, PARMRK }, { "inpck", IF, INPCK, INPCK }, { "istrip", IF, ISTRIP, ISTRIP },
    { "inlcr", IF, INLCR, INLCR }, { "igncr", IF, IGNCR, IGNCR }, { "icrnl", IF, ICRNL, ICRNL },
    { "ixon", IF, IXON, IXON }, { "ixany", IF, IXANY, IXANY }, { "ixoff", IF, IXOFF, IXOFF },
    /* output modes */
    { "opost", OF, OPOST, OPOST }, { "onlcr", OF, ONLCR, ONLCR }, { "ocrnl", OF, OCRNL, OCRNL },
    { "onocr", OF, ONOCR, ONOCR }, { "onlret", OF, ONLRET, ONLRET }, { "ofill", OF, OFILL, OFILL },
    { "ofdel", OF, OFDEL, OFDEL },
    { "cr0", OF, CR0, CRDLY }, { "cr1", OF, CR1, CRDLY }, { "cr2", OF, CR2, CRDLY }, { "cr3", OF, CR3, CRDLY },
    { "nl0", OF, NL0, NLDLY }, { "nl1", OF, NL1, NLDLY },
    { "tab0", OF, TAB0, TABDLY }, { "tab1", OF, TAB1, TABDLY }, { "tab2", OF, TAB2, TABDLY },
    { "tab3", OF, TAB3, TABDLY },
    { "bs0", OF, BS0, BSDLY }, { "bs1", OF, BS1, BSDLY },
    { "ff0", OF, FF0, FFDLY }, { "ff1", OF, FF1, FFDLY },
    { "vt0", OF, VT0, VTDLY }, { "vt1", OF, VT1, VTDLY },
    /* local modes */
    { "isig", LF, ISIG, ISIG }, { "icanon", LF, ICANON, ICANON }, { "iexten", LF, IEXTEN, IEXTEN },
    { "echo", LF, ECHO, ECHO }, { "echoe", LF, ECHOE, ECHOE }, { "echok", LF, ECHOK, ECHOK },
    { "echonl", LF, ECHONL, ECHONL }, { "noflsh", LF, NOFLSH, NOFLSH }, { "tostop", LF, TOSTOP, TOSTOP },
};
#define NMODES (sizeof modes / sizeof modes[0])

static const struct {
    const char *name;
    int index;
    cc_t sane;
} chars[] = {
    { "intr", VINTR, 3 }, { "quit", VQUIT, 28 }, { "erase", VERASE, 127 }, { "kill", VKILL, 21 },
    { "eof", VEOF, 4 }, { "eol", VEOL, 0 /* disabled */ }, { "start", VSTART, 17 },
    { "stop", VSTOP, 19 }, { "susp", VSUSP, 26 },
};
#define NCHARS (sizeof chars / sizeof chars[0])

static const struct {
    long n;
    speed_t code;
} speeds[] = {
    { 0, B0 }, { 50, B50 }, { 75, B75 }, { 110, B110 }, { 134, B134 }, { 150, B150 }, { 200, B200 },
    { 300, B300 }, { 600, B600 }, { 1200, B1200 }, { 1800, B1800 }, { 2400, B2400 }, { 4800, B4800 },
    { 9600, B9600 }, { 19200, B19200 }, { 38400, B38400 },
#ifdef B57600
    { 57600, B57600 },
#endif
#ifdef B115200
    { 115200, B115200 },
#endif
#ifdef B230400
    { 230400, B230400 },
#endif
#ifdef B460800
    { 460800, B460800 },
#endif
#ifdef B500000
    { 500000, B500000 },
#endif
#ifdef B576000
    { 576000, B576000 },
#endif
#ifdef B921600
    { 921600, B921600 },
#endif
#ifdef B1000000
    { 1000000, B1000000 },
#endif
#ifdef B1152000
    { 1152000, B1152000 },
#endif
#ifdef B1500000
    { 1500000, B1500000 },
#endif
#ifdef B2000000
    { 2000000, B2000000 },
#endif
#ifdef B2500000
    { 2500000, B2500000 },
#endif
#ifdef B3000000
    { 3000000, B3000000 },
#endif
#ifdef B3500000
    { 3500000, B3500000 },
#endif
#ifdef B4000000
    { 4000000, B4000000 },
#endif
};
#define NSPEEDS (sizeof speeds / sizeof speeds[0])

static cc_t vdisable;           /* _POSIX_VDISABLE for the terminal */
static bool have_vdisable;

static tcflag_t *flagp(struct termios *t, enum field f)
{
    switch (f) {
    case CF: return &t->c_cflag;
    case IF: return &t->c_iflag;
    case OF: return &t->c_oflag;
    default: return &t->c_lflag;
    }
}

static long speed_number(speed_t code)
{
    for (size_t i = 0; i < NSPEEDS; i++)
        if (speeds[i].code == code)
            return speeds[i].n;
    return -1;
}

static bool speed_code(const char *s, speed_t *code)
{
    if (!*s || strspn(s, "0123456789") != strlen(s))
        return false;
    long n = strtol(s, NULL, 10);
    for (size_t i = 0; i < NSPEEDS; i++)
        if (speeds[i].n == n) {
            *code = speeds[i].code;
            return true;
        }
    return false;
}

/* a control character as written */
static void show_char(char *buf, size_t size, cc_t c)
{
    if (have_vdisable && c == vdisable) {
        snprintf(buf, size, "<undef>");
        return;
    }
    char pre[3] = "";
    if (c & 0x80) {
        strcpy(pre, "M-");
        c &= 0x7f;
    }
    if (c == 127)
        snprintf(buf, size, "%s^?", pre);
    else if (c < 32)
        snprintf(buf, size, "%s^%c", pre, c + '@');
    else
        snprintf(buf, size, "%s%c", pre, c);
}

static bool get_winsize(struct winsize *w)
{
    return ioctl(0, TIOCGWINSZ, w) == 0;
}

static void sane(struct termios *t)
{
    t->c_cflag |= CREAD;
    t->c_iflag &= ~(tcflag_t)(IGNBRK | INLCR | IGNCR | IXOFF | IXANY);
    t->c_iflag |= BRKINT | ICRNL;
    t->c_oflag &= ~(tcflag_t)(OCRNL | ONOCR | ONLRET | OFILL | OFDEL | CRDLY | NLDLY | TABDLY | BSDLY |
                              FFDLY | VTDLY);
    t->c_oflag |= OPOST | ONLCR;
    t->c_lflag &= ~(tcflag_t)(ECHONL | NOFLSH | TOSTOP);
    t->c_lflag |= ISIG | ICANON | IEXTEN | ECHO | ECHOE | ECHOK;
    for (size_t i = 0; i < NCHARS; i++)
        t->c_cc[chars[i].index] = chars[i].sane ? chars[i].sane : vdisable;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

/* -g */
static void print_saved(const struct termios *t)
{
    printf("stty1:%lx:%lx:%lx:%lx:%lx:%lx", (unsigned long)t->c_iflag, (unsigned long)t->c_oflag,
           (unsigned long)t->c_cflag, (unsigned long)t->c_lflag, (unsigned long)cfgetispeed(t),
           (unsigned long)cfgetospeed(t));
    for (int i = 0; i < NCCS; i++)
        printf(":%x", t->c_cc[i]);
    putchar('\n');
}

static bool parse_saved(const char *s, struct termios *t)
{
    if (strncmp(s, "stty1:", 6) != 0)
        return false;
    s += 6;
    unsigned long v[6 + NCCS];
    int n = 0;
    while (*s && n < 6 + NCCS) {
        char *end;
        if (!isxdigit((unsigned char)*s))
            return false;
        errno = 0;
        v[n++] = strtoul(s, &end, 16);
        if (errno)
            return false;
        s = end;
        if (*s == ':')
            s++;
        else if (*s)
            return false;
    }
    if (n != 6 + NCCS || *s)
        return false;
    t->c_iflag = (tcflag_t)v[0];
    t->c_oflag = (tcflag_t)v[1];
    t->c_cflag = (tcflag_t)v[2];
    t->c_lflag = (tcflag_t)v[3];
    if (cfsetispeed(t, (speed_t)v[4]) != 0 || cfsetospeed(t, (speed_t)v[5]) != 0)
        return false;
    for (int i = 0; i < NCCS; i++) {
        if (v[6 + i] > 255)
            return false;
        t->c_cc[i] = (cc_t)v[6 + i];
    }
    return true;
}

static void print_speed(const struct termios *t)
{
    long is = speed_number(cfgetispeed(t)), os = speed_number(cfgetospeed(t));
    if (is == os || is == 0)
        printf("speed %ld baud;", os);
    else
        printf("ispeed %ld baud; ospeed %ld baud;", is, os);
}

static void print_all(const struct termios *t)
{
    print_speed(t);
    struct winsize w;
    if (get_winsize(&w))
        printf(" rows %d; columns %d;", w.ws_row, w.ws_col);
    putchar('\n');
    char buf[16];
    for (size_t i = 0; i < NCHARS; i++) {
        show_char(buf, sizeof buf, t->c_cc[chars[i].index]);
        printf("%s%s = %s;", i == 0 || i == 6 ? "" : " ", chars[i].name, buf);
        if (i == 5)
            putchar('\n');
    }
    printf(" min = %d; time = %d;\n", t->c_cc[VMIN], t->c_cc[VTIME]);
    enum field last = CF;
    bool first = true;
    for (size_t i = 0; i < NMODES; i++) {
        const struct mode *m = &modes[i];
        if (m->field != last) {
            putchar('\n');
            last = m->field;
            first = true;
        }
        tcflag_t v = *flagp((struct termios *)t, m->field);
        if (!is_value(m))
            printf("%s%s%s", first ? "" : " ", v & m->bits ? "" : "-", m->name);
        else if ((v & m->mask) == m->bits)
            printf("%s%s", first ? "" : " ", m->name);
        else
            continue;
        first = false;
    }
    putchar('\n');
}

static void print_diff(const struct termios *t)
{
    struct termios s = *t;
    sane(&s);
    s.c_cflag = (s.c_cflag & ~(tcflag_t)(CSIZE | PARENB | PARODD | CSTOPB | HUPCL | CLOCAL)) | CS8;
    print_speed(t);
    putchar('\n');
    char buf[16];
    bool any = false;
    for (size_t i = 0; i < NCHARS; i++) {
        cc_t c = t->c_cc[chars[i].index];
        if (c == s.c_cc[chars[i].index])
            continue;
        show_char(buf, sizeof buf, c);
        printf("%s%s = %s;", any ? " " : "", chars[i].name, buf);
        any = true;
    }
    if (!(t->c_lflag & ICANON) && (t->c_cc[VMIN] != 1 || t->c_cc[VTIME] != 0)) {
        printf("%smin = %d; time = %d;", any ? " " : "", t->c_cc[VMIN], t->c_cc[VTIME]);
        any = true;
    }
    if (any)
        putchar('\n');
    any = false;
    for (size_t i = 0; i < NMODES; i++) {
        const struct mode *m = &modes[i];
        tcflag_t v = *flagp((struct termios *)t, m->field), sv = *flagp(&s, m->field);
        if ((v & m->mask) == (sv & m->mask))
            continue;
        if (!is_value(m))
            printf("%s%s%s", any ? " " : "", v & m->bits ? "" : "-", m->name);
        else if ((v & m->mask) == m->bits)
            printf("%s%s", any ? " " : "", m->name);
        else
            continue;
        any = true;
    }
    if (any)
        putchar('\n');
}

static int bad(const char *what, const char *arg)
{
    fprintf(stderr, "stty: %s%s%s\n", arg ? arg : "", arg ? ": " : "", what);
    return 1;
}

/* a control character's value */
static bool char_value(const char *s, cc_t *c)
{
    if (strcmp(s, "undef") == 0 || strcmp(s, "^-") == 0) {
        if (!have_vdisable)
            return false;
        *c = vdisable;
        return true;
    }
    if (s[0] == '^' && s[1] && !s[2]) {
        int x = (unsigned char)s[1];
        if (x == '?')
            *c = 127;
        else if (isalpha(x))
            *c = (cc_t)(toupper(x) - '@');
        else if (strchr("[\\]^_", x))
            *c = (cc_t)(x - '@');
        else
            return false;
        return true;
    }
    if (s[0] && !s[1]) {
        *c = (cc_t)(unsigned char)s[0];
        return true;
    }
    /* one multibyte character is not a byte value */
    return false;
}

static bool small_number(const char *s, long max, long *v)
{
    if (!*s || strspn(s, "0123456789") != strlen(s) || strlen(s) > 9)
        return false;
    *v = strtol(s, NULL, 10);
    return *v <= max;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int i = 1;
    bool oa = false, og = false;
    if (i < argc && strcmp(argv[i], "--") == 0)
        i++;
    else if (i < argc && argv[i][0] == '-' && argv[i][1] && strspn(argv[i] + 1, "ag") == strlen(argv[i] + 1)) {
        oa = strchr(argv[i], 'a') != NULL;
        og = strchr(argv[i], 'g') != NULL;
        i++;
        if (i < argc && strcmp(argv[i], "--") == 0)
            i++;
        if ((oa && og) || i < argc) {
            fprintf(stderr, "usage: stty [-a|-g]\n       stty operand...\n");
            return 2;
        }
    }
    struct termios t;
    if (tcgetattr(0, &t) != 0) {
        fprintf(stderr, "stty: standard input: %s\n", strerror(errno));
        return 1;
    }
    errno = 0;
    long vd = fpathconf(0, _PC_VDISABLE);
    have_vdisable = vd != -1;
    vdisable = have_vdisable ? (cc_t)vd : 0;

    if (i >= argc) {
        if (og)
            print_saved(&t);
        else if (oa)
            print_all(&t);
        else
            print_diff(&t);
        if (fflush(stdout) != 0 || ferror(stdout)) {
            fprintf(stderr, "stty: write error: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }

    /* the operands, applied to a copy */
    struct termios n = t;
    struct winsize w;
    bool have_w = get_winsize(&w), set_w = false, show_size = false;
    for (; i < argc; i++) {
        const char *a = argv[i];
        bool neg = a[0] == '-';
        const char *name = neg ? a + 1 : a;
        size_t k;
        for (k = 0; k < NMODES; k++)
            if (strcmp(modes[k].name, name) == 0)
                break;
        if (k < NMODES) {
            const struct mode *m = &modes[k];
            tcflag_t *f = flagp(&n, m->field);
            if (!is_value(m))
                *f = neg ? *f & ~m->bits : *f | m->bits;
            else if (neg)
                return bad("not a mode that can be negated", a);
            else
                *f = (*f & ~m->mask) | m->bits;
            continue;
        }
        for (k = 0; k < NCHARS; k++)
            if (strcmp(chars[k].name, a) == 0)
                break;
        if (k < NCHARS) {
            cc_t c;
            if (++i >= argc)
                return bad("a value is needed", a);
            if (!char_value(argv[i], &c))
                return bad("not a valid control character", argv[i]);
            n.c_cc[chars[k].index] = c;
            continue;
        }
        long v;
        speed_t sp;
        if (strcmp(a, "min") == 0 || strcmp(a, "time") == 0) {
            if (++i >= argc)
                return bad("a value is needed", a);
            if (!small_number(argv[i], 255, &v))
                return bad("not a valid value (0 to 255)", argv[i]);
            n.c_cc[a[0] == 'm' ? VMIN : VTIME] = (cc_t)v;
        } else if (strcmp(a, "ispeed") == 0 || strcmp(a, "ospeed") == 0) {
            if (++i >= argc)
                return bad("a value is needed", a);
            if (!speed_code(argv[i], &sp))
                return bad("not a speed", argv[i]);
            if ((a[0] == 'i' ? cfsetispeed(&n, sp) : cfsetospeed(&n, sp)) != 0)
                return bad(strerror(errno), argv[i]);
        } else if (speed_code(a, &sp)) {
            if (cfsetispeed(&n, sp) != 0 || cfsetospeed(&n, sp) != 0)
                return bad(strerror(errno), a);
        } else if (strcmp(a, "rows") == 0 || strcmp(a, "cols") == 0 || strcmp(a, "columns") == 0) {
            if (++i >= argc)
                return bad("a value is needed", a);
            if (!small_number(argv[i], USHRT_MAX, &v))
                return bad("not a valid size", argv[i]);
            if (!have_w)
                return bad("the window size cannot be read", a);
            if (a[0] == 'r')
                w.ws_row = (unsigned short)v;
            else
                w.ws_col = (unsigned short)v;
            set_w = true;
        } else if (strcmp(a, "size") == 0) {
            if (!have_w)
                return bad("the window size cannot be read", a);
            show_size = true;
        } else if (strcmp(a, "hup") == 0 || strcmp(a, "-hup") == 0) {
            n.c_cflag = neg ? n.c_cflag & ~(tcflag_t)HUPCL : n.c_cflag | HUPCL;
        } else if (strcmp(a, "evenp") == 0 || strcmp(a, "parity") == 0 || strcmp(a, "oddp") == 0) {
            n.c_cflag = (n.c_cflag & ~(tcflag_t)(CSIZE | PARODD)) | PARENB | CS7;
            if (a[0] == 'o')
                n.c_cflag |= PARODD;
        } else if (strcmp(a, "-evenp") == 0 || strcmp(a, "-parity") == 0 || strcmp(a, "-oddp") == 0) {
            n.c_cflag = (n.c_cflag & ~(tcflag_t)(CSIZE | PARENB)) | CS8;
        } else if (strcmp(a, "raw") == 0) {
            if (!have_vdisable)
                return bad("control characters cannot be disabled on this terminal", a);
            n.c_cflag = (n.c_cflag & ~(tcflag_t)CSIZE) | CS8;
            n.c_cc[VERASE] = n.c_cc[VKILL] = n.c_cc[VINTR] = n.c_cc[VQUIT] = vdisable;
            n.c_cc[VEOF] = n.c_cc[VEOL] = vdisable;
            n.c_oflag &= ~(tcflag_t)OPOST;
            n.c_iflag &= ~(tcflag_t)INPCK;
        } else if (strcmp(a, "-raw") == 0 || strcmp(a, "cooked") == 0) {
            n.c_cc[VERASE] = 127;
            n.c_cc[VKILL] = 21;
            n.c_cc[VINTR] = 3;
            n.c_cc[VQUIT] = 28;
            n.c_cc[VEOF] = 4;
            n.c_cc[VEOL] = vdisable;
            n.c_oflag |= OPOST;
        } else if (strcmp(a, "nl") == 0) {
            n.c_iflag &= ~(tcflag_t)ICRNL;
        } else if (strcmp(a, "-nl") == 0) {
            n.c_iflag = (n.c_iflag & ~(tcflag_t)(INLCR | IGNCR)) | ICRNL;
        } else if (strcmp(a, "ek") == 0) {
            n.c_cc[VERASE] = 127;
            n.c_cc[VKILL] = 21;
        } else if (strcmp(a, "sane") == 0) {
            sane(&n);
        } else if (strcmp(a, "tabs") == 0 || strcmp(a, "-tabs") == 0) {
            n.c_oflag = (n.c_oflag & ~(tcflag_t)TABDLY) | (neg ? TAB3 : TAB0);
        } else if (strncmp(a, "stty1:", 6) == 0) {
            if (!parse_saved(a, &n))
                return bad("not valid saved settings", a);
        } else
            return bad("unknown operand", a);
    }
    if (memcmp(&n, &t, sizeof n) != 0) {
        if (tcsetattr(0, TCSADRAIN, &n) != 0) {
            fprintf(stderr, "stty: standard input: %s\n", strerror(errno));
            return 1;
        }
        /* what the terminal took */
        struct termios r;
        if (tcgetattr(0, &r) == 0 &&
            (r.c_iflag != n.c_iflag || r.c_oflag != n.c_oflag || r.c_lflag != n.c_lflag ||
             memcmp(r.c_cc, n.c_cc, sizeof r.c_cc) != 0)) {
            fprintf(stderr, "stty: standard input: the terminal did not take all the settings\n");
            return 1;
        }
    }
    if (set_w && ioctl(0, TIOCSWINSZ, &w) != 0) {
        fprintf(stderr, "stty: standard input: %s\n", strerror(errno));
        return 1;
    }
    if (show_size) {
        printf("%d %d\n", w.ws_row, w.ws_col);
        if (fflush(stdout) != 0 || ferror(stdout)) {
            fprintf(stderr, "stty: write error: %s\n", strerror(errno));
            return 1;
        }
    }
    return 0;
}
