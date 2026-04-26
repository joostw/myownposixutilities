/*
 * tail — copy the last part of a file  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis: tail [-f] [-c number|-n number] [file]
 *           tail -r [-n number] [file]
 *
 * number may be prefixed with '+' (from start, 1-based) or '-'/none (from end).
 */

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool      opt_f, opt_r, opt_n_set;
static bool      use_bytes;         /* -c vs -n */
static bool      from_start;        /* + prefix → count from beginning */
static long long count   = 10;      /* default: 10 lines from end */
static int       had_error;

/* ---- line ring buffer ---- */
typedef struct { char *data; size_t len; size_t cap; } Line;
static Line  *ring;
static size_t ring_cap, ring_head, ring_size;

static void ring_free(void)
{
    for (size_t i = 0; i < ring_cap; i++) free(ring[i].data);
    free(ring);
    ring = NULL;
    ring_cap = ring_head = ring_size = 0;
}

static void ring_init(size_t n)
{
    ring_free();
    if (n == 0) return;
    ring = calloc(n, sizeof *ring);
    if (!ring) { perror("tail"); exit(1); }
    ring_cap = n;
}

static void ring_push(const char *s, size_t len)
{
    if (!ring_cap) return;
    Line *slot = &ring[ring_head];
    if (len + 1 > slot->cap) {
        free(slot->data);
        slot->data = malloc(len + 1);
        if (!slot->data) { perror("tail"); exit(1); }
        slot->cap = len + 1;
    }
    memcpy(slot->data, s, len);
    slot->data[len] = '\0';
    slot->len = len;
    ring_head = (ring_head + 1) % ring_cap;
    if (ring_size < ring_cap) ring_size++;
}

/* Return index of the i-th oldest line in the ring (0 = oldest). */
static size_t ring_idx(size_t i)
{
    return (ring_head + ring_cap - ring_size + i) % ring_cap;
}

static void ring_print_forward(void)
{
    for (size_t i = 0; i < ring_size; i++)
        fwrite(ring[ring_idx(i)].data, 1, ring[ring_idx(i)].len, stdout);
}

static void ring_print_reverse(void)
{
    for (size_t i = ring_size; i-- > 0; )
        fwrite(ring[ring_idx(i)].data, 1, ring[ring_idx(i)].len, stdout);
}

/* ---- helpers ---- */
static void copy_stream(FILE *f)
{
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        fwrite(buf, 1, n, stdout);
}

/* Skip 'skip' bytes from f; return false on read error. */
static bool skip_bytes(FILE *f, long long skip)
{
    /* Try fseeko first */
    if (fseeko(f, (off_t)skip, SEEK_SET) == 0) return true;
    /* Non-seekable: read and discard */
    char buf[8192];
    while (skip > 0) {
        size_t want = (skip > (long long)sizeof buf) ? sizeof buf : (size_t)skip;
        size_t n = fread(buf, 1, want, f);
        if (n == 0) return !ferror(f);
        skip -= (long long)n;
    }
    return true;
}

/* Skip 'skip' newlines from f. */
static bool skip_lines(FILE *f, long long skip)
{
    int c;
    while (skip > 0 && (c = getc(f)) != EOF)
        if (c == '\n') skip--;
    return !ferror(f);
}

/* ---- mode implementations ---- */

static void from_start_bytes(FILE *f)
{
    /* Skip first (count - 1) bytes, then copy rest */
    if (!skip_bytes(f, count - 1)) {
        fprintf(stderr, "tail: read error\n");
        had_error = 1;
        return;
    }
    copy_stream(f);
}

static void from_start_lines(FILE *f)
{
    /* Skip first (count - 1) newlines, then copy rest */
    if (!skip_lines(f, count - 1)) {
        fprintf(stderr, "tail: read error\n");
        had_error = 1;
        return;
    }
    copy_stream(f);
}

static void from_end_bytes(FILE *f)
{
    if (count == 0) return;

    /* Try seeking */
    if (fseeko(f, 0, SEEK_END) == 0) {
        off_t end = ftello(f);
        off_t start = (end > (off_t)count) ? (end - (off_t)count) : 0;
        if (fseeko(f, start, SEEK_SET) == 0) {
            copy_stream(f);
            return;
        }
    }
    rewind(f);

    /* Ring byte buffer for non-seekable input */
    char *buf = malloc((size_t)count);
    if (!buf) { perror("tail"); had_error = 1; return; }
    size_t head = 0, n = 0;
    char chunk[8192];
    size_t r;
    while ((r = fread(chunk, 1, sizeof chunk, f)) > 0) {
        for (size_t i = 0; i < r; i++) {
            buf[head] = chunk[i];
            head = (head + 1) % (size_t)count;
            if (n < (size_t)count) n++;
        }
    }
    size_t start = (head + (size_t)count - n) % (size_t)count;
    for (size_t i = 0; i < n; i++)
        putchar_unlocked(buf[(start + i) % (size_t)count]);
    free(buf);
}

static void from_end_lines(FILE *f)
{
    ring_init((size_t)count);

    char *linebuf = NULL;
    size_t linecap = 0;
    ssize_t len;
    while ((len = getline(&linebuf, &linecap, f)) != -1)
        ring_push(linebuf, (size_t)len);
    free(linebuf);

    ring_print_forward();
}

static void reverse_lines(FILE *f)
{
    /* -r without -n: collect ALL lines; -r -n N: collect last N */
    size_t cap = opt_n_set ? (size_t)count : SIZE_MAX / sizeof(Line);
    ring_init(cap > 1000000 ? 1000000 : cap);

    char *linebuf = NULL;
    size_t linecap = 0;
    ssize_t len;

    /* Grow dynamically when no limit or ring is full for "all" case */
    if (cap > 1000000) {
        /* Unbounded: use dynamic array */
        Line *arr = NULL;
        size_t arr_n = 0, arr_cap = 0;
        while ((len = getline(&linebuf, &linecap, f)) != -1) {
            if (arr_n == arr_cap) {
                size_t newcap = arr_cap ? arr_cap * 2 : 256;
                Line *tmp = realloc(arr, newcap * sizeof *arr);
                if (!tmp) { perror("tail"); exit(1); }
                arr = tmp;
                arr_cap = newcap;
            }
            arr[arr_n].data = malloc((size_t)len + 1);
            if (!arr[arr_n].data) { perror("tail"); exit(1); }
            memcpy(arr[arr_n].data, linebuf, (size_t)len);
            arr[arr_n].data[len] = '\0';
            arr[arr_n].len = (size_t)len;
            arr[arr_n].cap = (size_t)len + 1;
            arr_n++;
        }
        free(linebuf);
        for (size_t i = arr_n; i-- > 0; )
            fwrite(arr[i].data, 1, arr[i].len, stdout);
        for (size_t i = 0; i < arr_n; i++) free(arr[i].data);
        free(arr);
        return;
    }

    while ((len = getline(&linebuf, &linecap, f)) != -1)
        ring_push(linebuf, (size_t)len);
    free(linebuf);

    ring_print_reverse();
}

static void follow_stream(FILE *f)
{
    while (1) {
        char buf[8192];
        size_t n = fread(buf, 1, sizeof buf, f);
        if (n > 0) {
            fwrite(buf, 1, n, stdout);
            fflush(stdout);
        } else {
            if (ferror(f)) { had_error = 1; return; }
            clearerr(f);
            sleep(1);
        }
    }
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    int opt;
    while ((opt = getopt(argc, argv, "c:fn:r")) != -1) {
        char *end, *p;
        switch (opt) {
        case 'c':
            use_bytes = true;
            p = optarg;
            if (*p == '+') { from_start = true; p++; }
            else if (*p == '-') { from_start = false; p++; }
            else { from_start = false; }
            count = strtoll(p, &end, 10);
            if (*end != '\0' || count <= 0) {
                fprintf(stderr, "tail: invalid number of bytes: '%s'\n", optarg);
                return 1;
            }
            break;
        case 'f':
            opt_f = true;
            break;
        case 'n':
            use_bytes = false;
            opt_n_set = true;
            p = optarg;
            if (*p == '+') { from_start = true; p++; }
            else if (*p == '-') { from_start = false; p++; }
            else { from_start = false; }
            count = strtoll(p, &end, 10);
            if (*end != '\0' || count <= 0) {
                fprintf(stderr, "tail: invalid number of lines: '%s'\n", optarg);
                return 1;
            }
            break;
        case 'r':
            opt_r = true;
            break;
        default:
            fprintf(stderr, "usage: tail [-f] [-c number|-n number] [file]\n"
                            "       tail -r [-n number] [file]\n");
            return 1;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc > 1) {
        fprintf(stderr, "tail: too many operands\n");
        return 1;
    }

    FILE *f;
    const char *name;
    if (argc == 0 || strcmp(argv[0], "-") == 0) {
        f    = stdin;
        name = "stdin";
    } else {
        f = fopen(argv[0], "rb");
        if (!f) {
            fprintf(stderr, "tail: %s: %s\n", argv[0], strerror(errno));
            return 1;
        }
        name = argv[0];
    }

    if (opt_r) {
        reverse_lines(f);
    } else if (use_bytes) {
        if (from_start) from_start_bytes(f);
        else            from_end_bytes(f);
        if (opt_f) follow_stream(f);
    } else {
        if (from_start) from_start_lines(f);
        else            from_end_lines(f);
        if (opt_f) follow_stream(f);
    }

    if (ferror(stdout)) {
        fprintf(stderr, "tail: write error\n");
        had_error = 1;
    }

    if (ferror(f)) {
        fprintf(stderr, "tail: %s: read error\n", name);
        had_error = 1;
    }

    if (f != stdin) fclose(f);

    ring_free();
    return had_error ? 1 : 0;
}
