/*
 * terminfo.c - reading compiled terminfo entries, and their parameter
 * strings (shared by tput and tabs; the same file in both directories)
 *
 * An entry is the file NAME in a subdirectory of a terminfo directory
 * named by its first character (or by that character in hexadecimal, as
 * on macOS).  The directories, in order: $TERMINFO, $HOME/.terminfo, those
 * of $TERMINFO_DIRS (an empty one is the system's), /etc/terminfo,
 * /lib/terminfo, /usr/share/terminfo, /usr/lib/terminfo.
 *
 * The format (term(5)): a header of six little-endian 16-bit numbers
 * (the magic 0432, or 01036 for 32-bit numbers; the sizes of the names,
 * the booleans, the numbers, the string offsets and the string table),
 * then the sections, the numbers starting at an even offset; then the
 * extended capabilities, again at an even offset, with their names.
 *
 * The capabilities' names and order are those of ncurses (the standard
 * ones of X/Open Curses first).
 */

#define _POSIX_C_SOURCE 200809L

#include "terminfo.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const boolnames[] = {
    "bw", "am", "xsb", "xhp", "xenl", "eo", "gn", "hc", "km", "hs", "in", "da", "db", "mir",
    "msgr", "os", "eslok", "xt", "hz", "ul", "xon", "nxon", "mc5i", "chts", "nrrmc", "npc",
    "ndscr", "ccc", "bce", "hls", "xhpa", "crxm", "daisy", "xvpa", "sam", "cpix", "lpix", "OTbs",
    "OTns", "OTnc", "OTMT", "OTNL", "OTpt", "OTxr",
};

static const char *const numnames[] = {
    "cols", "it", "lines", "lm", "xmc", "pb", "vt", "wsl", "nlab", "lh", "lw", "ma", "wnum",
    "colors", "pairs", "ncv", "bufsz", "spinv", "spinh", "maddr", "mjump", "mcs", "mls", "npins",
    "orc", "orl", "orhi", "orvi", "cps", "widcs", "btns", "bitwin", "bitype", "OTug", "OTdC",
    "OTdN", "OTdB", "OTdT", "OTkn",
};

static const char *const strnames[] = {
    "cbt", "bel", "cr", "csr", "tbc", "clear", "el", "ed", "hpa", "cmdch", "cup", "cud1", "home",
    "civis", "cub1", "mrcup", "cnorm", "cuf1", "ll", "cuu1", "cvvis", "dch1", "dl1", "dsl", "hd",
    "smacs", "blink", "bold", "smcup", "smdc", "dim", "smir", "invis", "prot", "rev", "smso",
    "smul", "ech", "rmacs", "sgr0", "rmcup", "rmdc", "rmir", "rmso", "rmul", "flash", "ff", "fsl",
    "is1", "is2", "is3", "if", "ich1", "il1", "ip", "kbs", "ktbc", "kclr", "kctab", "kdch1",
    "kdl1", "kcud1", "krmir", "kel", "ked", "kf0", "kf1", "kf10", "kf2", "kf3", "kf4", "kf5",
    "kf6", "kf7", "kf8", "kf9", "khome", "kich1", "kil1", "kcub1", "kll", "knp", "kpp", "kcuf1",
    "kind", "kri", "khts", "kcuu1", "rmkx", "smkx", "lf0", "lf1", "lf10", "lf2", "lf3", "lf4",
    "lf5", "lf6", "lf7", "lf8", "lf9", "rmm", "smm", "nel", "pad", "dch", "dl", "cud", "ich",
    "indn", "il", "cub", "cuf", "rin", "cuu", "pfkey", "pfloc", "pfx", "mc0", "mc4", "mc5", "rep",
    "rs1", "rs2", "rs3", "rf", "rc", "vpa", "sc", "ind", "ri", "sgr", "hts", "wind", "ht", "tsl",
    "uc", "hu", "iprog", "ka1", "ka3", "kb2", "kc1", "kc3", "mc5p", "rmp", "acsc", "pln", "kcbt",
    "smxon", "rmxon", "smam", "rmam", "xonc", "xoffc", "enacs", "smln", "rmln", "kbeg", "kcan",
    "kclo", "kcmd", "kcpy", "kcrt", "kend", "kent", "kext", "kfnd", "khlp", "kmrk", "kmsg", "kmov",
    "knxt", "kopn", "kopt", "kprv", "kprt", "krdo", "kref", "krfr", "krpl", "krst", "kres", "ksav",
    "kspd", "kund", "kBEG", "kCAN", "kCMD", "kCPY", "kCRT", "kDC", "kDL", "kslt", "kEND", "kEOL",
    "kEXT", "kFND", "kHLP", "kHOM", "kIC", "kLFT", "kMSG", "kMOV", "kNXT", "kOPT", "kPRV", "kPRT",
    "kRDO", "kRPL", "kRIT", "kRES", "kSAV", "kSPD", "kUND", "rfi", "kf11", "kf12", "kf13", "kf14",
    "kf15", "kf16", "kf17", "kf18", "kf19", "kf20", "kf21", "kf22", "kf23", "kf24", "kf25", "kf26",
    "kf27", "kf28", "kf29", "kf30", "kf31", "kf32", "kf33", "kf34", "kf35", "kf36", "kf37", "kf38",
    "kf39", "kf40", "kf41", "kf42", "kf43", "kf44", "kf45", "kf46", "kf47", "kf48", "kf49", "kf50",
    "kf51", "kf52", "kf53", "kf54", "kf55", "kf56", "kf57", "kf58", "kf59", "kf60", "kf61", "kf62",
    "kf63", "el1", "mgc", "smgl", "smgr", "fln", "sclk", "dclk", "rmclk", "cwin", "wingo", "hup",
    "dial", "qdial", "tone", "pulse", "hook", "pause", "wait", "u0", "u1", "u2", "u3", "u4", "u5",
    "u6", "u7", "u8", "u9", "op", "oc", "initc", "initp", "scp", "setf", "setb", "cpi", "lpi",
    "chr", "cvr", "defc", "swidm", "sdrfq", "sitm", "slm", "smicm", "snlq", "snrmq", "sshm",
    "ssubm", "ssupm", "sum", "rwidm", "ritm", "rlm", "rmicm", "rshm", "rsubm", "rsupm", "rum",
    "mhpa", "mcud1", "mcub1", "mcuf1", "mvpa", "mcuu1", "porder", "mcud", "mcub", "mcuf", "mcuu",
    "scs", "smgb", "smgbp", "smglp", "smgrp", "smgt", "smgtp", "sbim", "scsd", "rbim", "rcsd",
    "subcs", "supcs", "docr", "zerom", "csnm", "kmous", "minfo", "reqmp", "getm", "setaf", "setab",
    "pfxl", "devt", "csin", "s0ds", "s1ds", "s2ds", "s3ds", "smglr", "smgtb", "birep", "binel",
    "bicr", "colornm", "defbi", "endbi", "setcolor", "slines", "dispc", "smpch", "rmpch", "smsc",
    "rmsc", "pctrm", "scesc", "scesa", "ehhlm", "elhlm", "elohlm", "erhlm", "ethlm", "evhlm",
    "sgr1", "slength", "OTi2", "OTrs", "OTnl", "OTbc", "OTko", "OTma", "OTG2", "OTG3", "OTG1",
    "OTG4", "OTGR", "OTGL", "OTGU", "OTGD", "OTGH", "OTGV", "OTGC", "meml", "memu", "box1",
};

#define NBOOLNAMES (int)(sizeof boolnames / sizeof boolnames[0])
#define NNUMNAMES (int)(sizeof numnames / sizeof numnames[0])
#define NSTRNAMES (int)(sizeof strnames / sizeof strnames[0])

/* ---- reading ---- */

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 4096, n = 0;
    unsigned char *buf = malloc(cap);
    while (buf) {
        size_t k = fread(buf + n, 1, cap - n, f);
        n += k;
        if (n < cap)
            break;
        if (cap > 1 << 20) {        /* not a terminfo entry */
            free(buf);
            buf = NULL;
            break;
        }
        unsigned char *nb = realloc(buf, cap *= 2);
        if (!nb)
            free(buf);
        buf = nb;
    }
    fclose(f);
    *len = n;
    return buf;
}

static int s16(const unsigned char *p)
{
    int v = p[0] | p[1] << 8;
    return v >= 0x8000 ? v - 0x10000 : v;
}

static long s32(const unsigned char *p)
{
    unsigned long v = (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 |
                      (unsigned long)p[3] << 24;
    return v >= 0x80000000UL ? -(long)(0xffffffffUL - v) - 1 : (long)v;
}

/* a string at offset off of the table tab (size size); NULL if absent */
static char *str_at(const unsigned char *tab, long size, int off)
{
    if (off < 0 || off >= size)
        return NULL;
    const unsigned char *end = memchr(tab + off, '\0', (size_t)(size - off));
    if (!end)
        return NULL;
    return strdup((const char *)tab + off);
}

static int parse(const unsigned char *b, size_t len, struct terminfo *t)
{
    if (len < 12)
        return -2;
    int magic = s16(b), wide;
    if (magic == 0432)
        wide = 0;
    else if (magic == 01036)
        wide = 1;
    else
        return -2;
    int nsz = s16(b + 2), nb = s16(b + 4), nn = s16(b + 6), ns = s16(b + 8), tsz = s16(b + 10);
    int numsz = wide ? 4 : 2;
    if (nsz < 0 || nb < 0 || nn < 0 || ns < 0 || tsz < 0)
        return -2;
    size_t off = 12;
    if (off + (size_t)nsz > len)
        return -2;
    t->names = malloc((size_t)nsz + 1);
    if (!t->names)
        return -2;
    memcpy(t->names, b + off, (size_t)nsz);
    t->names[nsz] = '\0';
    off += (size_t)nsz;
    if (off + (size_t)nb > len)
        return -2;
    t->nbools = nb;
    t->bools = malloc((size_t)nb + 1);
    for (int i = 0; t->bools && i < nb; i++)
        t->bools[i] = (signed char)b[off + (size_t)i];
    off += (size_t)nb;
    if (off % 2)
        off++;
    if (off + (size_t)nn * (size_t)numsz > len)
        return -2;
    t->nnums = nn;
    t->nums = malloc(((size_t)nn + 1) * sizeof *t->nums);
    for (int i = 0; t->nums && i < nn; i++)
        t->nums[i] = wide ? s32(b + off + (size_t)i * 4) : s16(b + off + (size_t)i * 2);
    off += (size_t)nn * (size_t)numsz;
    if (off + (size_t)ns * 2 + (size_t)tsz > len)
        return -2;
    const unsigned char *tab = b + off + (size_t)ns * 2;
    t->nstrs = ns;
    t->strs = calloc((size_t)ns + 1, sizeof *t->strs);
    for (int i = 0; t->strs && i < ns; i++)
        t->strs[i] = str_at(tab, tsz, s16(b + off + (size_t)i * 2));
    off += (size_t)ns * 2 + (size_t)tsz;
    if (!t->bools || !t->nums || !t->strs)
        return -2;

    /* the extended capabilities */
    if (off % 2)
        off++;
    if (off + 10 > len)
        return 0;
    int xb = s16(b + off), xn = s16(b + off + 2), xs = s16(b + off + 4), xitems = s16(b + off + 6),
        xtsz = s16(b + off + 8);
    off += 10;
    if (xb < 0 || xn < 0 || xs < 0 || xitems < 0 || xtsz < 0)
        return 0;
    size_t bools_off = off;
    off += (size_t)xb;
    if (off % 2)
        off++;
    size_t nums_off = off;
    off += (size_t)xn * (size_t)numsz;
    size_t offs_off = off;
    int noffs = xs + xb + xn + xs;      /* the strings' offsets, then the names' */
    off += (size_t)noffs * 2;
    if (off + (size_t)xtsz > len)
        return 0;
    const unsigned char *xtab = b + off;
    t->xbools = malloc((size_t)xb + 1);
    t->xnums = malloc(((size_t)xn + 1) * sizeof *t->xnums);
    t->xstrs = calloc((size_t)xs + 1, sizeof *t->xstrs);
    t->xnames = calloc((size_t)(xb + xn + xs) + 1, sizeof *t->xnames);
    if (!t->xbools || !t->xnums || !t->xstrs || !t->xnames)
        return 0;
    for (int i = 0; i < xb; i++)
        t->xbools[i] = (signed char)b[bools_off + (size_t)i];
    for (int i = 0; i < xn; i++)
        t->xnums[i] = wide ? s32(b + nums_off + (size_t)i * 4) : s16(b + nums_off + (size_t)i * 2);
    /* the strings' offsets are from the start of the table; the names
       follow the last string, their offsets from the start of the names */
    long names_start = 0;
    for (int i = 0; i < xs; i++) {
        int o = s16(b + offs_off + (size_t)i * 2);
        t->xstrs[i] = str_at(xtab, xtsz, o);
        if (t->xstrs[i] && o + (long)strlen(t->xstrs[i]) + 1 > names_start)
            names_start = o + (long)strlen(t->xstrs[i]) + 1;
    }
    for (int i = 0; i < xb + xn + xs; i++) {
        int o = s16(b + offs_off + (size_t)(xs + i) * 2);
        t->xnames[i] = o < 0 ? NULL : str_at(xtab, xtsz, (int)(names_start + o));
    }
    t->nxbools = xb;
    t->nxnums = xn;
    t->nxstrs = xs;
    return 0;
}

static int try_dir(const char *dir, const char *name, struct terminfo *t)
{
    size_t n = strlen(dir) + strlen(name) + 8;
    char *path = malloc(n);
    if (!path)
        return -1;
    int r = -1;
    for (int hex = 0; hex < 2 && r == -1; hex++) {
        if (hex)
            snprintf(path, n, "%s/%02x/%s", dir, (unsigned char)name[0], name);
        else
            snprintf(path, n, "%s/%c/%s", dir, name[0], name);
        size_t len;
        unsigned char *b = read_file(path, &len);
        if (b) {
            r = parse(b, len, t);
            free(b);
        }
    }
    free(path);
    return r;
}

int ti_load(const char *name, struct terminfo *t)
{
    memset(t, 0, sizeof *t);
    if (!name || !*name || strchr(name, '/') || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;
    static const char *const sys[] = { "/etc/terminfo", "/lib/terminfo", "/usr/share/terminfo",
                                       "/usr/lib/terminfo" };
    const char *e = getenv("TERMINFO");
    int r;
    if (e && *e && (r = try_dir(e, name, t)) != -1)
        return r;
    const char *home = getenv("HOME");
    if (home && *home) {
        char *d = malloc(strlen(home) + 11);
        if (d) {
            sprintf(d, "%s/.terminfo", home);
            r = try_dir(d, name, t);
            free(d);
            if (r != -1)
                return r;
        }
    }
    const char *dirs = getenv("TERMINFO_DIRS");
    if (dirs) {
        char *copy = strdup(dirs);
        for (char *p = copy, *q; p; p = q) {
            q = strchr(p, ':');
            if (q)
                *q++ = '\0';
            if (*p)
                r = try_dir(p, name, t);
            else {
                r = -1;
                for (size_t i = 0; i < sizeof sys / sizeof sys[0] && r == -1; i++)
                    r = try_dir(sys[i], name, t);
            }
            if (r != -1) {
                free(copy);
                return r;
            }
        }
        free(copy);
    }
    for (size_t i = 0; i < sizeof sys / sizeof sys[0]; i++)
        if ((r = try_dir(sys[i], name, t)) != -1)
            return r;
    return -1;
}

enum ti_kind ti_find(const struct terminfo *t, const char *cap, long *num, const char **str)
{
    for (int i = 0; i < NBOOLNAMES; i++)
        if (strcmp(boolnames[i], cap) == 0) {
            *num = i < t->nbools && t->bools[i] > 0;
            return TI_BOOL;
        }
    for (int i = 0; i < NNUMNAMES; i++)
        if (strcmp(numnames[i], cap) == 0) {
            *num = i < t->nnums && t->nums[i] >= 0 ? t->nums[i] : -1;
            return TI_NUM;
        }
    for (int i = 0; i < NSTRNAMES; i++)
        if (strcmp(strnames[i], cap) == 0) {
            *str = i < t->nstrs ? t->strs[i] : NULL;
            return TI_STR;
        }
    int nx = t->nxbools + t->nxnums + t->nxstrs;
    for (int i = 0; t->xnames && i < nx; i++)
        if (t->xnames[i] && strcmp(t->xnames[i], cap) == 0) {
            if (i < t->nxbools) {
                *num = t->xbools[i] > 0;
                return TI_BOOL;
            }
            if ((i -= t->nxbools) < t->nxnums) {
                *num = t->xnums[i] >= 0 ? t->xnums[i] : -1;
                return TI_NUM;
            }
            *str = t->xstrs[i - t->nxnums];
            return TI_STR;
        }
    return TI_ABSENT;
}

const char *ti_longname(const struct terminfo *t)
{
    const char *bar = strrchr(t->names, '|');
    return bar ? bar + 1 : t->names;
}

/* ---- parameters ---- */

struct out {
    char *s;
    size_t n, cap;
    bool bad;
};

static void add(struct out *o, const char *s, size_t n)
{
    if (o->n + n + 1 > o->cap) {
        size_t cap = (o->n + n + 1) * 2;
        char *ns = realloc(o->s, cap);
        if (!ns) {
            o->bad = true;
            return;
        }
        o->s = ns;
        o->cap = cap;
    }
    memcpy(o->s + o->n, s, n);
    o->n += n;
    o->s[o->n] = '\0';
}

/* the stack: numbers or strings */
struct item {
    long n;
    const char *s;
};

int ti_nparams(const char *s, bool isstr[9])
{
    int max = 0, last = -1;
    for (int i = 0; i < 9; i++)
        isstr[i] = false;
    for (const char *p = s; p && *p; p++) {
        if (*p != '%')
            continue;
        p++;
        if (*p == 'p' && p[1] >= '1' && p[1] <= '9') {
            last = p[1] - '1';
            if (last + 1 > max)
                max = last + 1;
            p++;
        } else if ((*p == 's' || *p == 'l') && last >= 0)
            isstr[last] = true;
        else if (*p == '\0')
            break;
    }
    return max;
}

char *ti_tparm(const char *s, const long p0[9], char *const sp[9])
{
    struct out o = { NULL, 0, 0, false };
    add(&o, "", 0);
    struct item stack[64];
    int top = 0;
    long dyn[26] = { 0 }, stat[26] = { 0 };
    long p[9];
    for (int i = 0; i < 9; i++)
        p[i] = p0[i];
#define PUSHN(v) do { long v_ = (v); if (top < 64) { stack[top].n = v_; stack[top].s = NULL; top++; } } while (0)
#define POPN() (top > 0 ? stack[--top].n : 0)
    for (const char *c = s; *c; c++) {
        if (*c != '%') {
            add(&o, c, 1);
            continue;
        }
        c++;
        if (!*c)
            break;
        /* %[[:]flags][width[.precision]][doxXs] */
        const char *start = c;
        if (*c == ':')
            c++;
        while (*c && strchr("-+# ", *c))
            c++;
        while (*c >= '0' && *c <= '9')
            c++;
        if (*c == '.') {
            c++;
            while (*c >= '0' && *c <= '9')
                c++;
        }
        if (*c && strchr("doxXs", *c)) {
            char fmt[32] = "%";
            size_t fl = (size_t)(c - start);
            if (*start == ':') {
                start++;
                fl--;
            }
            if (fl > 20)
                fl = 20;
            memcpy(fmt + 1, start, fl);
            char buf[512];
            if (*c == 's') {
                const char *str = "";
                if (top > 0) {
                    top--;
                    str = stack[top].s ? stack[top].s : "";
                }
                strcpy(fmt + 1 + fl, "s");
                snprintf(buf, sizeof buf, fmt, str);
            } else {
                long v = POPN();
                fmt[1 + fl] = 'l';
                fmt[2 + fl] = *c;
                fmt[3 + fl] = '\0';
                snprintf(buf, sizeof buf, fmt, v);
            }
            add(&o, buf, strlen(buf));
            continue;
        }
        c = start;
        switch (*c) {
        case '%': add(&o, "%", 1); break;
        case 'c': {
            char ch = (char)POPN();
            add(&o, &ch, 1);         /* a NUL too */
            break;
        }
        case 'p':
            if (c[1] >= '1' && c[1] <= '9') {
                int k = c[1] - '1';
                if (top < 64) {
                    stack[top].n = p[k];
                    stack[top].s = sp ? sp[k] : NULL;
                    top++;
                }
                c++;
            }
            break;
        case 'P':
            if (c[1] >= 'a' && c[1] <= 'z')
                dyn[c[1] - 'a'] = POPN();
            else if (c[1] >= 'A' && c[1] <= 'Z')
                stat[c[1] - 'A'] = POPN();
            if (c[1])
                c++;
            break;
        case 'g':
            if (c[1] >= 'a' && c[1] <= 'z')
                PUSHN(dyn[c[1] - 'a']);
            else if (c[1] >= 'A' && c[1] <= 'Z')
                PUSHN(stat[c[1] - 'A']);
            if (c[1])
                c++;
            break;
        case '\'':
            if (c[1]) {
                PUSHN((unsigned char)c[1]);
                c += c[2] == '\'' ? 2 : 1;
            }
            break;
        case '{': {
            long v = 0;
            c++;
            while (*c >= '0' && *c <= '9')
                v = v * 10 + (*c++ - '0');
            if (*c != '}')
                c--;
            PUSHN(v);
            break;
        }
        case 'l': {
            const char *str = top > 0 ? stack[top - 1].s : NULL;
            if (top > 0)
                top--;
            PUSHN(str ? (long)strlen(str) : 0);
            break;
        }
        case 'i':
            p[0]++;
            p[1]++;
            break;
        case '+': case '-': case '*': case '/': case 'm': case '&': case '|': case '^':
        case '=': case '<': case '>': case 'A': case 'O': {
            long y = POPN(), x = POPN(), r = 0;
            switch (*c) {
            case '+': r = x + y; break;
            case '-': r = x - y; break;
            case '*': r = x * y; break;
            case '/': r = y ? x / y : 0; break;
            case 'm': r = y ? x % y : 0; break;
            case '&': r = x & y; break;
            case '|': r = x | y; break;
            case '^': r = x ^ y; break;
            case '=': r = x == y; break;
            case '<': r = x < y; break;
            case '>': r = x > y; break;
            case 'A': r = x && y; break;
            case 'O': r = x || y; break;
            }
            PUSHN(r);
            break;
        }
        case '!': PUSHN(!POPN()); break;
        case '~': PUSHN(~POPN()); break;
        case '?':
        case ';':
            break;
        case 't': {
            if (POPN())
                break;
            /* false: to the matching %e or %; */
            int level = 0;
            for (c++; *c; c++) {
                if (*c != '%' || !c[1])
                    continue;
                c++;
                if (*c == '?')
                    level++;
                else if (*c == ';') {
                    if (level-- == 0)
                        break;
                } else if (*c == 'e' && level == 0)
                    break;
            }
            if (!*c)
                c--;
            break;
        }
        case 'e': {
            /* the end of a then-part: to the matching %; */
            int level = 0;
            for (c++; *c; c++) {
                if (*c != '%' || !c[1])
                    continue;
                c++;
                if (*c == '?')
                    level++;
                else if (*c == ';' && level-- == 0)
                    break;
            }
            if (!*c)
                c--;
            break;
        }
        default:
            break;
        }
    }
#undef PUSHN
#undef POPN
    if (o.bad) {
        free(o.s);
        return NULL;
    }
    return o.s;
}

bool ti_put(const char *s)
{
    for (; *s; s++) {
        if (s[0] == '$' && s[1] == '<') {
            const char *e = strchr(s, '>');
            if (e) {
                s = e;
                continue;
            }
        }
        if (putchar((unsigned char)*s) == EOF)
            return false;
    }
    return true;
}
