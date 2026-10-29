/*
 * dd.c - dd: convert and copy a file (POSIX.1-2024, XCU dd)
 *
 * Synopsis:
 *   dd [operand...]
 *
 * Operands: if=file of=file ibs=expr obs=expr bs=expr cbs=expr skip=n
 * seek=n count=n conv=value[,value...] iflags=fullblock, where conv
 * takes ascii, ebcdic, ibm (XSI), block, unblock, lcase, ucase, swab,
 * noerror, notrunc and sync.  An expr is a positive decimal number,
 * optionally followed by k (1024) or b (512), or several of them joined
 * by x for their product.
 *
 * Input blocks go through the steps of the standard in order: sync
 * padding, then (unless bs= alone copies block by block) swab, ascii,
 * lcase or ucase, block or unblock, ebcdic or ibm, and the result is
 * gathered into output blocks of obs bytes.  The counts are written to
 * standard error at the end, after an input error with noerror, on
 * SIGINT (which then ends dd as by its default action) and on SIGUSR1
 * (after which the copy goes on).
 *
 * Choices where the standard leaves room:
 *  - Operands given more than once: the last one counts (conv= adds up).
 *  - skip=, seek= and count= also take the expr forms; 0 is allowed,
 *    and count=0 copies nothing.
 *  - iflag=fullblock is accepted as well as iflags=fullblock.
 *  - cbs=0 (the default) with block or unblock: records are not
 *    converted, but sync still pads with spaces; ascii, ebcdic and ibm
 *    without cbs= just translate.
 *  - swab works within each input block, as the standard says: the odd
 *    last byte of a block stays where it is.
 *  - At the end of the input, an unfinished unblock record is written
 *    like a full one: trailing spaces removed and a newline added.
 *  - Skipping past the end of the input is not an error; a warning is
 *    written and there is nothing to copy.
 *  - seek= on output that cannot seek writes seek * obs null bytes.
 *  - The output is truncated (when of= is given and notrunc is not) with
 *    ftruncate(); a failure counts only for a regular file.
 *  - With noerror, a block that fails to read counts as a partial input
 *    block when sync is given (it becomes ibs null bytes), and is just
 *    left out otherwise; dd then seeks past it, if it can.
 *  - Usage errors (unknown or bad operands, conflicting conv values)
 *    exit with status 2; other errors with status 1.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* the tables of the standard (XSI): ASCII to EBCDIC, ASCII to IBM EBCDIC,
   and EBCDIC to ASCII, the inverse of the first */
static const unsigned char to_ebcdic[256] = {
    0000, 0001, 0002, 0003, 0067, 0055, 0056, 0057,
    0026, 0005, 0045, 0013, 0014, 0015, 0016, 0017,
    0020, 0021, 0022, 0023, 0074, 0075, 0062, 0046,
    0030, 0031, 0077, 0047, 0034, 0035, 0036, 0037,
    0100, 0132, 0177, 0173, 0133, 0154, 0120, 0175,
    0115, 0135, 0134, 0116, 0153, 0140, 0113, 0141,
    0360, 0361, 0362, 0363, 0364, 0365, 0366, 0367,
    0370, 0371, 0172, 0136, 0114, 0176, 0156, 0157,
    0174, 0301, 0302, 0303, 0304, 0305, 0306, 0307,
    0310, 0311, 0321, 0322, 0323, 0324, 0325, 0326,
    0327, 0330, 0331, 0342, 0343, 0344, 0345, 0346,
    0347, 0350, 0351, 0255, 0340, 0275, 0232, 0155,
    0171, 0201, 0202, 0203, 0204, 0205, 0206, 0207,
    0210, 0211, 0221, 0222, 0223, 0224, 0225, 0226,
    0227, 0230, 0231, 0242, 0243, 0244, 0245, 0246,
    0247, 0250, 0251, 0300, 0117, 0320, 0137, 0007,
    0040, 0041, 0042, 0043, 0044, 0025, 0006, 0027,
    0050, 0051, 0052, 0053, 0054, 0011, 0012, 0033,
    0060, 0061, 0032, 0063, 0064, 0065, 0066, 0010,
    0070, 0071, 0072, 0073, 0004, 0024, 0076, 0341,
    0101, 0102, 0103, 0104, 0105, 0106, 0107, 0110,
    0111, 0121, 0122, 0123, 0124, 0125, 0126, 0127,
    0130, 0131, 0142, 0143, 0144, 0145, 0146, 0147,
    0150, 0151, 0160, 0161, 0162, 0163, 0164, 0165,
    0166, 0167, 0170, 0200, 0212, 0213, 0214, 0215,
    0216, 0217, 0220, 0152, 0233, 0234, 0235, 0236,
    0237, 0240, 0252, 0253, 0254, 0112, 0256, 0257,
    0260, 0261, 0262, 0263, 0264, 0265, 0266, 0267,
    0270, 0271, 0272, 0273, 0274, 0241, 0276, 0277,
    0312, 0313, 0314, 0315, 0316, 0317, 0332, 0333,
    0334, 0335, 0336, 0337, 0352, 0353, 0354, 0355,
    0356, 0357, 0372, 0373, 0374, 0375, 0376, 0377,
};
static const unsigned char to_ibm[256] = {
    0000, 0001, 0002, 0003, 0067, 0055, 0056, 0057,
    0026, 0005, 0045, 0013, 0014, 0015, 0016, 0017,
    0020, 0021, 0022, 0023, 0074, 0075, 0062, 0046,
    0030, 0031, 0077, 0047, 0034, 0035, 0036, 0037,
    0100, 0132, 0177, 0173, 0133, 0154, 0120, 0175,
    0115, 0135, 0134, 0116, 0153, 0140, 0113, 0141,
    0360, 0361, 0362, 0363, 0364, 0365, 0366, 0367,
    0370, 0371, 0172, 0136, 0114, 0176, 0156, 0157,
    0174, 0301, 0302, 0303, 0304, 0305, 0306, 0307,
    0310, 0311, 0321, 0322, 0323, 0324, 0325, 0326,
    0327, 0330, 0331, 0342, 0343, 0344, 0345, 0346,
    0347, 0350, 0351, 0255, 0340, 0275, 0137, 0155,
    0171, 0201, 0202, 0203, 0204, 0205, 0206, 0207,
    0210, 0211, 0221, 0222, 0223, 0224, 0225, 0226,
    0227, 0230, 0231, 0242, 0243, 0244, 0245, 0246,
    0247, 0250, 0251, 0300, 0117, 0320, 0241, 0007,
    0040, 0041, 0042, 0043, 0044, 0025, 0006, 0027,
    0050, 0051, 0052, 0053, 0054, 0011, 0012, 0033,
    0060, 0061, 0032, 0063, 0064, 0065, 0066, 0010,
    0070, 0071, 0072, 0073, 0004, 0024, 0076, 0341,
    0101, 0102, 0103, 0104, 0105, 0106, 0107, 0110,
    0111, 0121, 0122, 0123, 0124, 0125, 0126, 0127,
    0130, 0131, 0142, 0143, 0144, 0145, 0146, 0147,
    0150, 0151, 0160, 0161, 0162, 0163, 0164, 0165,
    0166, 0167, 0170, 0200, 0212, 0213, 0214, 0215,
    0216, 0217, 0220, 0232, 0233, 0234, 0235, 0236,
    0237, 0240, 0252, 0253, 0254, 0255, 0256, 0257,
    0260, 0261, 0262, 0263, 0264, 0265, 0266, 0267,
    0270, 0271, 0272, 0273, 0274, 0275, 0276, 0277,
    0312, 0313, 0314, 0315, 0316, 0317, 0332, 0333,
    0334, 0335, 0336, 0337, 0352, 0353, 0354, 0355,
    0356, 0357, 0372, 0373, 0374, 0375, 0376, 0377,
};
static const unsigned char to_ascii[256] = {
    0000, 0001, 0002, 0003, 0234, 0011, 0206, 0177,
    0227, 0215, 0216, 0013, 0014, 0015, 0016, 0017,
    0020, 0021, 0022, 0023, 0235, 0205, 0010, 0207,
    0030, 0031, 0222, 0217, 0034, 0035, 0036, 0037,
    0200, 0201, 0202, 0203, 0204, 0012, 0027, 0033,
    0210, 0211, 0212, 0213, 0214, 0005, 0006, 0007,
    0220, 0221, 0026, 0223, 0224, 0225, 0226, 0004,
    0230, 0231, 0232, 0233, 0024, 0025, 0236, 0032,
    0040, 0240, 0241, 0242, 0243, 0244, 0245, 0246,
    0247, 0250, 0325, 0056, 0074, 0050, 0053, 0174,
    0046, 0251, 0252, 0253, 0254, 0255, 0256, 0257,
    0260, 0261, 0041, 0044, 0052, 0051, 0073, 0176,
    0055, 0057, 0262, 0263, 0264, 0265, 0266, 0267,
    0270, 0271, 0313, 0054, 0045, 0137, 0076, 0077,
    0272, 0273, 0274, 0275, 0276, 0277, 0300, 0301,
    0302, 0140, 0072, 0043, 0100, 0047, 0075, 0042,
    0303, 0141, 0142, 0143, 0144, 0145, 0146, 0147,
    0150, 0151, 0304, 0305, 0306, 0307, 0310, 0311,
    0312, 0152, 0153, 0154, 0155, 0156, 0157, 0160,
    0161, 0162, 0136, 0314, 0315, 0316, 0317, 0320,
    0321, 0345, 0163, 0164, 0165, 0166, 0167, 0170,
    0171, 0172, 0322, 0323, 0324, 0133, 0326, 0327,
    0330, 0331, 0332, 0333, 0334, 0335, 0336, 0337,
    0340, 0341, 0342, 0343, 0344, 0135, 0346, 0347,
    0173, 0101, 0102, 0103, 0104, 0105, 0106, 0107,
    0110, 0111, 0350, 0351, 0352, 0353, 0354, 0355,
    0175, 0112, 0113, 0114, 0115, 0116, 0117, 0120,
    0121, 0122, 0356, 0357, 0360, 0361, 0362, 0363,
    0134, 0237, 0123, 0124, 0125, 0126, 0127, 0130,
    0131, 0132, 0364, 0365, 0366, 0367, 0370, 0371,
    0060, 0061, 0062, 0063, 0064, 0065, 0066, 0067,
    0070, 0071, 0372, 0373, 0374, 0375, 0376, 0377,
};

enum {
    C_ASCII = 1 << 0, C_EBCDIC = 1 << 1, C_IBM = 1 << 2, C_BLOCK = 1 << 3,
    C_UNBLOCK = 1 << 4, C_LCASE = 1 << 5, C_UCASE = 1 << 6, C_SWAB = 1 << 7,
    C_NOERROR = 1 << 8, C_NOTRUNC = 1 << 9, C_SYNC = 1 << 10
};
static const struct { const char *name; unsigned flag; } convs[] = {
    { "ascii", C_ASCII }, { "ebcdic", C_EBCDIC }, { "ibm", C_IBM },
    { "block", C_BLOCK }, { "unblock", C_UNBLOCK }, { "lcase", C_LCASE },
    { "ucase", C_UCASE }, { "swab", C_SWAB }, { "noerror", C_NOERROR },
    { "notrunc", C_NOTRUNC }, { "sync", C_SYNC },
};

static const char *iname, *oname;   /* NULL: the standard input/output */
static int ifd = 0, ofd = 1;
static size_t ibs = 512, obs = 512, cbs;
static bool bs_given, fullblock;
static uintmax_t skip, seek, count;
static bool count_given;
static unsigned conv;

static unsigned char *ibuf, *obuf, *rec;
static size_t olen;                 /* bytes in obuf */
static size_t col;                  /* position in the current record */
static bool truncating;             /* the current block record is too long */
static const unsigned char *outtab; /* ebcdic or ibm: translate on output */

static uintmax_t in_whole, in_part, out_whole, out_part, truncated;
static volatile sig_atomic_t got_int, got_usr1;

static void usage(void)
{
    fprintf(stderr, "usage: dd [operand...]\n");
    exit(2);
}

static void status(void)
{
    fprintf(stderr, "%ju+%ju records in\n%ju+%ju records out\n",
            in_whole, in_part, out_whole, out_part);
    if (truncated)
        fprintf(stderr, "%ju truncated %s\n", truncated,
                truncated == 1 ? "record" : "records");
}

static void catcher(int sig)
{
    if (sig == SIGINT)
        got_int = 1;
    else
        got_usr1 = 1;
}

/* act on a signal caught since the last look */
static void signals(void)
{
    if (got_usr1) {
        got_usr1 = 0;
        status();
    }
    if (got_int) {
        status();
        signal(SIGINT, SIG_DFL);
        raise(SIGINT);
        exit(130);
    }
}

static void fail(const char *name, int err)
{
    fprintf(stderr, "dd: %s: %s\n", name, strerror(err));
}

/* the value of an expr: numbers with k or b, joined by x */
static uintmax_t expr(const char *op, const char *s, uintmax_t max, bool zero)
{
    uintmax_t v = 1;
    const char *p = s;
    for (;;) {
        if (*p < '0' || *p > '9')
            goto bad;
        uintmax_t n = 0;
        for (; *p >= '0' && *p <= '9'; p++) {
            if (n > (UINTMAX_MAX - (unsigned)(*p - '0')) / 10)
                goto big;
            n = n * 10 + (unsigned)(*p - '0');
        }
        uintmax_t m = *p == 'k' ? 1024 : *p == 'b' ? 512 : 1;
        if (m != 1)
            p++;
        if (n && m > UINTMAX_MAX / n)
            goto big;
        n *= m;
        if (n && v > UINTMAX_MAX / n)
            goto big;
        v *= n;
        if (*p == '\0')
            break;
        if (*p++ != 'x')
            goto bad;
    }
    if (v > max)
        goto big;
    if (v == 0 && !zero)
        goto bad;
    return v;
big:
    fprintf(stderr, "dd: %s=%s: value too large\n", op, s);
    exit(2);
bad:
    fprintf(stderr, "dd: %s=%s: invalid number\n", op, s);
    exit(2);
}

static void operands(int argc, char **argv)
{
    size_t bs = 0;
    uintmax_t smax = SIZE_MAX / 2;
    for (int i = 1; i < argc; i++) {
        char *a = argv[i], *v = strchr(a, '=');
        if (v == NULL) {
            fprintf(stderr, "dd: unknown operand %s\n", a);
            usage();
        }
        size_t k = (size_t)(v++ - a);
#define IS(s) (k == strlen(s) && strncmp(a, s, k) == 0)
        if (IS("if"))
            iname = v;
        else if (IS("of"))
            oname = v;
        else if (IS("ibs"))
            ibs = (size_t)expr("ibs", v, smax, false);
        else if (IS("obs"))
            obs = (size_t)expr("obs", v, smax, false);
        else if (IS("bs"))
            bs = (size_t)expr("bs", v, smax, false), bs_given = true;
        else if (IS("cbs"))
            cbs = (size_t)expr("cbs", v, smax, true);
        else if (IS("skip"))
            skip = expr("skip", v, UINTMAX_MAX, true);
        else if (IS("seek"))
            seek = expr("seek", v, UINTMAX_MAX, true);
        else if (IS("count"))
            count = expr("count", v, UINTMAX_MAX, true), count_given = true;
        else if (IS("iflags") || IS("iflag")) {
            if (strcmp(v, "fullblock") != 0) {
                fprintf(stderr, "dd: %s: unknown input flag\n", v);
                usage();
            }
            fullblock = true;
        } else if (IS("conv")) {
            for (char *c = v; ; ) {
                size_t n = strcspn(c, ",");
                size_t j;
                for (j = 0; j < sizeof convs / sizeof convs[0]; j++)
                    if (strlen(convs[j].name) == n && strncmp(c, convs[j].name, n) == 0)
                        break;
                if (j == sizeof convs / sizeof convs[0]) {
                    fprintf(stderr, "dd: %.*s: unknown conversion\n", (int)n, c);
                    usage();
                }
                conv |= convs[j].flag;
                if (c[n] == '\0')
                    break;
                c += n + 1;
            }
        } else {
            fprintf(stderr, "dd: unknown operand %s\n", a);
            usage();
        }
#undef IS
    }
    if (bs_given)
        ibs = obs = bs;
    unsigned x = conv & (C_ASCII | C_EBCDIC | C_IBM);
    if (x & (x - 1)) {
        fprintf(stderr, "dd: ascii, ebcdic and ibm cannot be combined\n");
        usage();
    }
    if (conv & C_ASCII)
        conv |= C_UNBLOCK;
    if (conv & (C_EBCDIC | C_IBM))
        conv |= C_BLOCK;
    if ((conv & (C_BLOCK | C_UNBLOCK)) == (C_BLOCK | C_UNBLOCK)) {
        fprintf(stderr, "dd: block and unblock cannot be combined\n");
        usage();
    }
    if ((conv & (C_LCASE | C_UCASE)) == (C_LCASE | C_UCASE)) {
        fprintf(stderr, "dd: lcase and ucase cannot be combined\n");
        usage();
    }
    /* bs= alone: each input block is an output block */
    if (conv & ~(C_SYNC | C_NOERROR | C_NOTRUNC))
        bs_given = false;
    outtab = conv & C_EBCDIC ? to_ebcdic : conv & C_IBM ? to_ibm : NULL;
}

/* write all of buf, as one output block */
static void write_block(const unsigned char *buf, size_t n)
{
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(ofd, buf + done, n - done);
        if (w < 0) {
            if (errno == EINTR) {
                signals();
                continue;
            }
            fail(oname ? oname : "standard output", errno);
            status();
            exit(1);
        }
        done += (size_t)w;
        signals();
    }
    if (n == obs)
        out_whole++;
    else
        out_part++;
}

/* gather bytes into output blocks, translated for ebcdic or ibm */
static void put(const unsigned char *p, size_t n)
{
    while (n > 0) {
        size_t k = obs - olen < n ? obs - olen : n;
        if (outtab)
            for (size_t i = 0; i < k; i++)
                obuf[olen + i] = outtab[p[i]];
        else
            memcpy(obuf + olen, p, k);
        olen += k, p += k, n -= k;
        if (olen == obs) {
            write_block(obuf, olen);
            olen = 0;
        }
    }
}

static void spaces(size_t n)
{
    static const unsigned char sp[64] =
        "                                                                ";
    while (n > 0) {
        size_t k = n < sizeof sp ? n : sizeof sp;
        put(sp, k);
        n -= k;
    }
}

/* block: newline-ended records to records of cbs bytes */
static void block(const unsigned char *p, size_t n)
{
    size_t start = 0;               /* bytes of the record not yet put */
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '\n') {
            put(p + start, i - start);
            spaces(cbs - col);
            col = 0, truncating = false;
            start = i + 1;
        } else if (col < cbs) {
            col++;
        } else {
            put(p + start, i - start);
            start = i + 1;
            if (!truncating)
                truncating = true, truncated++;
        }
    }
    put(p + start, n - start);
}

static void put_record(void)
{
    size_t n = col;
    while (n > 0 && rec[n - 1] == ' ')
        n--;
    put(rec, n);
    put((const unsigned char *)"\n", 1);
    col = 0;
}

/* unblock: records of cbs bytes to lines without trailing spaces */
static void unblock(const unsigned char *p, size_t n)
{
    while (n > 0) {
        size_t k = cbs - col < n ? cbs - col : n;
        memcpy(rec + col, p, k);
        col += k, p += k, n -= k;
        if (col == cbs)
            put_record();
    }
}

/* the conversions after sync, for one input block */
static void convert(unsigned char *b, size_t n)
{
    if (conv & C_SWAB)
        for (size_t i = 0; i + 1 < n; i += 2) {
            unsigned char c = b[i];
            b[i] = b[i + 1], b[i + 1] = c;
        }
    if (conv & C_ASCII)
        for (size_t i = 0; i < n; i++)
            b[i] = to_ascii[b[i]];
    if (conv & C_LCASE)
        for (size_t i = 0; i < n; i++)
            b[i] = (unsigned char)tolower(b[i]);
    if (conv & C_UCASE)
        for (size_t i = 0; i < n; i++)
            b[i] = (unsigned char)toupper(b[i]);
    if ((conv & C_BLOCK) && cbs)
        block(b, n);
    else if ((conv & C_UNBLOCK) && cbs)
        unblock(b, n);
    else
        put(b, n);
}

/* read one input block (several reads with fullblock); -1 on error */
static ssize_t read_block(unsigned char *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(ifd, buf + got, n - got);
        if (r < 0) {
            if (errno == EINTR) {
                signals();
                continue;
            }
            if (got > 0 && fullblock)
                return (ssize_t)got;
            return -1;
        }
        signals();
        if (r == 0)
            break;
        got += (size_t)r;
        if (!fullblock)
            break;
    }
    return (ssize_t)got;
}

static off_t off_max(void)
{
    return (off_t)(((uintmax_t)1 << (sizeof(off_t) * CHAR_BIT - 1)) - 1);
}

/* an input error: false to give up */
static bool input_error(int err)
{
    fail(iname ? iname : "standard input", err);
    if (!(conv & C_NOERROR))
        return false;
    status();
    if (lseek(ifd, (off_t)ibs, SEEK_CUR) < 0) {
        /* not seekable: the next read goes on where it can */
    }
    return true;
}

static void skip_input(void)
{
    if (skip == 0)
        return;
    struct stat st;
    if (skip <= (uintmax_t)off_max() / ibs) {
        off_t off = (off_t)(skip * ibs);
        if (fstat(ifd, &st) == 0 && lseek(ifd, off, SEEK_CUR) >= 0) {
            if (S_ISREG(st.st_mode) && st.st_size < lseek(ifd, 0, SEEK_CUR))
                fprintf(stderr, "dd: %s: cannot skip to specified offset\n",
                        iname ? iname : "standard input");
            return;
        }
    }
    for (uintmax_t i = 0; i < skip; i++) {
        ssize_t r = read_block(ibuf, ibs);
        if (r < 0) {
            if (!input_error(errno)) {
                status();
                exit(1);
            }
            continue;
        }
        if (r == 0) {
            fprintf(stderr, "dd: %s: cannot skip to specified offset\n",
                    iname ? iname : "standard input");
            return;
        }
    }
}

static void seek_output(void)
{
    const char *name = oname ? oname : "standard output";
    if (seek > (uintmax_t)off_max() / obs) {
        fprintf(stderr, "dd: seek=%ju: offset too large\n", seek);
        exit(1);
    }
    off_t off = (off_t)(seek * obs);
    if (oname && !(conv & C_NOTRUNC) && ftruncate(ofd, off) != 0) {
        struct stat st;
        int e = errno;
        if (fstat(ofd, &st) == 0 && S_ISREG(st.st_mode)) {
            fail(name, e);
            exit(1);
        }
    }
    if (seek == 0)
        return;
    if (lseek(ofd, off, SEEK_CUR) >= 0)
        return;
    if (errno != ESPIPE) {
        fail(name, errno);
        exit(1);
    }
    /* cannot seek: write the blocks as null bytes */
    memset(obuf, 0, obs);
    for (uintmax_t i = 0; i < seek; i++) {
        size_t done = 0;
        while (done < obs) {
            ssize_t w = write(ofd, obuf + done, obs - done);
            if (w < 0) {
                if (errno == EINTR) {
                    signals();
                    continue;
                }
                fail(name, errno);
                exit(1);
            }
            done += (size_t)w;
        }
    }
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    if (argc > 1 && strcmp(argv[1], "--") == 0)
        argv++, argc--;
    operands(argc, argv);

    if (iname && (ifd = open(iname, O_RDONLY)) < 0) {
        fail(iname, errno);
        return 1;
    }
    if (oname && (ofd = open(oname, O_WRONLY | O_CREAT, 0666)) < 0) {
        fail(oname, errno);
        return 1;
    }
    ibuf = malloc(ibs);
    obuf = malloc(obs);
    rec = malloc(cbs ? cbs : 1);
    if (ibuf == NULL || obuf == NULL || rec == NULL) {
        fprintf(stderr, "dd: %s\n", strerror(ENOMEM));
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                /* no SA_RESTART: reads see EINTR */
    struct sigaction old;
    if (sigaction(SIGINT, NULL, &old) == 0 && old.sa_handler != SIG_IGN)
        sigaction(SIGINT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);

    skip_input();
    seek_output();

    int ret = 0;
    unsigned char pad = conv & (C_BLOCK | C_UNBLOCK) ? ' ' : '\0';
    for (uintmax_t blocks = 0; !count_given || blocks < count; blocks++) {
        ssize_t r = read_block(ibuf, ibs);
        size_t n;
        if (r < 0) {
            if (!input_error(errno)) {
                ret = 1;
                break;
            }
            if (!(conv & C_SYNC))
                continue;
            n = 0;
            in_part++;
        } else if (r == 0) {
            break;
        } else {
            n = (size_t)r;
            if (n == ibs)
                in_whole++;
            else
                in_part++;
        }
        if (n < ibs && (conv & C_SYNC)) {
            memset(ibuf + n, pad, ibs - n);
            n = ibs;
        }
        if (bs_given)
            write_block(ibuf, n);
        else
            convert(ibuf, n);
    }
    /* the last record, and the last output block */
    if ((conv & C_BLOCK) && cbs && (col > 0 || truncating))
        spaces(cbs - col);
    if ((conv & C_UNBLOCK) && cbs && col > 0)
        put_record();
    if (olen > 0)
        write_block(obuf, olen);
    if (oname && close(ofd) != 0) {
        fail(oname, errno);
        ret = 1;
    }
    status();
    return ret;
}
