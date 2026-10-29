/*
 * cmp.c - cmp: compare two files (POSIX.1-2024, XCU cmp)
 *
 * Synopsis: cmp [-l|-s] file1 file2
 *
 * The files are read in blocks; byte and line numbers are kept in
 * uintmax_t, so files of any size can be compared.
 *
 * Choices where the standard leaves room:
 *  - -l and -s together are a usage error (exit status 2).
 *  - The additional information after "cmp: EOF on FILE" is " after byte
 *    N, line L": the bytes and the lines (counted by newlines, plus one
 *    for a last partial line) of the shorter file; for an empty file it
 *    is " (empty)".  With -l the message is written also when the files
 *    differ before the end of the shorter one.
 *  - With -s, diagnostics for errors are still written to standard error.
 *  - When both operands are "-", or name the same file, the files are
 *    identical.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { BUFSZ = 1 << 16 };

struct input {
    const char *name;
    int fd;
    unsigned char buf[BUFSZ];
    size_t pos, len;
    bool eof;
};

static struct input in[2];

/* the next byte, or -1 at the end; -2 on an error */
static int next(struct input *f)
{
    if (f->pos == f->len) {
        if (f->eof)
            return -1;
        ssize_t n;
        do
            n = read(f->fd, f->buf, BUFSZ);
        while (n < 0 && errno == EINTR);
        if (n < 0) {
            fprintf(stderr, "cmp: %s: %s\n", f->name, strerror(errno));
            return -2;
        }
        if (n == 0) {
            f->eof = true;
            return -1;
        }
        f->pos = 0;
        f->len = (size_t)n;
    }
    return f->buf[f->pos++];
}

static void usage(void)
{
    fprintf(stderr, "usage: cmp [-l|-s] file1 file2\n");
    exit(2);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool opt_l = false, opt_s = false;
    int c;
    while ((c = getopt(argc, argv, "ls")) != -1) {
        switch (c) {
        case 'l': opt_l = true; break;
        case 's': opt_s = true; break;
        default: usage();
        }
    }
    if (opt_l && opt_s) {
        fprintf(stderr, "cmp: -l and -s cannot be used together\n");
        usage();
    }
    if (argc - optind != 2)
        usage();
    struct stat st[2];
    for (int i = 0; i < 2; i++) {
        const char *name = argv[optind + i];
        in[i].name = name;
        if (strcmp(name, "-") == 0) {
            in[i].fd = 0;
        } else if ((in[i].fd = open(name, O_RDONLY)) < 0) {
            fprintf(stderr, "cmp: %s: %s\n", name, strerror(errno));
            return 2;
        }
        if (fstat(in[i].fd, &st[i]) != 0) {
            fprintf(stderr, "cmp: %s: %s\n", name, strerror(errno));
            return 2;
        }
    }
    if (in[0].fd == in[1].fd ||
        (st[0].st_dev == st[1].st_dev && st[0].st_ino == st[1].st_ino && S_ISREG(st[0].st_mode)))
        return 0;

    uintmax_t byte = 0, line = 1;
    bool differ = false, partial = false;
    for (;;) {
        int a = next(&in[0]), b = next(&in[1]);
        if (a == -2 || b == -2)
            return 2;
        if (a < 0 || b < 0) {
            if (a < 0 && b < 0)
                break;
            /* one file ended */
            if (!opt_s) {
                const struct input *s = a < 0 ? &in[0] : &in[1];
                if (fflush(stdout) != 0) {
                    fprintf(stderr, "cmp: write error: %s\n", strerror(errno));
                    return 2;
                }
                if (byte == 0)
                    fprintf(stderr, "cmp: EOF on %s (empty)\n", s->name);
                else
                    fprintf(stderr, "cmp: EOF on %s after byte %ju, line %ju\n", s->name, byte,
                            partial ? line : line - 1);
            }
            differ = true;
            break;
        }
        byte++;
        if (a != b) {
            differ = true;
            if (opt_s)
                break;
            if (!opt_l) {
                if (printf("%s %s differ: char %ju, line %ju\n", in[0].name, in[1].name, byte, line) < 0)
                    return 2;
                break;
            }
            if (printf("%ju %o %o\n", byte, (unsigned)a, (unsigned)b) < 0)
                return 2;
        }
        if (a == '\n') {
            line++;
            partial = false;
        } else
            partial = true;
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "cmp: write error: %s\n", strerror(errno));
        return 2;
    }
    return differ ? 1 : 0;
}
