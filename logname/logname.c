/*
 * logname.c - logname: return the user's login name (POSIX.1-2024, XCU
 * logname)
 *
 * Synopsis: logname
 *
 * The name getlogin() gives; when it fails, a diagnostic and status 1.
 * There are no options; a first "--" is discarded.
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
        fprintf(stderr, "usage: logname\n");
        return 2;
    }
    errno = 0;
    const char *name = getlogin();
    if (!name) {
        fprintf(stderr, "logname: no login name%s%s\n", errno ? ": " : "", errno ? strerror(errno) : "");
        return 1;
    }
    if (printf("%s\n", name) < 0 || fflush(stdout) != 0) {
        fprintf(stderr, "logname: write error: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
