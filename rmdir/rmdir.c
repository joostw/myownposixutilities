/*
 * rmdir.c - rmdir: remove directories (POSIX.1-2024, XCU rmdir)
 *
 * Synopsis: rmdir [-p] dir...
 *
 * -p: after dir, its parent directories as named in the operand are
 * removed, as "rmdir -p $(dirname dir)" would, until one that is a single
 * component has been removed (or a removal fails, which is reported).
 *
 * Choices where the standard leaves room:
 *  - -p stops at "." and "/", which dirname gives for the last step; they
 *    are not removed.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int status;

static bool remove_dir(const char *d)
{
    if (rmdir(d) != 0) {
        fprintf(stderr, "rmdir: %s: %s\n", d, strerror(errno));
        status = 1;
        return false;
    }
    return true;
}

/* dirname of p, in place: false if it is "." or "/" (nothing more to do) */
static bool parent(char *p)
{
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/')
        n--;
    while (n > 0 && p[n - 1] != '/')
        n--;
    if (n == 0)
        return false;           /* a single component: dirname is "." */
    while (n > 1 && p[n - 1] == '/')
        n--;
    p[n] = '\0';
    return strcmp(p, "/") != 0;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool opt_p = false;
    int c;
    while ((c = getopt(argc, argv, "p")) != -1) {
        if (c == 'p')
            opt_p = true;
        else {
            fprintf(stderr, "usage: rmdir [-p] dir...\n");
            return 2;
        }
    }
    if (optind == argc) {
        fprintf(stderr, "usage: rmdir [-p] dir...\n");
        return 2;
    }
    for (int i = optind; i < argc; i++) {
        if (!remove_dir(argv[i]) || !opt_p)
            continue;
        char *p = strdup(argv[i]);
        if (!p) {
            fprintf(stderr, "rmdir: out of memory\n");
            return 1;
        }
        while (parent(p) && remove_dir(p))
            ;
        free(p);
    }
    return status;
}
