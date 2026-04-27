/*
 * tee — duplicate standard input  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis: tee [-ai] [file...]
 */

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int had_error;

static void write_all(int fd, const char *name, const char *buf, size_t n)
{
    size_t written = 0;
    while (written < n) {
        ssize_t w = write(fd, buf + written, n - written);
        if (w == -1) {
            fprintf(stderr, "tee: %s: %s\n", name, strerror(errno));
            had_error = 1;
            return;
        }
        written += (size_t)w;
    }
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    bool opt_a = false;

    int opt;
    while ((opt = getopt(argc, argv, "ai")) != -1) {
        switch (opt) {
        case 'a': opt_a = true;                break;
        case 'i': signal(SIGINT, SIG_IGN);     break;
        default:
            fprintf(stderr, "usage: tee [-ai] [file...]\n");
            return 1;
        }
    }
    argc -= optind;
    argv += optind;

    int oflags = O_WRONLY | O_CREAT | (opt_a ? O_APPEND : O_TRUNC);

    int *fds = NULL;
    if (argc > 0) {
        fds = malloc((size_t)argc * sizeof(int));
        if (!fds) {
            fprintf(stderr, "tee: out of memory\n");
            return 1;
        }
        for (int i = 0; i < argc; i++) {
            fds[i] = open(argv[i], oflags, 0666);
            if (fds[i] == -1) {
                fprintf(stderr, "tee: %s: %s\n", argv[i], strerror(errno));
                had_error = 1;
            }
        }
    }

    char buf[8192];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof buf)) > 0) {
        write_all(STDOUT_FILENO, "stdout", buf, (size_t)n);
        for (int i = 0; i < argc; i++) {
            if (fds[i] != -1)
                write_all(fds[i], argv[i], buf, (size_t)n);
        }
    }
    if (n == -1) {
        fprintf(stderr, "tee: read error: %s\n", strerror(errno));
        had_error = 1;
    }

    for (int i = 0; i < argc; i++)
        if (fds[i] != -1)
            close(fds[i]);
    free(fds);

    return had_error ? 1 : 0;
}
