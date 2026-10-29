/*
 * split.c - split: split a file into pieces (POSIX.1-2024, XCU split)
 *
 * Synopsis:
 *   split [-l line_count] [-a suffix_length] [file [name]]
 *   split -b n[k|m] [-a suffix_length] [file [name]]
 *
 * The pieces are name (x) followed by suffix_length (2) lowercase letters,
 * aa, ab, ...; each is created (or truncated) as it is needed, so an empty
 * input makes none.  When the suffixes run out, the last file gets its
 * full share and split fails (status 1), keeping the files.
 *
 * Choices where the standard leaves room:
 *  - -l 0, -b 0, -a 0 are usage errors (status 2); so are -l with -b.
 *  - The output files are created with mode 0666, less the mask.
 *  - A name whose last component, with the suffix, is longer than
 *    {NAME_MAX} of its directory is an error before any file is made.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *prefix = "x";
static long suflen = 2;
static char *path;              /* prefix and suffix */
static size_t plen;
static int out = -1;
static unsigned long long pieces, maxpieces;

static void usage(void)
{
    fprintf(stderr, "usage: split [-l line_count] [-a suffix_length] [file [name]]\n"
                    "       split -b n[k|m] [-a suffix_length] [file [name]]\n");
    exit(2);
}

/* open the next piece; false when the suffixes have run out */
static bool next_piece(void)
{
    if (out >= 0 && close(out) != 0) {
        fprintf(stderr, "split: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    out = -1;
    if (pieces >= maxpieces)
        return false;
    unsigned long long n = pieces++;
    for (long i = suflen - 1; i >= 0; i--) {
        path[plen + (size_t)i] = (char)('a' + n % 26);
        n /= 26;
    }
    out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
        fprintf(stderr, "split: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    return true;
}

static void put(const char *b, size_t n)
{
    while (n > 0) {
        ssize_t w = write(out, b, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "split: %s: %s\n", path, strerror(errno));
            exit(1);
        }
        b += w;
        n -= (size_t)w;
    }
}

static void too_many(void)
{
    fprintf(stderr, "split: the input needs more files than %ld letters of suffix give\n", suflen);
    exit(1);
}

static unsigned long long number(const char *s, const char *opt, bool sizes)
{
    char *end;
    if (*s < '0' || *s > '9')
        goto bad;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno)
        goto bad;
    if (sizes && (*end == 'k' || *end == 'm')) {
        unsigned long long mul = *end == 'k' ? 1024 : 1048576;
        if (v > UINT64_MAX / mul)
            goto bad;
        v *= mul;
        end++;
    }
    if (*end || v == 0)
        goto bad;
    return v;
bad:
    fprintf(stderr, "split: %s %s: not a valid number\n", opt, s);
    exit(2);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    unsigned long long lines = 0, bytes = 0;
    int c;
    while ((c = getopt(argc, argv, "l:a:b:")) != -1) {
        switch (c) {
        case 'l': lines = number(optarg, "-l", false); break;
        case 'b': bytes = number(optarg, "-b", true); break;
        case 'a': {
            unsigned long long a = number(optarg, "-a", false);
            if (a > 64) {
                fprintf(stderr, "split: -a %s: too long a suffix\n", optarg);
                return 2;
            }
            suflen = (long)a;
            break;
        }
        default: usage();
        }
    }
    if ((lines && bytes) || argc - optind > 2)
        usage();
    if (!lines && !bytes)
        lines = 1000;
    const char *file = optind < argc ? argv[optind] : "-";
    if (optind + 1 < argc)
        prefix = argv[optind + 1];
    /* the length of the names */
    const char *slash = strrchr(prefix, '/');
    const char *base = slash ? slash + 1 : prefix;
    char *dir;
    if (slash) {
        dir = strndup(prefix, (size_t)(slash - prefix) + (slash == prefix));
    } else
        dir = strdup(".");
    long name_max = dir ? pathconf(dir, _PC_NAME_MAX) : -1;
    if (name_max < 0)
        name_max = NAME_MAX;
    if ((long)strlen(base) + suflen > name_max) {
        fprintf(stderr, "split: %s: the names would be longer than {NAME_MAX} (%ld)\n", prefix, name_max);
        return 1;
    }
    free(dir);
    plen = strlen(prefix);
    path = malloc(plen + (size_t)suflen + 1);
    if (!path) {
        fprintf(stderr, "split: out of memory\n");
        return 1;
    }
    memcpy(path, prefix, plen);
    path[plen + (size_t)suflen] = '\0';
    maxpieces = 1;
    for (long i = 0; i < suflen; i++)
        maxpieces = maxpieces > UINT64_MAX / 26 ? UINT64_MAX : maxpieces * 26;

    int in = 0;
    if (strcmp(file, "-") != 0 && (in = open(file, O_RDONLY)) < 0) {
        fprintf(stderr, "split: %s: %s\n", file, strerror(errno));
        return 1;
    }
    static char buf[65536];
    unsigned long long left = 0;    /* lines or bytes still to go into the open piece */
    bool open_piece = false;
    for (;;) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "split: %s: %s\n", file, strerror(errno));
            return 1;
        }
        if (n == 0)
            break;
        size_t i = 0;
        while (i < (size_t)n) {
            if (!open_piece || left == 0) {
                if (!next_piece())
                    too_many();
                open_piece = true;
                left = bytes ? bytes : lines;
            }
            size_t take;
            if (bytes) {
                take = (size_t)n - i < left ? (size_t)n - i : (size_t)left;
                left -= take;
            } else {
                /* up to the end of the left'th line */
                size_t j = i;
                while (j < (size_t)n && left > 0) {
                    char *nl = memchr(buf + j, '\n', (size_t)n - j);
                    if (!nl) {
                        j = (size_t)n;
                        break;
                    }
                    j = (size_t)(nl - buf) + 1;
                    left--;
                }
                take = j - i;
            }
            put(buf + i, take);
            i += take;
        }
    }
    if (out >= 0 && close(out) != 0) {
        fprintf(stderr, "split: %s: %s\n", path, strerror(errno));
        return 1;
    }
    return 0;
}
