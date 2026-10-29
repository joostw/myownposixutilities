/*
 * uuencode.c - uuencode: encode a binary file (POSIX.1-2024, XCU uuencode)
 *
 * Synopsis: uuencode [-m] [file] decode_pathname
 *
 * The historical encoding: "begin MODE NAME", lines of a length character
 * and up to 45 bytes as 60 characters, a line of one '`', "end"; with -m,
 * Base64: "begin-base64 MODE NAME", lines of 76 characters, "====".
 *
 * Choices where the standard leaves room:
 *  - The mode is the file's permission bits in octal (three digits, four
 *    with set-user-ID, set-group-ID or sticky); for the standard input,
 *    the bits of fstat() of a regular file, else 0666 less the mask.
 *  - In the historical encoding the value 0 is written as '`' (0x60), not
 *    as a space, so that no line ends in blanks.
 *  - An encoded line of the historical encoding holds 45 bytes (the last
 *    one fewer); a Base64 line, 57 bytes.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static FILE *in;

static size_t fill(unsigned char *buf, size_t want)
{
    size_t n = 0;
    while (n < want) {
        size_t k = fread(buf + n, 1, want - n, in);
        if (k == 0)
            break;
        n += k;
    }
    return n;
}

#define ENC(c) ((c) ? ((c) & 077) + ' ' : '`')

static void historical(void)
{
    unsigned char buf[45];
    size_t n;
    while ((n = fill(buf, 45)) > 0) {
        putchar(ENC((int)n));
        for (size_t i = 0; i < n; i += 3) {
            int a = buf[i], b = i + 1 < n ? buf[i + 1] : 0, c = i + 2 < n ? buf[i + 2] : 0;
            putchar(ENC(a >> 2));
            putchar(ENC(((a << 4) | ((b >> 4) & 0xF)) & 077));
            putchar(ENC(((b << 2) | ((c >> 6) & 0x3)) & 077));
            putchar(ENC(c & 077));
        }
        putchar('\n');
    }
    puts("`\nend");
}

static void base64(void)
{
    static const char alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char buf[57];
    size_t n;
    while ((n = fill(buf, 57)) > 0) {
        for (size_t i = 0; i < n; i += 3) {
            unsigned v = (unsigned)buf[i] << 16;
            if (i + 1 < n)
                v |= (unsigned)buf[i + 1] << 8;
            if (i + 2 < n)
                v |= buf[i + 2];
            putchar(alpha[v >> 18 & 63]);
            putchar(alpha[v >> 12 & 63]);
            putchar(i + 1 < n ? alpha[v >> 6 & 63] : '=');
            putchar(i + 2 < n ? alpha[v & 63] : '=');
        }
        putchar('\n');
    }
    puts("====");
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool opt_m = false;
    int c;
    while ((c = getopt(argc, argv, "m")) != -1) {
        if (c != 'm') {
            fprintf(stderr, "usage: uuencode [-m] [file] decode_pathname\n");
            return 2;
        }
        opt_m = true;
    }
    int nops = argc - optind;
    if (nops < 1 || nops > 2) {
        fprintf(stderr, "usage: uuencode [-m] [file] decode_pathname\n");
        return 2;
    }
    const char *name = argv[argc - 1];
    in = stdin;
    const char *inname = "standard input";
    if (nops == 2) {
        inname = argv[optind];
        if (!(in = fopen(inname, "rb"))) {
            fprintf(stderr, "uuencode: %s: %s\n", inname, strerror(errno));
            return 1;
        }
    }
    struct stat st;
    mode_t mode;
    if (fstat(fileno(in), &st) == 0 && (nops == 2 || S_ISREG(st.st_mode)))
        mode = st.st_mode & 07777;
    else {
        mode_t mask = umask(0);
        umask(mask);
        mode = 0666 & ~mask;
    }
    printf("%s %03o %s\n", opt_m ? "begin-base64" : "begin", (unsigned)mode, name);
    if (opt_m)
        base64();
    else
        historical();
    if (ferror(in)) {
        fprintf(stderr, "uuencode: %s: %s\n", inname, strerror(errno));
        return 1;
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "uuencode: write error: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
