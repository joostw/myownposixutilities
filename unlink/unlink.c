/*
 * unlink — call the unlink function  (SUSv5 / IEEE Std 1003.1-2024, XSI)
 *
 * Synopsis: unlink file
 *
 * Calls unlink(file) and exits 0 on success, >0 on error.
 * No options are defined; -- is accepted as the end-of-options delimiter
 * per XBD 12.2 Utility Syntax Guidelines.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void usage(void)
{
    fprintf(stderr, "usage: unlink file\n");
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    int opt;
    while ((opt = getopt(argc, argv, "")) != -1) {
        (void)opt;   /* getopt already wrote "illegal option" to stderr */
        usage();
        return 1;
    }
    argc -= optind;
    argv += optind;

    if (argc != 1) {
        usage();
        return 1;
    }

    if (unlink(argv[0]) != 0) {
        fprintf(stderr, "unlink: %s: %s\n", argv[0], strerror(errno));
        return 1;
    }

    return 0;
}
