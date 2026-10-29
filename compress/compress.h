/*
 * compress.h - the parts of compress (compress.c, lzw.c, gzip.c)
 */

#ifndef COMPRESS_H
#define COMPRESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define IOSIZE 65536

/* buffered input from a descriptor */
struct in {
    int fd;
    size_t pos, len;
    bool eof;
    int err;                    /* errno of a failed read, or 0 */
    uintmax_t count;            /* bytes taken so far */
    unsigned char buf[IOSIZE];
};

/* buffered output to a descriptor; errors are kept for the end */
struct out {
    int fd;
    size_t len;
    int err;                    /* errno of a failed write, or 0 */
    uintmax_t count;            /* bytes given so far */
    unsigned char buf[IOSIZE];
};

int in_refill(struct in *in);
/* the next input byte, or -1 at the end (or on an error, in in->err) */
static inline int in_byte(struct in *in)
{
    if (in->pos < in->len) {
        in->count++;
        return in->buf[in->pos++];
    }
    return in_refill(in);
}
void out_bytes(struct out *out, const void *p, size_t n);
void out_flush(struct out *out);

/* lzw.c: the .Z format.  The decompressors are called after the two
   magic bytes, and return NULL, or a message for bad input */
void lzw_compress(struct in *in, struct out *out, int maxbits);
const char *lzw_decompress(struct in *in, struct out *out);

/* gzip.c: the gzip format (RFC 1952) with DEFLATE (RFC 1951) */
void gzip_compress(struct in *in, struct out *out, int level, time_t mtime);
const char *gzip_decompress(struct in *in, struct out *out);

#endif
