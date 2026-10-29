/*
 * ipcrm.c - ipcrm: remove an XSI message queue, semaphore set, or shared
 * memory segment identifier (POSIX.1-2024, XCU ipcrm) [XSI]
 *
 * Synopsis: ipcrm [-q msgid|-Q msgkey|-s semid|-S semkey|-m shmid|-M shmkey]...
 *
 * msgctl(), semctl() or shmctl() with IPC_RMID, for each option in the
 * order given; a key is found with msgget(), semget() or shmget() without
 * IPC_CREAT.
 *
 * Choices where the standard leaves room:
 *  - An identifier is a decimal integer; a key is an integer in C notation
 *    (decimal, 0x hexadecimal or 0 octal), as ipcs writes it in hex.  The
 *    key IPC_PRIVATE (0) names no single entry and is an error.
 *  - Each option is done even when one before it fails; the exit status is
 *    then 1.  A value that is not a number is such an error too.  Unknown
 *    options and operands are usage errors (status 2), and nothing is
 *    removed then.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>

static bool number(const char *s, int base, long *v)
{
    char *end;
    if (!*s || *s == '-' || *s == '+' || *s == ' ')
        return false;
    errno = 0;
    unsigned long u = strtoul(s, &end, base);
    if (*end || errno || u > (base == 10 ? (unsigned long)INT_MAX : 0xffffffffUL))
        return false;
    *v = (long)u;
    return true;
}

static void usage(void)
{
    fprintf(stderr, "usage: ipcrm [-q msgid|-Q msgkey|-s semid|-S semkey|-m shmid|-M shmkey]...\n");
    exit(2);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    /* the options are checked first: a usage error removes nothing */
    int c;
    while ((c = getopt(argc, argv, "q:Q:s:S:m:M:")) != -1)
        if (c == '?' || c == ':')
            usage();
    if (optind < argc)
        usage();
    optind = 1;
    int status = 0;
    while ((c = getopt(argc, argv, "q:Q:s:S:m:M:")) != -1) {
        bool by_key = c == 'Q' || c == 'S' || c == 'M';
        long v;
        if (!number(optarg, by_key ? 0 : 10, &v)) {
            fprintf(stderr, "ipcrm: -%c %s: not %s\n", c, optarg, by_key ? "a key" : "an identifier");
            status = 1;
            continue;
        }
        int id = (int)v, r;
        if (by_key) {
            key_t key = (key_t)(unsigned)v;
            if (key == IPC_PRIVATE) {
                fprintf(stderr, "ipcrm: -%c %s: the key IPC_PRIVATE names no entry\n", c, optarg);
                status = 1;
                continue;
            }
            id = c == 'Q' ? msgget(key, 0) : c == 'S' ? semget(key, 0, 0) : shmget(key, 0, 0);
            if (id < 0) {
                fprintf(stderr, "ipcrm: -%c %s: %s\n", c, optarg,
                        errno == ENOENT ? "no entry with that key" : strerror(errno));
                status = 1;
                continue;
            }
        }
        switch (c) {
        case 'q': case 'Q': r = msgctl(id, IPC_RMID, NULL); break;
        case 's': case 'S': r = semctl(id, 0, IPC_RMID); break;
        default: r = shmctl(id, IPC_RMID, NULL); break;
        }
        if (r != 0) {
            fprintf(stderr, "ipcrm: -%c %s: %s\n", c, optarg,
                    errno == EINVAL || errno == EIDRM ? "no such identifier" : strerror(errno));
            status = 1;
        }
    }
    return status;
}
