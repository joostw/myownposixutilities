/*
 * cksum.c - cksum: write file checksums and sizes (POSIX.1-2024, XCU cksum)
 *
 * Synopsis: cksum [file...]
 *
 * The CRC of the standard (the polynomial of ISO/IEC 8802-3, the length
 * of the file appended least significant octet first, the result
 * complemented), computed a byte at a time with a table; sizes are
 * uintmax_t.  There are no options; a first "--" is discarded.
 *
 * Choices where the standard leaves room:
 *  - "-" is a file name like any other (the standard input is read only
 *    when there are no operands).
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static uint32_t table[256];

static void make_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i << 24;
        for (int k = 0; k < 8; k++)
            c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : c << 1;
        table[i] = c;
    }
}

/* the CRC and size of the file open on fd; -1 on a read error */
static int sum(int fd, uint32_t *crc, uintmax_t *size)
{
    static unsigned char buf[1 << 16];
    uint32_t c = 0;
    uintmax_t n = 0;
    for (;;) {
        ssize_t k = read(fd, buf, sizeof buf);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (k == 0)
            break;
        for (ssize_t i = 0; i < k; i++)
            c = (c << 8) ^ table[(c >> 24) ^ buf[i]];
        n += (uintmax_t)k;
    }
    for (uintmax_t len = n; len; len >>= 8)
        c = (c << 8) ^ table[(c >> 24) ^ (len & 0xff)];
    *crc = ~c;
    *size = n;
    return 0;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    make_table();
    int i = 1;
    if (i < argc && strcmp(argv[i], "--") == 0)
        i++;
    int status = 0;
    uint32_t crc;
    uintmax_t size;
    if (i == argc) {
        if (sum(0, &crc, &size) != 0) {
            fprintf(stderr, "cksum: standard input: %s\n", strerror(errno));
            return 1;
        }
        printf("%" PRIu32 " %ju\n", crc, size);
    }
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0 || sum(fd, &crc, &size) != 0) {
            fprintf(stderr, "cksum: %s: %s\n", argv[i], strerror(errno));
            status = 1;
        } else
            printf("%" PRIu32 " %ju %s\n", crc, size, argv[i]);
        if (fd >= 0)
            close(fd);
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "cksum: write error: %s\n", strerror(errno));
        return 1;
    }
    return status;
}
