/*
 * cat — concatenate and print files  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis: cat [-u] [file...]
 */

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int had_error;

static void cat_stream(FILE *in, const char *name)
{
    char buf[4096];
    size_t n;

    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, stdout) != n) {
            fprintf(stderr, "cat: write error\n");
            had_error = 1;
            return;
        }
    }
    if (ferror(in)) {
        fprintf(stderr, "cat: %s: read error\n", name);
        had_error = 1;
    }
}

static void cat_file(const char *path)
{
    if (strcmp(path, "-") == 0) {
        cat_stream(stdin, "stdin");
        return;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        had_error = 1;
        return;
    }
    cat_stream(f, path);
    fclose(f);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    int opt;
    while ((opt = getopt(argc, argv, "u")) != -1) {
        switch (opt) {
        case 'u':
            setvbuf(stdout, NULL, _IONBF, 0);
            break;
        default:
            fprintf(stderr, "usage: cat [-u] [file...]\n");
            return 1;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc == 0) {
        cat_stream(stdin, "stdin");
    } else {
        for (int i = 0; i < argc; i++)
            cat_file(argv[i]);
    }

    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "cat: write error\n");
        had_error = 1;
    }

    return had_error ? 1 : 0;
}
