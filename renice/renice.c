/*
 * renice.c - renice: set nice values of running processes (POSIX.1-2024,
 * XCU renice)
 *
 * Synopsis: renice [-g|-p|-u] -n increment ID...
 *
 * For each ID, the nice value getpriority() gives (for PRIO_PROCESS,
 * PRIO_PGRP or PRIO_USER) plus the increment, kept within -NZERO and
 * NZERO - 1, is set with setpriority().
 *
 * Choices where the standard leaves room:
 *  - The options are read by hand (renice is excepted from Guideline 9):
 *    -g, -p and -u apply to the IDs that follow them, until the next of
 *    them, as historical renice does; -n may come anywhere before the
 *    end.  "--" ends the options.
 *  - For a process group or a user the value getpriority() gives (that of
 *    the process with the lowest nice value) is the one incremented, and
 *    setpriority() sets the result for all the processes.
 *  - An ID that is not valid, a process (group, user) that does not exist,
 *    and a change that is not allowed are reported, and the others are
 *    still done; the exit status is then 1.  Usage errors have status 2.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#ifndef NZERO
#define NZERO 20
#endif

static void usage(void)
{
    fprintf(stderr, "usage: renice [-g|-p|-u] -n increment ID...\n");
    exit(2);
}

/* an unsigned decimal integer; false if s is not one */
static bool udecimal(const char *s, unsigned long *v)
{
    if (!*s || strspn(s, "0123456789") != strlen(s))
        return false;
    errno = 0;
    *v = strtoul(s, NULL, 10);
    return errno == 0;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    /* the first pass: the increment, and the syntax */
    bool have_n = false;
    long incr = 0;
    int nids = 0;
    bool opts = true;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (opts && strcmp(a, "--") == 0) {
            opts = false;
            continue;
        }
        if (opts && a[0] == '-' && a[1]) {
            for (const char *p = a + 1; *p; p++) {
                if (*p == 'g' || *p == 'p' || *p == 'u')
                    continue;
                if (*p != 'n') {
                    fprintf(stderr, "renice: -%c: unknown option\n", *p);
                    usage();
                }
                const char *arg = p[1] ? p + 1 : argv[++i];
                if (!arg) {
                    fprintf(stderr, "renice: -n: an increment is needed\n");
                    usage();
                }
                const char *q = arg;
                if (*q == '-' || *q == '+')
                    q++;
                char *end;
                errno = 0;
                incr = strtol(arg, &end, 10);
                if (!*q || strspn(q, "0123456789") != strlen(q) || errno) {
                    fprintf(stderr, "renice: -n %s: not a valid increment\n", arg);
                    return 2;
                }
                have_n = true;
                break;
            }
            continue;
        }
        nids++;
    }
    if (!have_n || nids == 0)
        usage();
    if (incr > 2 * NZERO)
        incr = 2 * NZERO;
    if (incr < -2 * NZERO)
        incr = -2 * NZERO;

    /* the second pass: the IDs */
    int which = PRIO_PROCESS, status = 0;
    opts = true;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (opts && strcmp(a, "--") == 0) {
            opts = false;
            continue;
        }
        if (opts && a[0] == '-' && a[1]) {
            for (const char *p = a + 1; *p; p++) {
                if (*p == 'g')
                    which = PRIO_PGRP;
                else if (*p == 'p')
                    which = PRIO_PROCESS;
                else if (*p == 'u')
                    which = PRIO_USER;
                else {          /* -n */
                    if (!p[1])
                        i++;
                    break;
                }
            }
            continue;
        }
        unsigned long v;
        id_t who;
        if (which == PRIO_USER) {
            struct passwd *pw = getpwnam(a);
            if (pw)
                who = (id_t)pw->pw_uid;
            else if (udecimal(a, &v))
                who = (id_t)v;
            else {
                fprintf(stderr, "renice: %s: no such user\n", a);
                status = 1;
                continue;
            }
        } else if (udecimal(a, &v) && v <= INT_MAX)
            who = (id_t)v;
        else {
            fprintf(stderr, "renice: %s: not a %s\n", a,
                    which == PRIO_PGRP ? "process group ID" : "process ID");
            status = 1;
            continue;
        }
        errno = 0;
        int cur = getpriority(which, who);
        if (cur == -1 && errno) {
            fprintf(stderr, "renice: %s: %s\n", a, strerror(errno));
            status = 1;
            continue;
        }
        long nv = cur + incr;
        if (nv < -NZERO)
            nv = -NZERO;
        if (nv > NZERO - 1)
            nv = NZERO - 1;
        if (setpriority(which, who, (int)nv) != 0) {
            fprintf(stderr, "renice: %s: %s\n", a, strerror(errno));
            status = 1;
        }
    }
    return status;
}
