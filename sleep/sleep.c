/*
 * sleep.c - sleep: suspend execution for an interval (POSIX.1-2024, XCU sleep)
 *
 * Synopsis: sleep time
 *
 * time is a non-negative decimal integer (seconds); any number of
 * digits is accepted, and values too large to represent sleep for the
 * longest interval that can be represented.
 * SIGALRM is effectively ignored (one of the three permitted actions);
 * other signals take their standard action.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    /* no options, but "--" as the first argument is discarded (XCU 1.4) */
    if (argc > 1 && strcmp(argv[1], "--") == 0) {
        argv++;
        argc--;
    }
    if (argc != 2) {
        fprintf(stderr, "usage: sleep time\n");
        return 1;
    }

    /* a decimal integer: digits only (no sign, no blanks) */
    const char *p = argv[1];
    time_t secs = 0;
    const time_t max = (time_t)((((unsigned long long)1 << (sizeof(time_t) * CHAR_BIT - 2)) - 1) * 2 + 1);
    if (!*p) {
        fprintf(stderr, "sleep: invalid time interval: '%s'\n", argv[1]);
        return 1;
    }
    for (; *p; p++) {
        if (*p < '0' || *p > '9') {
            fprintf(stderr, "sleep: invalid time interval: '%s'\n", argv[1]);
            return 1;
        }
        if (secs > (max - (*p - '0')) / 10)
            secs = max;                 /* saturate */
        else
            secs = secs * 10 + (*p - '0');
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    /* nanosleep, resumed with the time remaining if a signal interrupts it */
    struct timespec req = { secs, 0 }, rem;
    while (nanosleep(&req, &rem) != 0) {
        if (errno != EINTR) {
            perror("sleep");
            return 1;
        }
        req = rem;
    }
    return 0;
}
