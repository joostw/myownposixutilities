/*
 * file.c - file: determine file type (POSIX.1-2024, XCU file)
 *
 * Synopsis:
 *   file [-dh] [-M file] [-m file] file...
 *   file -i [-h] file...
 *
 * For each operand, "name: type".  The order of the tests: the file
 * type (symbolic link with -h, directory, fifo, socket, block special,
 * character special); with -i a regular file is just "regular file";
 * an empty file is "empty"; then the position-sensitive tests (those of
 * -M and -m files, and the defaults of -d, in the order of the options);
 * then the context-sensitive defaults, which look at text; "data" when
 * nothing matched.
 *
 * The default position-sensitive tests know ELF (with "executable" for
 * executables), Mach-O and PE binaries, ar archives, cpio archives (odc,
 * newc, crc and binary), tar archives (ustar, GNU and checked V7
 * headers), compress, gzip, bzip2, xz, zstd and Zip data, PDF,
 * PostScript, PNG, GIF and JPEG images, Java classes and SQLite
 * databases.  The context-sensitive defaults tell ASCII, UTF-8 and
 * ISO-8859 text apart, and within it scripts with #! ("commands text"
 * for a shell), C ("c program text"), FORTRAN ("fortran program text")
 * and shell commands without #! ("commands text").
 *
 * The tests of -M and -m files are the lines of the standard: offset,
 * type, value and message, separated by tabs or blanks (the value of a
 * string ends at a tab if the line has one after it, else at a blank;
 * "\ " is a space).  Lines that are empty or start with # are ignored.
 * The types: d, u (with a size of 1, 2, 4 or 8, or C, S, I, L, and a
 * &mask), s, and byte, short, long, string; numbers are in the byte
 * order of the machine.  The comparisons: =, <, >, &, ^ and x.  The
 * message is a printf format for the value; the messages of a test and
 * of the > lines after it are joined with a space (none when a message
 * starts with \b).  Lines starting with more than one > apply when the
 * last line with one fewer matched.
 *
 * Choices where the standard leaves room:
 *  - A file that cannot be examined: "cannot open (reason)"; a symbolic
 *    link: "symbolic link to target"; neither affects the status.
 *  - The operand - is the standard input.
 *  - With -m alone the defaults follow; -d, -m and -M are applied in the
 *    order given, the context-sensitive defaults after the
 *    position-sensitive tests when -d is given or there is no -M.
 *  - -i with -d, -m or -M is a usage error.  A -M or -m file that
 *    cannot be read, or a line that is not understood, is an error
 *    (status 1) before any operand is looked at.
 *  - Usage errors exit with status 2.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUFSIZE (1L << 20)      /* what is read of a file */

static void usage(void)
{
    fprintf(stderr, "usage: file [-dh] [-M file] [-m file] file...\n"
                    "       file -i [-h] file...\n");
    exit(2);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "file: %s\n", strerror(ENOMEM));
        exit(1);
    }
    return p;
}

/* ---- the file being examined ---- */

static unsigned char *buf;
static size_t blen;             /* bytes in buf */
static int cur_fd = -1;
static bool cur_reg;            /* a regular file: pread beyond buf */
static off_t cur_size;

/* n bytes at off into out; false if the file is shorter */
static bool get(uintmax_t off, size_t n, unsigned char *out)
{
    if (off <= blen && n <= blen - off) {
        memcpy(out, buf + off, n);
        return true;
    }
    if (!cur_reg || off > (uintmax_t)cur_size || n > (uintmax_t)cur_size - off)
        return false;
    return pread(cur_fd, out, n, (off_t)off) == (ssize_t)n;
}

/* ---- magic files ---- */

enum { T_NUM, T_STR };
enum { OP_EQ, OP_LT, OP_GT, OP_AND, OP_XOR, OP_ANY };

struct test {
    int level;                  /* the number of > */
    uintmax_t offset;
    int type;                   /* T_NUM or T_STR */
    bool is_signed;
    int size;                   /* of a number */
    bool has_mask;
    uintmax_t mask;
    int op;
    uintmax_t value;            /* for a number */
    unsigned char *str;         /* for a string */
    size_t slen;
    char *message;
};

/* a list of tests, or the defaults (tests == NULL) */
struct source {
    struct test *tests;
    size_t n;
};

static struct source *sources;
static size_t nsources;
static bool defaults_ctx;       /* apply the context-sensitive defaults */

/* an unsigned number: decimal, 0x hexadecimal or 0 octal */
static bool number(const char **pp, uintmax_t *v)
{
    const char *p = *pp;
    int base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        base = 16, p += 2;
    else if (p[0] == '0')
        base = 8;
    if (!isxdigit((unsigned char)*p))
        return false;
    errno = 0;
    char *end;
    *v = strtoumax(p, &end, base);
    if (errno || end == p)
        return false;
    *pp = end;
    return true;
}

/* the string value at *pp, up to an unescaped blank (or a tab when tabs
   is set); escapes as in XBD Escape Sequences, and "\ " */
static unsigned char *string_value(const char **pp, bool tabs, size_t *len)
{
    const char *p = *pp;
    unsigned char *s = xmalloc(strlen(p) + 1);
    size_t n = 0;
    while (*p && (tabs ? *p != '\t' : !isblank((unsigned char)*p))) {
        if (*p != '\\' || !p[1]) {
            s[n++] = (unsigned char)*p++;
            continue;
        }
        p++;
        if (*p >= '0' && *p <= '7') {
            unsigned v = 0;
            for (int k = 0; k < 3 && *p >= '0' && *p <= '7'; k++)
                v = v * 8 + (unsigned)(*p++ - '0');
            s[n++] = (unsigned char)v;
            continue;
        }
        switch (*p) {
        case 'a': s[n++] = '\a'; break;
        case 'b': s[n++] = '\b'; break;
        case 'f': s[n++] = '\f'; break;
        case 'n': s[n++] = '\n'; break;
        case 'r': s[n++] = '\r'; break;
        case 't': s[n++] = '\t'; break;
        case 'v': s[n++] = '\v'; break;
        default: s[n++] = (unsigned char)*p; break;     /* \\, "\ ", others */
        }
        p++;
    }
    *pp = p;
    *len = n;
    return s;
}

static bool parse_line(char *line, struct test *t)
{
    const char *p = line;
    memset(t, 0, sizeof *t);
    while (*p == '>')
        t->level++, p++;
    if (!number(&p, &t->offset) || !isblank((unsigned char)*p))
        return false;
    while (isblank((unsigned char)*p))
        p++;

    /* the type */
    const char *ty = p;
    while (*p && !isblank((unsigned char)*p) && *p != '&')
        p++;
    size_t tl = (size_t)(p - ty);
    char tbuf[32];
    if (tl == 0 || tl >= sizeof tbuf)
        return false;
    memcpy(tbuf, ty, tl);
    tbuf[tl] = '\0';
    if (strcmp(tbuf, "byte") == 0)
        strcpy(tbuf, "dC");
    else if (strcmp(tbuf, "short") == 0)
        strcpy(tbuf, "dS");
    else if (strcmp(tbuf, "long") == 0)
        strcpy(tbuf, "dL");
    else if (strcmp(tbuf, "string") == 0)
        strcpy(tbuf, "s");
    if (strcmp(tbuf, "s") == 0) {
        t->type = T_STR;
    } else if (tbuf[0] == 'd' || tbuf[0] == 'u') {
        t->type = T_NUM;
        t->is_signed = tbuf[0] == 'd';
        const char *q = tbuf + 1;
        t->size = (int)sizeof(int);
        if (*q == 'C') t->size = 1, q++;
        else if (*q == 'S') t->size = (int)sizeof(short), q++;
        else if (*q == 'I') t->size = (int)sizeof(int), q++;
        else if (*q == 'L') t->size = (int)sizeof(long), q++;
        else if (isdigit((unsigned char)*q)) {
            t->size = atoi(q);
            while (isdigit((unsigned char)*q))
                q++;
            if (t->size != 1 && t->size != 2 && t->size != 4 && t->size != 8)
                return false;
        }
        if (*q)
            return false;
        if (*p == '&') {
            p++;
            if (!number(&p, &t->mask))
                return false;
            t->has_mask = true;
        }
    } else {
        return false;
    }
    if (!isblank((unsigned char)*p))
        return false;
    while (isblank((unsigned char)*p))
        p++;

    /* the value */
    if (t->type == T_STR) {
        bool tabs = strchr(p, '\t') != NULL;
        t->str = string_value(&p, tabs, &t->slen);
    } else {
        t->op = OP_EQ;
        switch (*p) {
        case '=': p++; break;
        case '<': t->op = OP_LT, p++; break;
        case '>': t->op = OP_GT, p++; break;
        case '&': t->op = OP_AND, p++; break;
        case '^': t->op = OP_XOR, p++; break;
        case 'x':
            if (p[1] == '\0' || isblank((unsigned char)p[1])) {
                t->op = OP_ANY, p++;
                break;
            }
            return false;
        }
        if (t->op != OP_ANY) {
            bool neg = false;
            if (*p == '-' || *p == '+')
                neg = *p++ == '-';
            if (!number(&p, &t->value))
                return false;
            if (neg)
                t->value = (uintmax_t)0 - t->value;
        }
    }
    if (*p && !isblank((unsigned char)*p))
        return false;
    while (isblank((unsigned char)*p))
        p++;
    t->message = strdup(p);
    if (!t->message)
        return false;
    return true;
}

static void read_magic(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "file: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    struct source s = { NULL, 0 };
    size_t cap = 0;
    char *line = NULL;
    size_t lcap = 0;
    ssize_t len;
    unsigned long lineno = 0;
    while ((len = getline(&line, &lcap, f)) > 0) {
        lineno++;
        if (line[len - 1] == '\n')
            line[--len] = '\0';
        if (len == 0 || line[0] == '#')
            continue;
        if (s.n == cap) {
            cap = cap ? 2 * cap : 64;
            s.tests = realloc(s.tests, cap * sizeof *s.tests);
            if (!s.tests) {
                fprintf(stderr, "file: %s\n", strerror(ENOMEM));
                exit(1);
            }
        }
        if (!parse_line(line, &s.tests[s.n])) {
            fprintf(stderr, "file: %s: line %lu: not understood\n", path, lineno);
            exit(1);
        }
        s.n++;
    }
    if (ferror(f)) {
        fprintf(stderr, "file: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    fclose(f);
    free(line);
    if (!s.tests)
        s.tests = xmalloc(sizeof *s.tests);     /* an empty list, not the defaults */
    sources = realloc(sources, (nsources + 1) * sizeof *sources);
    if (!sources)
        exit(1);
    sources[nsources++] = s;
}

static void add_defaults(void)
{
    sources = realloc(sources, (nsources + 1) * sizeof *sources);
    if (!sources)
        exit(1);
    sources[nsources].tests = NULL;
    sources[nsources++].n = 0;
}

/* ---- the output of a message ---- */

static char out[8192];
static size_t olen;

static void emit(const char *s, size_t n)
{
    if (olen + n >= sizeof out)
        n = sizeof out - 1 - olen;
    memcpy(out + olen, s, n);
    olen += n;
    out[olen] = '\0';
}

static void emits(const char *s)
{
    emit(s, strlen(s));
}

/* the message of t, a printf format with the value as its argument */
static void message(const struct test *t, intmax_t num, const char *str)
{
    const char *m = t->message;
    if (strncmp(m, "\\b", 2) == 0)
        m += 2;
    else if (olen > 0 && *m)
        emit(" ", 1);
    while (*m) {
        if (*m == '\\' && m[1]) {
            /* the escapes of printf */
            char c = m[1];
            static const char from[] = "\\abfnrtv", to[] = "\\\a\b\f\n\r\t\v";
            const char *k = strchr(from, c);
            if (k) {
                emit(&to[k - from], 1);
                m += 2;
                continue;
            }
            emit(m, 1);
            m++;
            continue;
        }
        if (*m != '%') {
            emit(m++, 1);
            continue;
        }
        if (m[1] == '%') {
            emit("%", 1);
            m += 2;
            continue;
        }
        /* a conversion: flags, width, precision, letter */
        const char *s = m++;
        while (*m && strchr("-+ #0", *m))
            m++;
        while (isdigit((unsigned char)*m))
            m++;
        if (*m == '.') {
            m++;
            while (isdigit((unsigned char)*m))
                m++;
        }
        while (*m && strchr("hljztL", *m))
            m++;            /* length modifiers are not needed */
        char conv = *m;
        if (!conv)
            break;
        m++;
        char spec[64];
        size_t fl = 0;
        for (const char *q = s; q < m - 1 && fl < sizeof spec - 4; q++)
            if (!strchr("hljztL", *q))
                spec[fl++] = *q;
        char tmp[1024];
        if (strchr("diouxX", conv)) {
            spec[fl++] = 'j';
            spec[fl++] = conv;
            spec[fl] = '\0';
            if (conv == 'd' || conv == 'i')
                snprintf(tmp, sizeof tmp, spec, str ? (intmax_t)0 : num);
            else
                snprintf(tmp, sizeof tmp, spec, str ? (uintmax_t)0 : (uintmax_t)num);
        } else if (conv == 'c') {
            spec[fl++] = 'c';
            spec[fl] = '\0';
            snprintf(tmp, sizeof tmp, spec, str ? (str[0] & 0xff) : (int)(num & 0xff));
        } else if (conv == 's') {
            char nb[32];
            if (!str)
                snprintf(nb, sizeof nb, "%jd", num);
            spec[fl++] = 's';
            spec[fl] = '\0';
            snprintf(tmp, sizeof tmp, spec, str ? str : nb);
        } else {
            snprintf(tmp, sizeof tmp, "%.*s", (int)(m - s), s);
        }
        emits(tmp);
    }
}

/* ---- running the tests of a magic file ---- */

static bool run_test(const struct test *t, intmax_t *num, char **str)
{
    if (t->type == T_STR) {
        unsigned char *b = xmalloc(t->slen + 1);
        bool ok = get(t->offset, t->slen, b) && memcmp(b, t->str, t->slen) == 0;
        b[t->slen] = '\0';
        *str = (char *)b;
        return ok;
    }
    unsigned char b[8];
    if (!get(t->offset, (size_t)t->size, b))
        return false;
    uintmax_t v = 0;
    switch (t->size) {      /* the byte order of the machine */
    case 1: v = b[0]; break;
    case 2: { uint16_t x; memcpy(&x, b, 2); v = x; break; }
    case 4: { uint32_t x; memcpy(&x, b, 4); v = x; break; }
    case 8: { uint64_t x; memcpy(&x, b, 8); v = x; break; }
    }
    if (t->has_mask)
        v &= t->mask;
    intmax_t sv;
    int bits = t->size * 8;
    if (t->is_signed && bits < 64 && (v >> (bits - 1)) & 1)
        sv = (intmax_t)(v | ~(((uintmax_t)1 << bits) - 1));
    else
        sv = (intmax_t)v;
    *num = t->is_signed ? sv : (intmax_t)v;
    uintmax_t want = t->value;
    switch (t->op) {
    case OP_ANY: return true;
    case OP_AND: return (v & want) == want;
    case OP_XOR: return (v & want) != want;
    case OP_EQ:
        if (t->is_signed)
            return sv == (intmax_t)want;
        return v == (bits < 64 ? want & (((uintmax_t)1 << bits) - 1) : want);
    case OP_LT: return t->is_signed ? sv < (intmax_t)want : v < want;
    case OP_GT: return t->is_signed ? sv > (intmax_t)want : v > want;
    }
    return false;
}

/* the tests of s; true if one matched (its messages are in out) */
static bool run_source(const struct source *s)
{
    for (size_t i = 0; i < s->n; i++) {
        const struct test *t = &s->tests[i];
        if (t->level != 0)
            continue;
        intmax_t num = 0;
        char *str = NULL;
        bool ok = run_test(t, &num, &str);
        if (ok)
            message(t, num, str);
        free(str);
        if (!ok)
            continue;
        /* the > lines after it: level n applies when level n - 1 matched */
        int matched = 0;            /* the deepest level that matched */
        for (size_t j = i + 1; j < s->n && s->tests[j].level > 0; j++) {
            const struct test *c = &s->tests[j];
            if (c->level > matched + 1)
                continue;
            matched = c->level - 1;
            str = NULL;
            if (run_test(c, &num, &str)) {
                message(c, num, str);
                matched = c->level;
            }
            free(str);
        }
        return true;
    }
    return false;
}

/* ---- the default position-sensitive tests ---- */

static bool has(uintmax_t off, const char *s, size_t n)
{
    unsigned char b[64];
    return n <= sizeof b && get(off, n, b) && memcmp(b, s, n) == 0;
}

static uint64_t rd(const unsigned char *p, int n, bool big)
{
    uint64_t v = 0;
    for (int i = 0; i < n; i++)
        v |= (uint64_t)p[big ? n - 1 - i : i] << (8 * i);
    return v;
}

static const char *elf_machine(unsigned m)
{
    switch (m) {
    case 2: return "SPARC";
    case 3: return "Intel 80386";
    case 8: return "MIPS";
    case 20: return "PowerPC";
    case 21: return "64-bit PowerPC";
    case 22: return "IBM S/390";
    case 40: return "ARM";
    case 43: return "SPARC V9";
    case 50: return "IA-64";
    case 62: return "x86-64";
    case 183: return "ARM aarch64";
    case 243: return "RISC-V";
    case 247: return "eBPF";
    case 258: return "LoongArch";
    }
    return NULL;
}

static bool elf(void)
{
    unsigned char h[64];
    if (!get(0, 52, h) || memcmp(h, "\177ELF", 4) != 0)
        return false;
    int cls = h[4], data = h[5];
    if ((cls != 1 && cls != 2) || (data != 1 && data != 2)) {
        emits("ELF data");
        return true;
    }
    bool big = data == 2, b64 = cls == 2;
    if (b64 && !get(0, 64, h))
        return false;
    unsigned type = (unsigned)rd(h + 16, 2, big), mach = (unsigned)rd(h + 18, 2, big);
    uint64_t phoff = b64 ? rd(h + 32, 8, big) : rd(h + 28, 4, big);
    unsigned phentsize = (unsigned)rd(h + (b64 ? 54 : 42), 2, big);
    unsigned phnum = (unsigned)rd(h + (b64 ? 56 : 44), 2, big);
    bool interp = false, dynamic = false;
    for (unsigned i = 0; i < phnum && i < 4096 && phentsize >= 4; i++) {
        unsigned char ph[4];
        if (!get(phoff + (uint64_t)i * phentsize, 4, ph))
            break;
        uint32_t pt = (uint32_t)rd(ph, 4, big);
        if (pt == 3)
            interp = true;
        if (pt == 2)
            dynamic = true;
    }
    char s[200];
    const char *what;
    switch (type) {
    case 1: what = "relocatable"; break;
    case 2: what = "executable"; break;
    case 3: what = interp ? "pie executable" : "shared object"; break;
    case 4: what = "core file"; break;
    default: what = "data"; break;
    }
    const char *m = elf_machine(mach);
    char mb[32];
    if (!m)
        snprintf(mb, sizeof mb, "machine %u", mach), m = mb;
    snprintf(s, sizeof s, "ELF %d-bit %s %s, %s", b64 ? 64 : 32, big ? "MSB" : "LSB", what, m);
    emits(s);
    if (type == 2 || type == 3)
        emits(dynamic || interp ? ", dynamically linked" : ", statically linked");
    return true;
}

static bool macho(void)
{
    unsigned char h[16];
    if (!get(0, 16, h))
        return false;
    uint32_t le = (uint32_t)rd(h, 4, false), be = (uint32_t)rd(h, 4, true);
    bool big;
    int bits;
    if (be == 0xfeedface || le == 0xfeedface)
        bits = 32, big = be == 0xfeedface;
    else if (be == 0xfeedfacf || le == 0xfeedfacf)
        bits = 64, big = be == 0xfeedfacf;
    else
        return false;
    uint32_t ft = (uint32_t)rd(h + 12, 4, big);
    static const char *const kinds[] = { NULL, "object", "executable", NULL, "core",
        NULL, "dynamically linked shared library", "dynamic linker", "bundle" };
    char s[100];
    snprintf(s, sizeof s, "Mach-O %s%s", bits == 64 ? "64-bit " : "",
             ft < sizeof kinds / sizeof kinds[0] && kinds[ft] ? kinds[ft] : "file");
    emits(s);
    return true;
}

static bool pe(void)
{
    unsigned char h[64], p[24];
    if (!get(0, 64, h) || h[0] != 'M' || h[1] != 'Z')
        return false;
    uint32_t off = (uint32_t)rd(h + 60, 4, false);
    if (!get(off, 24, p) || memcmp(p, "PE\0\0", 4) != 0) {
        emits("MS-DOS executable");
        return true;
    }
    unsigned ch = (unsigned)rd(p + 22, 2, false);
    unsigned char o[2];
    bool plus = get(off + 24, 2, o) && rd(o, 2, false) == 0x20b;
    emits(plus ? "PE32+ " : "PE32 ");
    emits(ch & 0x2000 ? "executable (DLL)" : "executable");
    return true;
}

/* a V7 tar header: the checksum of the first block */
static bool tar_checksum(void)
{
    unsigned char h[512];
    if (!get(0, 512, h))
        return false;
    unsigned long sum = 0, want = 0;
    int k;
    for (int i = 0; i < 512; i++)
        sum += i >= 148 && i < 156 ? ' ' : h[i];
    for (k = 148; k < 156 && h[k] == ' '; k++)
        ;
    if (k == 156 || h[k] < '0' || h[k] > '7')
        return false;
    for (; k < 156 && h[k] >= '0' && h[k] <= '7'; k++)
        want = want * 8 + (unsigned long)(h[k] - '0');
    return sum == want && h[0] != 0;
}

static bool defaults_position(void)
{
    unsigned char h[32];
    char s[200];
    if (elf() || macho() || pe())
        return true;
    if (has(0, "!<arch>\n", 8)) {
        emits("current ar archive");
        return true;
    }
    if (has(0, "070707", 6)) {
        emits("ASCII cpio archive (pre-SVR4 or odc)");
        return true;
    }
    if (has(0, "070701", 6)) {
        emits("ASCII cpio archive (SVR4 with no CRC)");
        return true;
    }
    if (has(0, "070702", 6)) {
        emits("ASCII cpio archive (SVR4 with CRC)");
        return true;
    }
    if (get(0, 2, h) && ((h[0] == 0xc7 && h[1] == 0x71) || (h[0] == 0x71 && h[1] == 0xc7))
        && cur_size >= 26) {
        emits(h[0] == 0xc7 ? "cpio archive (binary, little-endian)" : "cpio archive (binary, big-endian)");
        return true;
    }
    if (has(257, "ustar\0" "00", 8)) {
        unsigned char ty;
        emits("POSIX tar archive");
        if (get(156, 1, &ty) && (ty == 'x' || ty == 'g'))
            emits(" (pax)");
        return true;
    }
    if (has(257, "ustar  \0", 8)) {
        emits("GNU tar archive");
        return true;
    }
    if (tar_checksum()) {
        emits("tar archive (V7)");
        return true;
    }
    if (get(0, 3, h) && h[0] == 0x1f && h[1] == 0x9d) {
        snprintf(s, sizeof s, "compress'd data %d bits", h[2] & 0x1f);
        emits(s);
        return true;
    }
    if (get(0, 3, h) && h[0] == 0x1f && h[1] == 0x8b) {
        emits("gzip compressed data");
        return true;
    }
    if (get(0, 4, h) && h[0] == 'B' && h[1] == 'Z' && h[2] == 'h' && h[3] >= '1' && h[3] <= '9') {
        emits("bzip2 compressed data");
        return true;
    }
    if (has(0, "\xfd" "7zXZ\0", 6)) {
        emits("XZ compressed data");
        return true;
    }
    if (has(0, "\x28\xb5\x2f\xfd", 4)) {
        emits("Zstandard compressed data");
        return true;
    }
    if (has(0, "PK\003\004", 4) || has(0, "PK\005\006", 4)) {
        emits("Zip archive data");
        return true;
    }
    if (has(0, "%PDF-", 5)) {
        unsigned char v[3];
        if (get(5, 3, v) && isdigit(v[0]) && v[1] == '.' && isdigit(v[2]))
            snprintf(s, sizeof s, "PDF document, version %c.%c", v[0], v[2]);
        else
            snprintf(s, sizeof s, "PDF document");
        emits(s);
        return true;
    }
    if (has(0, "%!PS", 4)) {
        emits("PostScript document text");
        return true;
    }
    if (has(0, "\x89PNG\r\n\x1a\n", 8)) {
        if (get(16, 8, h)) {
            snprintf(s, sizeof s, "PNG image data, %lu x %lu",
                     (unsigned long)rd(h, 4, true), (unsigned long)rd(h + 4, 4, true));
            emits(s);
        } else {
            emits("PNG image data");
        }
        return true;
    }
    if (has(0, "GIF87a", 6) || has(0, "GIF89a", 6)) {
        get(0, 10, h);
        snprintf(s, sizeof s, "GIF image data, version %.3s, %u x %u", (char *)h + 3,
                 (unsigned)rd(h + 6, 2, false), (unsigned)rd(h + 8, 2, false));
        emits(s);
        return true;
    }
    if (has(0, "\xff\xd8\xff", 3)) {
        emits("JPEG image data");
        return true;
    }
    if (has(0, "\xca\xfe\xba\xbe", 4) && get(4, 4, h) && rd(h + 2, 2, true) >= 45) {
        snprintf(s, sizeof s, "compiled Java class data, version %u.%u",
                 (unsigned)rd(h + 2, 2, true), (unsigned)rd(h, 2, true));
        emits(s);
        return true;
    }
    if (has(0, "SQLite format 3\0", 16)) {
        emits("SQLite 3.x database");
        return true;
    }
    return false;
}

/* ---- the default context-sensitive tests ---- */

enum { E_ASCII, E_UTF8, E_LATIN, E_NONE };

static int encoding(void)
{
    bool hi = false, utf8 = true, latin = true;
    for (size_t i = 0; i < blen; i++) {
        unsigned char c = buf[i];
        if (c < 0x80) {
            if (c == 0 || (c < 0x20 && !strchr("\a\b\t\n\v\f\r\033", c)))
                return E_NONE;
            if (c == 0x7f)
                return E_NONE;
            continue;
        }
        hi = true;
        if (c < 0xa0)
            latin = false;
        if (utf8) {
            int n = c >= 0xf0 && c <= 0xf4 ? 3 : c >= 0xe0 ? 2 : c >= 0xc2 && c < 0xe0 ? 1 : -1;
            if (n < 0 || c > 0xf4) {
                utf8 = false;
                continue;
            }
            size_t k;
            for (k = 1; k <= (size_t)n; k++)
                if (i + k >= blen || (buf[i + k] & 0xc0) != 0x80)
                    break;
            /* a character cut off at the end of what was read is fine */
            if (k <= (size_t)n && !(i + k >= blen && blen == BUFSIZE))
                utf8 = false;
            else
                i += k - 1;
        }
    }
    if (!hi)
        return E_ASCII;
    if (utf8)
        return E_UTF8;
    return latin ? E_LATIN : E_NONE;
}

/* the lines of buf, one at a time: start and length */
static bool next_line(size_t *pos, const char **s, size_t *n)
{
    if (*pos >= blen)
        return false;
    *s = (const char *)buf + *pos;
    const char *nl = memchr(*s, '\n', blen - *pos);
    *n = nl ? (size_t)(nl - *s) : blen - *pos;
    *pos += *n + 1;
    return true;
}

/* does the line start with word (followed by a non-identifier)? */
static bool starts(const char *s, size_t n, const char *word, bool icase)
{
    size_t k = strlen(word);
    if (n < k || (icase ? strncasecmp(s, word, k) : strncmp(s, word, k)) != 0)
        return false;
    return n == k || !(isalnum((unsigned char)s[k]) || s[k] == '_');
}

static bool is_c(void)
{
    size_t pos = 0, n;
    const char *s;
    int pre = 0, kw = 0, braces = 0, semis = 0;
    static const char *const cpp[] = { "include", "define", "ifdef", "ifndef", "if",
                                       "endif", "pragma", "undef", "elif", NULL };
    static const char *const words[] = { "int", "char", "void", "struct", "static",
        "typedef", "unsigned", "long", "extern", "const", "enum", "union", "double",
        "float", "return", "switch", NULL };
    while (next_line(&pos, &s, &n)) {
        while (n > 0 && isblank((unsigned char)*s))
            s++, n--;
        if (n > 0 && *s == '#') {
            s++, n--;
            while (n > 0 && isblank((unsigned char)*s))
                s++, n--;
            for (int i = 0; cpp[i]; i++)
                if (starts(s, n, cpp[i], false)) {
                    pre++;
                    break;
                }
            continue;
        }
        for (int i = 0; words[i]; i++)
            if (starts(s, n, words[i], false)) {
                kw++;
                break;
            }
        for (size_t i = 0; i < n; i++) {
            if (s[i] == '{' || s[i] == '}')
                braces++;
            if (s[i] == ';')
                semis++;
        }
    }
    return pre > 0 ? (kw > 0 || braces > 0 || pre >= 2) : (kw >= 1 && braces >= 2 && semis >= 1);
}

static bool is_fortran(void)
{
    size_t pos = 0, n;
    const char *s;
    int kw = 0, other = 0;
    bool first = true, first_kw = false;
    static const char *const words[] = { "program", "subroutine", "function", "integer",
        "real", "double precision", "dimension", "do", "call", "end", "format", "write",
        "read", "implicit", "common", "parameter", "character", "logical", "if", "continue",
        "stop", "return", "print", "goto", "go to", "module", "use", "contains", "complex",
        "data", "external", "block data", "else", "endif", "enddo", "open", "close", NULL };
    while (next_line(&pos, &s, &n)) {
        if (n == 0)
            continue;
        /* comments: C, c, * or ! in column 1, or ! after blanks */
        if (*s == 'C' || *s == 'c' || *s == '*' || *s == '!') {
            if (*s == '!' || n == 1 || !isalpha((unsigned char)s[1]) || s[1] == ' ')
                continue;
        }
        size_t k = 0;
        while (k < n && isblank((unsigned char)s[k]))
            k++;
        if (k == n || s[k] == '!')
            continue;
        /* fixed form: a label in columns 1-5; free form: anywhere */
        while (k < n && isdigit((unsigned char)s[k]))
            k++;
        while (k < n && isblank((unsigned char)s[k]))
            k++;
        bool hit = false;
        for (int i = 0; words[i]; i++)
            if (starts(s + k, n - k, words[i], true)) {
                hit = true;
                break;
            }
        if (hit)
            kw++;
        else
            other++;
        if (first)
            first_kw = hit, first = false;
    }
    return first_kw && kw >= 2 && kw >= other;
}

static bool is_shell(void)
{
    size_t pos = 0, n;
    const char *s;
    int open = 0, close = 0;
    static const char *const openw[] = { "if", "for", "while", "until", "case", NULL };
    static const char *const closew[] = { "fi", "done", "esac", NULL };
    while (next_line(&pos, &s, &n)) {
        while (n > 0 && isblank((unsigned char)*s))
            s++, n--;
        for (int i = 0; openw[i]; i++)
            if (starts(s, n, openw[i], false))
                open++;
        for (int i = 0; closew[i]; i++)
            if (starts(s, n, closew[i], false))
                close++;
        /* "; then", "; do" */
        for (size_t i = 0; i + 1 < n; i++)
            if (s[i] == ';' && (starts(s + i + 1 + (s[i + 1] == ' '), n - i - 1 - (s[i + 1] == ' '), "then", false)
                                || starts(s + i + 1 + (s[i + 1] == ' '), n - i - 1 - (s[i + 1] == ' '), "do", false)))
                open++;
    }
    return open > 0 && close > 0;
}

static void defaults_context(void)
{
    int e = encoding();
    if (e == E_NONE) {
        emits("data");
        return;
    }
    static const char *const enc[] = { "ASCII", "UTF-8", "ISO-8859" };
    char s[300];
    if (blen > 2 && buf[0] == '#' && buf[1] == '!') {
        /* the interpreter of a script */
        size_t i = 2, end;
        while (i < blen && isblank(buf[i]))
            i++;
        for (end = i; end < blen && !isspace(buf[end]); end++)
            ;
        char path[256];
        size_t pl = end - i < sizeof path - 1 ? end - i : sizeof path - 1;
        memcpy(path, buf + i, pl);
        path[pl] = '\0';
        char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (strcmp(base, "env") == 0) {
            /* #!/usr/bin/env name */
            size_t j = end;
            while (j < blen && isblank(buf[j]))
                j++;
            size_t k;
            for (k = j; k < blen && !isspace(buf[k]); k++)
                ;
            pl = k - j < sizeof path - 1 ? k - j : sizeof path - 1;
            memmove(path, buf + j, pl);
            path[pl] = '\0';
            base = path;
        }
        static const char *const shells[] = { "sh", "bash", "ksh", "dash", "zsh", "ash",
            "mksh", "yash", "posh", "ksh93", "csh", "tcsh", "busybox", NULL };
        bool shell = false;
        for (int k = 0; shells[k]; k++)
            if (strcmp(base, shells[k]) == 0)
                shell = true;
        if (*base == '\0')
            snprintf(s, sizeof s, "script, %s text", enc[e]);
        else if (shell)
            snprintf(s, sizeof s, "%s script, commands text", base);
        else
            snprintf(s, sizeof s, "%s script, %s text", base, enc[e]);
        emits(s);
        return;
    }
    const char *kind = is_c() ? "c program text" : is_fortran() ? "fortran program text"
                     : is_shell() ? "commands text" : NULL;
    if (kind) {
        emits(kind);
        if (e != E_ASCII) {
            snprintf(s, sizeof s, " (%s)", enc[e]);
            emits(s);
        }
        return;
    }
    snprintf(s, sizeof s, "%s text", enc[e]);
    emits(s);
}

/* ---- each operand ---- */

static bool opt_h, opt_i;

static void classify(const char *name)
{
    struct stat st;
    int fd = -1;
    olen = 0;
    out[0] = '\0';
    bool std = strcmp(name, "-") == 0;
    if (std) {
        if (fstat(0, &st) != 0) {
            printf("%s: cannot open (%s)\n", name, strerror(errno));
            return;
        }
        fd = 0;
    } else {
        bool link = false;
        if (lstat(name, &st) != 0) {
            printf("%s: cannot open (%s)\n", name, strerror(errno));
            return;
        }
        if (S_ISLNK(st.st_mode)) {
            struct stat t;
            if (opt_h || stat(name, &t) != 0)
                link = true;
            else
                st = t;
        }
        if (link) {
            char target[PATH_MAX + 1];
            ssize_t n = readlink(name, target, sizeof target - 1);
            if (n < 0) {
                printf("%s: cannot open (%s)\n", name, strerror(errno));
                return;
            }
            target[n] = '\0';
            printf("%s: symbolic link to %s\n", name, target);
            return;
        }
    }
    const char *type = S_ISDIR(st.st_mode) ? "directory" : S_ISFIFO(st.st_mode) ? "fifo"
        : S_ISSOCK(st.st_mode) ? "socket" : S_ISBLK(st.st_mode) ? "block special"
        : S_ISCHR(st.st_mode) ? "character special" : NULL;
    if (type && !(std && S_ISFIFO(st.st_mode))) {
        printf("%s: %s\n", name, type);
        return;
    }
    if (!type && !S_ISREG(st.st_mode)) {
        printf("%s: unknown file type\n", name);
        return;
    }
    if (opt_i) {
        printf("%s: regular file\n", name);
        return;
    }
    if (!std && (fd = open(name, O_RDONLY | O_NONBLOCK)) < 0) {
        printf("%s: cannot open (%s)\n", name, strerror(errno));
        return;
    }
    cur_fd = fd;
    cur_reg = S_ISREG(st.st_mode);
    cur_size = st.st_size;
    blen = 0;
    while (blen < (size_t)BUFSIZE) {
        ssize_t r = read(fd, buf + blen, (size_t)BUFSIZE - blen);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            printf("%s: cannot open (%s)\n", name, strerror(errno));
            if (!std)
                close(fd);
            return;
        }
        if (r == 0)
            break;
        blen += (size_t)r;
    }
    if (!cur_reg)
        cur_size = (off_t)blen;
    if (blen == 0) {
        printf("%s: empty\n", name);
    } else {
        bool found = false;
        for (size_t i = 0; i < nsources && !found; i++)
            found = sources[i].tests ? run_source(&sources[i]) : defaults_position();
        if (!found && defaults_ctx)
            defaults_context();
        if (!found && !defaults_ctx)
            emits("data");
        printf("%s: %s\n", name, out);
    }
    if (!std)
        close(fd);
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    bool opt_d = false, opt_M = false, any = false;
    int c;
    while ((c = getopt(argc, argv, "dhiM:m:")) != -1) {
        switch (c) {
        case 'd':
            opt_d = any = true;
            add_defaults();
            break;
        case 'h': opt_h = true; break;
        case 'i': opt_i = true; break;
        case 'M':
            opt_M = any = true;
            read_magic(optarg);
            break;
        case 'm':
            any = true;
            read_magic(optarg);
            break;
        default: usage();
        }
    }
    if (optind == argc || (opt_i && any))
        usage();
    if (!opt_M && !opt_d)
        add_defaults();         /* after the -m files, or alone */
    defaults_ctx = opt_d || !opt_M;
    buf = xmalloc(BUFSIZE);
    for (int i = optind; i < argc; i++)
        classify(argv[i]);
    if (fflush(stdout) != 0) {
        fprintf(stderr, "file: standard output: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
