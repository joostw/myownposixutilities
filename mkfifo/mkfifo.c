/*
 * mkfifo.c - mkfifo: make FIFO special files (POSIX.1-2024, XCU mkfifo)
 *
 * Synopsis: mkfifo [-m mode] file...
 *
 * Without -m, mkfifo() is called with the mode rw for everyone, which the
 * file mode creation mask then restricts.  With -m, the mode is that of
 * chmod (octal, or symbolic, + and - relative to a=rw), and the FIFO is
 * created with exactly that mode: the mask is set to 0 for the call, so
 * the FIFO has at no time other permissions.
 *
 * The mode is compiled and applied by the same code as in chmod, where a
 * clause without a who leaves alone the bits of the mask.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- the mode ---- */

struct action {
    char op;                    /* + - = */
    int who;                    /* bits 1 u, 2 g, 4 o; 0: none given */
    mode_t perm;                /* the r w x s t bits, for all three classes */
    bool X;                     /* X given */
    char copy;                  /* u g o: copy the permissions of that class, or 0 */
};

static struct action *acts;
static size_t nacts;
static bool octal;
static mode_t octal_mode;
static mode_t cmask;

static void bad_mode(const char *s)
{
    fprintf(stderr, "mkfifo: invalid mode: %s\n", s);
    exit(2);
}

static void compile_mode(const char *s)
{
    if (*s >= '0' && *s <= '9') {
        mode_t m = 0;
        for (const char *p = s; *p; p++) {
            if (*p < '0' || *p > '7')
                bad_mode(s);
            m = m * 8 + (mode_t)(*p - '0');
            if (m > 07777)
                bad_mode(s);
        }
        octal = true;
        octal_mode = m;
        return;
    }
    size_t cap = 0;
    const char *p = s;
    for (;;) {
        /* clause: [wholist] actionlist */
        int who = 0;
        for (;; p++) {
            if (*p == 'u') who |= 1;
            else if (*p == 'g') who |= 2;
            else if (*p == 'o') who |= 4;
            else if (*p == 'a') who |= 7;
            else break;
        }
        if (*p != '+' && *p != '-' && *p != '=')
            bad_mode(s);
        while (*p == '+' || *p == '-' || *p == '=') {
            struct action a = { *p++, who, 0, false, 0 };
            if (*p == 'u' || *p == 'g' || *p == 'o') {
                a.copy = *p++;          /* permcopy: one letter, alone */
            } else {
                for (;; p++) {
                    if (*p == 'r') a.perm |= 0444;
                    else if (*p == 'w') a.perm |= 0222;
                    else if (*p == 'x') a.perm |= 0111;
                    else if (*p == 'X') a.X = true;
                    else if (*p == 's') a.perm |= S_ISUID | S_ISGID;
                    else if (*p == 't') a.perm |= S_ISVTX;
                    else break;
                }
            }
            if (nacts == cap) {
                cap = cap ? cap * 2 : 8;
                if (!(acts = realloc(acts, cap * sizeof *acts))) {
                    fprintf(stderr, "mkfifo: out of memory\n");
                    exit(1);
                }
            }
            acts[nacts++] = a;
        }
        if (*p == '\0')
            break;
        if (*p != ',')
            bad_mode(s);
        p++;
    }
}

/* the bits of a class: its rwx bits, and the special bit that goes with it */
static mode_t class_bits(int who)
{
    mode_t m = 0;
    if (who & 1) m |= S_IRWXU | S_ISUID;
    if (who & 2) m |= S_IRWXG | S_ISGID;
    if (who & 4) m |= S_IRWXO | S_ISVTX;
    return m;
}

/* the new mode of a file whose mode is old */
static mode_t new_mode(mode_t old)
{
    if (octal)
        return octal_mode;
    bool isdir = S_ISDIR(old);
    mode_t m = old & 07777;
    for (size_t i = 0; i < nacts; i++) {
        struct action *a = &acts[i];
        mode_t perm = a->perm;
        if (a->X && (isdir || (old & 0111)))
            perm |= 0111;
        if (a->copy) {
            mode_t c = a->copy == 'u' ? (m >> 6) & 7 : a->copy == 'g' ? (m >> 3) & 7 : m & 7;
            perm = c << 6 | c << 3 | c;
        }
        mode_t mask;
        if (a->who) {
            mask = class_bits(a->who);
            /* t goes with o; s with u and g: for o alone it is nothing */
        } else {
            mask = 07777 & ~cmask;
            mask |= S_ISUID | S_ISGID | S_ISVTX;    /* the umask has no say in these */
        }
        perm &= mask;
        switch (a->op) {
        case '+':
            m |= perm;
            break;
        case '-':
            m &= ~perm;
            break;
        case '=':
            m &= ~(a->who ? class_bits(a->who) : 07777);
            m |= perm;
            break;
        }
    }
    return m;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    const char *m = NULL;
    int c;
    while ((c = getopt(argc, argv, "m:")) != -1) {
        if (c == 'm')
            m = optarg;
        else {
            fprintf(stderr, "usage: mkfifo [-m mode] file...\n");
            return 2;
        }
    }
    if (optind == argc) {
        fprintf(stderr, "usage: mkfifo [-m mode] file...\n");
        return 2;
    }
    cmask = umask(0);
    umask(cmask);
    mode_t mode = 0666;
    if (m) {
        compile_mode(m);
        mode = new_mode(S_IFIFO | 0666);
        umask(0);
    }
    int status = 0;
    for (int i = optind; i < argc; i++)
        if (mkfifo(argv[i], mode) != 0) {
            fprintf(stderr, "mkfifo: %s: %s\n", argv[i], strerror(errno));
            status = 1;
        }
    return status;
}
