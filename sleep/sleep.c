/*
 * sleep — suspend execution for an interval  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis: sleep time
 *
 * time is a non-negative decimal integer (seconds).
 * SIGALRM: effectively ignored (sleep returns early, we exit 0).
 */

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    if (argc != 2) {
        fprintf(stderr, "usage: sleep time\n");
        return 1;
    }

    char *end;
    errno = 0;
    long t = strtol(argv[1], &end, 10);
    if (errno != 0 || *end != '\0' || t < 0 || *argv[1] == '\0') {
        fprintf(stderr, "sleep: invalid time interval: '%s'\n", argv[1]);
        return 1;
    }

    /* Install a no-op SIGALRM handler so an incoming SIGALRM causes sleep()
     * to return early rather than kill the process; we then exit 0. */
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    sleep((unsigned)t);
    return 0;
}
