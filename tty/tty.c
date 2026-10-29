/*
 * tty.c - tty: return user's terminal name (POSIX.1-2024, XCU tty)
 *
 * Synopsis: tty
 *
 * The name ttyname(0) gives, or "not a tty" (exit status 1).  There are no
 * options; a first "--" is discarded.
 *
 * Choices where the standard leaves room:
 *  - A terminal whose name ttyname() cannot find is an error (status 2).
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    if (getopt(argc, argv, "") != -1 || optind < argc) {
        fprintf(stderr, "usage: tty\n");
        return 2;
    }
    int r;
    if (!isatty(0)) {
        puts("not a tty");
        r = 1;
    } else {
        const char *name = ttyname(0);
        if (!name) {
            fprintf(stderr, "tty: %s\n", strerror(errno));
            return 2;
        }
        puts(name);
        r = 0;
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "tty: write error: %s\n", strerror(errno));
        return 2;
    }
    return r;
}
