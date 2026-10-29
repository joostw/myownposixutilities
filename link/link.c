/*
 * link.c - link: call the link function (POSIX.1-2024, XCU link) [XSI]
 *
 * Synopsis: link file1 file2
 *
 * Calls link(file1, file2).  There are no options; a first "--" is
 * discarded (XCU 1.4), so that operands starting with '-' can be given.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void usage(void)
{
    fprintf(stderr, "usage: link file1 file2\n");
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    if (getopt(argc, argv, "") != -1) {
        usage();
        return 1;
    }
    argc -= optind;
    argv += optind;
    if (argc != 2) {
        usage();
        return 1;
    }
    if (link(argv[0], argv[1]) != 0) {
        fprintf(stderr, "link: %s %s: %s\n", argv[0], argv[1], strerror(errno));
        return 1;
    }
    return 0;
}
