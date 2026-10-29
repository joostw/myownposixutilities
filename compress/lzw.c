/*
 * lzw.c - the .Z format of compress: adaptive LZW
 *
 * The format of compress 4.0: the bytes 0x1f 0x9d, a byte with the
 * largest code size (9 to 16 bits) and 0x80 for block mode, then the
 * codes, least significant bit first.  Codes start at 9 bits; 256 is
 * CLEAR, which empties the table, and new strings get the codes from 257
 * on.  Codes are written in groups of eight, so a group takes as many
 * bytes as there are bits in a code; when the code size changes (it grows
 * when the next new code would not fit, and goes back to 9 after CLEAR),
 * the rest of the group is padded.  Once the table is full, the
 * compression ratio is checked every 10000 input bytes, and CLEAR is sent
 * when it has not got better.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include "compress.h"

#define INIT_BITS 9
#define CLEAR 256
#define FIRST 257
#define CHECK_GAP 10000
#define HSIZE 69001             /* a prime, for 16 bits at 95% */

/* the state of the code writer */
static struct out *wout;
static int n_bits, maxbits;
static long maxcode, maxmaxcode, free_ent;
static bool clear_flg;
static unsigned long offset;    /* bits in the group */
static uintmax_t bytes_out;
static unsigned char group[16 + 8];

static void put_group(size_t n)
{
    out_bytes(wout, group, n);
    bytes_out += n;
    memset(group, 0, sizeof group);
}

/* write a code (-1: the end) */
static void output(long code)
{
    if (code < 0) {
        if (offset > 0)
            put_group((offset + 7) / 8);
        offset = 0;
        return;
    }
    unsigned long v = (unsigned long)code << (offset & 7);
    for (size_t p = offset >> 3; v; v >>= 8)
        group[p++] |= (unsigned char)(v & 0xff);
    offset += (unsigned long)n_bits;
    if (offset == (unsigned long)n_bits * 8) {
        put_group((size_t)n_bits);
        offset = 0;
    }
    /* the decoder finds out about a new size only at the end of a group */
    if (free_ent > maxcode || clear_flg) {
        if (offset > 0)
            put_group((size_t)n_bits);
        offset = 0;
        if (clear_flg) {
            n_bits = INIT_BITS;
            maxcode = (1L << n_bits) - 1;
            clear_flg = false;
        } else {
            n_bits++;
            maxcode = n_bits == maxbits ? maxmaxcode : (1L << n_bits) - 1;
        }
    }
}

void lzw_compress(struct in *in, struct out *out, int bits)
{
    static long htab[HSIZE];            /* (c << maxbits) + prefix, or -1 */
    static unsigned short codetab[HSIZE];
    unsigned char hdr[3] = { 0x1f, 0x9d, (unsigned char)(bits | 0x80) };

    wout = out;
    maxbits = bits;
    maxmaxcode = 1L << bits;
    n_bits = INIT_BITS;
    maxcode = (1L << n_bits) - 1;
    free_ent = FIRST;
    clear_flg = false;
    offset = 0;
    memset(group, 0, sizeof group);
    out_bytes(out, hdr, sizeof hdr);
    bytes_out = 3;

    uintmax_t in_count = 1, checkpoint = CHECK_GAP;
    long ratio = 0;
    for (long i = 0; i < HSIZE; i++)
        htab[i] = -1;
    int ent = in_byte(in), c;
    if (ent < 0)
        return;
    while ((c = in_byte(in)) >= 0) {
        in_count++;
        long fcode = ((long)c << maxbits) + ent;
        long i = (long)(((unsigned long)c << 8 ^ (unsigned long)ent) % HSIZE);
        long disp = i == 0 ? 1 : HSIZE - i;
        while (htab[i] >= 0 && htab[i] != fcode)
            if ((i -= disp) < 0)
                i += HSIZE;
        if (htab[i] == fcode) {
            ent = codetab[i];
            continue;
        }
        output(ent);
        ent = c;
        if (free_ent < maxmaxcode) {
            codetab[i] = (unsigned short)free_ent++;
            htab[i] = fcode;
        } else if (in_count >= checkpoint) {
            /* the table is full: start again if the ratio has not got better */
            long rat;
            checkpoint = in_count + CHECK_GAP;
            if (in_count > 0x007fffff) {
                uintmax_t b = bytes_out >> 8;
                rat = b == 0 ? 0x7fffffff : (long)(in_count / b);
            } else {
                rat = (long)((in_count << 8) / bytes_out);
            }
            if (rat > ratio) {
                ratio = rat;
            } else {
                ratio = 0;
                for (long j = 0; j < HSIZE; j++)
                    htab[j] = -1;
                free_ent = FIRST;
                clear_flg = true;
                output(CLEAR);
            }
        }
    }
    output(ent);
    output(-1);
}

/* the state of the code reader */
static struct in *rin;
static unsigned char rbuf[16];
static long rsize;              /* bits of whole codes in rbuf */

static long getcode(void)
{
    if (clear_flg || (long)offset >= rsize || free_ent > maxcode) {
        if (free_ent > maxcode) {
            n_bits++;
            maxcode = n_bits == maxbits ? maxmaxcode : (1L << n_bits) - 1;
        }
        if (clear_flg) {
            n_bits = INIT_BITS;
            maxcode = (1L << n_bits) - 1;
            clear_flg = false;
        }
        int n = 0, c;
        while (n < n_bits && (c = in_byte(rin)) >= 0)
            rbuf[n++] = (unsigned char)c;
        if (n == 0)
            return -1;
        offset = 0;
        rsize = n * 8L - (n_bits - 1);
    }
    unsigned long v = 0;
    size_t p = offset >> 3;
    for (int k = 0; k < 3 && p + (size_t)k < sizeof rbuf; k++)
        v |= (unsigned long)rbuf[p + (size_t)k] << (8 * k);
    long code = (long)((v >> (offset & 7)) & ((1UL << n_bits) - 1));
    offset += (unsigned long)n_bits;
    return code;
}

const char *lzw_decompress(struct in *in, struct out *out)
{
    static unsigned short prefix[1L << 16];
    static unsigned char suffix[1L << 16];
    static unsigned char stack[1L << 16];

    int flags = in_byte(in);
    if (flags < 0)
        return "unexpected end of file";
    maxbits = flags & 0x1f;
    bool block_mode = (flags & 0x80) != 0;
    if (flags & 0x60)
        return "unknown flags in the header";
    if (maxbits < INIT_BITS || maxbits > 16)
        return "unsupported code size";
    maxmaxcode = 1L << maxbits;
    n_bits = INIT_BITS;
    maxcode = (1L << n_bits) - 1;
    free_ent = block_mode ? FIRST : 256;
    clear_flg = false;
    rin = in;
    offset = 0;
    rsize = 0;
    for (int i = 0; i < 256; i++)
        prefix[i] = 0, suffix[i] = (unsigned char)i;

    long oldcode = getcode(), code;
    if (oldcode < 0)
        return NULL;
    if (oldcode >= 256)
        return "corrupt input";
    unsigned char finchar = (unsigned char)oldcode;
    out_bytes(out, &finchar, 1);
    while ((code = getcode()) >= 0) {
        if (code == CLEAR && block_mode) {
            clear_flg = true;
            free_ent = FIRST - 1;
            if ((code = getcode()) < 0)
                break;
        }
        long incode = code;
        size_t sp = 0;
        if (code >= free_ent) {
            if (code > free_ent)
                return "corrupt input";
            stack[sp++] = finchar;
            code = oldcode;
        }
        while (code >= 256) {
            stack[sp++] = suffix[code];
            code = prefix[code];
        }
        finchar = suffix[code];
        stack[sp++] = finchar;
        for (size_t a = 0, b = sp - 1; a < b; a++, b--) {
            unsigned char t = stack[a];
            stack[a] = stack[b], stack[b] = t;
        }
        out_bytes(out, stack, sp);
        if (free_ent < maxmaxcode) {
            prefix[free_ent] = (unsigned short)oldcode;
            suffix[free_ent] = finchar;
            free_ent++;
        }
        oldcode = incode;
    }
    return NULL;
}
