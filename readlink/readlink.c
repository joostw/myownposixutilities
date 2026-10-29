/*
 * readlink.c - readlink: display the contents of a symbolic link
 * (POSIX.1-2024, XCU readlink)
 *
 * Synopsis: readlink [-n] file
 *
 * The contents are read with readlink() into a buffer that grows until
 * they fit, so links of any length are written whole.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void usage(void)
{
    fprintf(stderr, "usage: readlink [-n] file\n");
    exit(2);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool opt_n = false;
    int c;
    while ((c = getopt(argc, argv, "n")) != -1) {
        if (c == 'n')
            opt_n = true;
        else
            usage();
    }
    if (argc - optind != 1)
        usage();
    const char *file = argv[optind];
    size_t cap = 256;
    char *buf = NULL;
    ssize_t n;
    for (;;) {
        if (!(buf = realloc(buf, cap))) {
            fprintf(stderr, "readlink: out of memory\n");
            return 1;
        }
        n = readlink(file, buf, cap);
        if (n < 0) {
            if (errno == EINVAL)
                fprintf(stderr, "readlink: %s: not a symbolic link\n", file);
            else
                fprintf(stderr, "readlink: %s: %s\n", file, strerror(errno));
            return 1;
        }
        if ((size_t)n < cap)
            break;
        cap *= 2;
    }
    if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n || (!opt_n && putchar('\n') == EOF) ||
        fflush(stdout) != 0) {
        fprintf(stderr, "readlink: write error: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
