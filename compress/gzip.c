/*
 * gzip.c - the gzip format (RFC 1952) and DEFLATE (RFC 1951)
 *
 * Decompression handles every kind of block (stored, fixed and dynamic
 * Huffman codes), every header field, and members one after another;
 * the CRC-32 and the size in each trailer are checked.
 *
 * Compression finds matches through hash chains over a 64 KiB window,
 * with lazy matching; how hard it looks depends on the level (1 to 9,
 * the same steps as zlib's).  Each block of up to 16384 symbols is
 * written whichever way is shortest: stored, fixed codes or dynamic
 * codes.  The header has the modification time of the input, no name.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include "compress.h"

/* ---- CRC-32 ---- */

static uint32_t crc_table[256];

static void crc_init(void)
{
    if (crc_table[1])
        return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320U ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *p, size_t n)
{
    crc = ~crc;
    while (n--)
        crc = crc_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    return ~crc;
}

/* ---- the tables of DEFLATE ---- */

static const unsigned short len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const unsigned char len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const unsigned short dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const unsigned char dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
/* the order of the code length code lengths */
static const unsigned char cl_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/* ---- decompression ---- */

#define WSIZE 32768
#define MAXBITS 15

static struct in *din;
static unsigned long bitbuf;
static int bitcnt;
static const char *derr;        /* the first error */

static unsigned char window[WSIZE];
static size_t wpos;             /* bytes in window not yet written */
static uintmax_t total;         /* bytes made in this member */
static uint32_t crc;
static struct out *dout;

/* n bits (n <= 16), or 0 with derr set at the end of the input */
static unsigned bits(int n)
{
    while (bitcnt < n) {
        int c = in_byte(din);
        if (c < 0) {
            if (!derr)
                derr = "unexpected end of file";
            return 0;
        }
        bitbuf |= (unsigned long)c << bitcnt;
        bitcnt += 8;
    }
    unsigned v = (unsigned)(bitbuf & ((1UL << n) - 1));
    bitbuf >>= n;
    bitcnt -= n;
    return v;
}

static void flush_window(void)
{
    crc = crc32_update(crc, window, wpos);
    out_bytes(dout, window, wpos);
    wpos = 0;
}

static void put_byte(unsigned char c)
{
    window[wpos++] = c;
    total++;
    if (wpos == WSIZE)
        flush_window();
}

/* a canonical Huffman code: how many codes of each length, and the
   symbols in order of their codes */
struct huff {
    short count[MAXBITS + 1];
    short symbol[288];
};

/* build h from the lengths; false if the code is over-subscribed, or
   incomplete with more than one code (which DEFLATE allows only for a
   single distance code) */
static bool build(struct huff *h, const unsigned char *len, int n)
{
    short offs[MAXBITS + 1];
    memset(h->count, 0, sizeof h->count);
    for (int s = 0; s < n; s++)
        h->count[len[s]]++;
    if (h->count[0] == n)
        return true;
    int left = 1;
    for (int l = 1; l <= MAXBITS; l++) {
        left <<= 1;
        left -= h->count[l];
        if (left < 0)
            return false;
    }
    offs[1] = 0;
    for (int l = 1; l < MAXBITS; l++)
        offs[l + 1] = (short)(offs[l] + h->count[l]);
    for (int s = 0; s < n; s++)
        if (len[s])
            h->symbol[offs[len[s]]++] = (short)s;
    return left == 0 || n - h->count[0] == 1;
}

/* the next symbol in code h, or -1 */
static int decode(const struct huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int l = 1; l <= MAXBITS; l++) {
        code |= (int)bits(1);
        if (derr)
            return -1;
        int count = h->count[l];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    derr = "invalid code";
    return -1;
}

static void codes(const struct huff *lit, const struct huff *dist)
{
    for (;;) {
        int s = decode(lit);
        if (s < 0)
            return;
        if (s < 256) {
            put_byte((unsigned char)s);
            continue;
        }
        if (s == 256)
            return;
        s -= 257;
        if (s >= 29) {
            derr = "invalid length code";
            return;
        }
        unsigned len = len_base[s] + bits(len_extra[s]);
        int d = decode(dist);
        if (d < 0)
            return;
        if (d >= 30) {
            derr = "invalid distance code";
            return;
        }
        unsigned dst = dist_base[d] + bits(dist_extra[d]);
        if (derr)
            return;
        if (dst > total || dst > WSIZE) {
            derr = "distance too far back";
            return;
        }
        while (len--)
            put_byte(window[(wpos + WSIZE - dst) % WSIZE]);
    }
}

static void stored(void)
{
    bitbuf = 0, bitcnt = 0;     /* to a byte boundary */
    unsigned len = bits(16), nlen = bits(16);
    if (derr)
        return;
    if (len != (~nlen & 0xffff)) {
        derr = "invalid stored block length";
        return;
    }
    while (len--) {
        int c = in_byte(din);
        if (c < 0) {
            derr = "unexpected end of file";
            return;
        }
        put_byte((unsigned char)c);
    }
}

static void fixed(void)
{
    static struct huff lit, dist;
    static bool made;
    if (!made) {
        unsigned char l[288];
        int s;
        for (s = 0; s < 144; s++) l[s] = 8;
        for (; s < 256; s++) l[s] = 9;
        for (; s < 280; s++) l[s] = 7;
        for (; s < 288; s++) l[s] = 8;
        build(&lit, l, 288);
        for (s = 0; s < 30; s++) l[s] = 5;
        build(&dist, l, 30);
        made = true;
    }
    codes(&lit, &dist);
}

static void dynamic(void)
{
    static struct huff lit, dist, cl;
    unsigned char len[320];
    int nlen = (int)bits(5) + 257, ndist = (int)bits(5) + 1, ncode = (int)bits(4) + 4;
    if (derr)
        return;
    if (nlen > 286 || ndist > 30) {
        derr = "too many length or distance codes";
        return;
    }
    memset(len, 0, 19);
    for (int i = 0; i < ncode; i++)
        len[cl_order[i]] = (unsigned char)bits(3);
    if (derr)
        return;
    if (!build(&cl, len, 19)) {
        derr = "invalid code lengths";
        return;
    }
    for (int i = 0; i < nlen + ndist; ) {
        int s = decode(&cl);
        if (s < 0)
            return;
        if (s < 16) {
            len[i++] = (unsigned char)s;
            continue;
        }
        unsigned char v = 0;
        int rep;
        if (s == 16) {
            if (i == 0) {
                derr = "repeat with no first length";
                return;
            }
            v = len[i - 1];
            rep = 3 + (int)bits(2);
        } else if (s == 17) {
            rep = 3 + (int)bits(3);
        } else {
            rep = 11 + (int)bits(7);
        }
        if (derr)
            return;
        if (i + rep > nlen + ndist) {
            derr = "too many lengths";
            return;
        }
        while (rep--)
            len[i++] = v;
    }
    if (len[256] == 0) {
        derr = "no end-of-block code";
        return;
    }
    if (!build(&lit, len, nlen) || !build(&dist, len + nlen, ndist)) {
        derr = "invalid literal/length or distance code";
        return;
    }
    codes(&lit, &dist);
}

static unsigned get4(void)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        int c = in_byte(din);
        if (c < 0) {
            derr = "unexpected end of file";
            return 0;
        }
        v |= (unsigned)c << (8 * i);
    }
    return v;
}

/* skip a zero-terminated header field */
static void skip_string(void)
{
    int c;
    while ((c = in_byte(din)) > 0)
        ;
    if (c < 0)
        derr = "unexpected end of file";
}

const char *gzip_decompress(struct in *in, struct out *out)
{
    crc_init();
    din = in, dout = out, derr = NULL;
    for (;;) {
        /* the rest of the header: method, flags, mtime, xfl, os */
        int method = in_byte(in), flags = in_byte(in);
        if (method < 0 || flags < 0)
            return "unexpected end of file";
        if (method != 8)
            return "unknown compression method";
        if (flags & 0xe0)
            return "unknown flags in the header";
        for (int i = 0; i < 6; i++)
            if (in_byte(in) < 0)
                return "unexpected end of file";
        if (flags & 4) {                        /* FEXTRA */
            int a = in_byte(in), b = in_byte(in);
            if (a < 0 || b < 0)
                return "unexpected end of file";
            for (int n = a | b << 8; n > 0; n--)
                if (in_byte(in) < 0)
                    return "unexpected end of file";
        }
        if (flags & 8)                          /* FNAME */
            skip_string();
        if (flags & 16)                         /* FCOMMENT */
            skip_string();
        if ((flags & 2) && (in_byte(in) < 0 || in_byte(in) < 0))    /* FHCRC */
            return "unexpected end of file";
        if (derr)
            return derr;

        bitbuf = 0, bitcnt = 0, wpos = 0, total = 0, crc = 0;
        int last;
        do {
            last = (int)bits(1);
            int type = (int)bits(2);
            if (derr)
                break;
            if (type == 0)
                stored();
            else if (type == 1)
                fixed();
            else if (type == 2)
                dynamic();
            else
                derr = "invalid block type";
        } while (!last && !derr);
        flush_window();
        if (derr)
            return derr;
        /* the trailer starts at the next byte */
        bitbuf = 0, bitcnt = 0;
        unsigned c = get4(), size = get4();
        if (derr)
            return derr;
        if (c != crc)
            return "CRC error";
        if (size != (unsigned)(total & 0xffffffffU))
            return "length error";

        /* another member, or the end */
        int m1 = in_byte(in);
        if (m1 < 0)
            return NULL;
        int m2 = in_byte(in);
        if (m1 != 0x1f || m2 != 0x8b)
            return "trailing garbage";
    }
}

/* ---- compression ---- */

#define CWSIZE 32768
#define WMASK (CWSIZE - 1)
#define MIN_MATCH 3
#define MAX_MATCH 258
#define MIN_LOOKAHEAD (MAX_MATCH + MIN_MATCH + 1)
#define MAX_DIST (CWSIZE - MIN_LOOKAHEAD)
#define HBITS 15
#define HSIZE (1 << HBITS)
#define NIL (-1)
#define SYMS 16384

static unsigned char win[2 * CWSIZE];
static long lookahead;          /* bytes from strstart on */
static long strstart, block_start;
static long match_start;        /* of the last match found */
static int head[HSIZE], prev[CWSIZE];
static unsigned hash;
static bool in_done;
static struct in *cin;
static struct out *cout;
static uint32_t ccrc;

/* the symbols of the current block: a literal (dist 0), or a match */
static unsigned short sym_dist[SYMS];
static unsigned short sym_lc[SYMS];
static size_t nsym;
static long sym_bytes;          /* the input bytes they stand for */
static unsigned lit_freq[286], dist_freq[30];

static unsigned long long obits;
static int ocnt;

static void put_bits(unsigned v, int n)
{
    obits |= (unsigned long long)v << ocnt;
    ocnt += n;
    while (ocnt >= 8) {
        unsigned char b = (unsigned char)(obits & 0xff);
        out_bytes(cout, &b, 1);
        obits >>= 8;
        ocnt -= 8;
    }
}

static void align_bits(void)
{
    if (ocnt > 0)
        put_bits(0, 8 - ocnt);
}

static int len_code(unsigned len)        /* len 3..258: 0..28 */
{
    int c = 0;
    while (c < 28 && len_base[c + 1] <= len)
        c++;
    return c;
}

static int dist_code(unsigned d)         /* 1..32768: 0..29 */
{
    int c = 0;
    while (c < 29 && dist_base[c + 1] <= d)
        c++;
    return c;
}

static unsigned char lcode[MAX_MATCH + 1];  /* length -> code */

/* Huffman code lengths for freq[0..n), none longer than limit */
static void lengths(const unsigned *freq, int n, int limit, unsigned char *len)
{
    unsigned f[320];
    int parent[640], node_count;
    unsigned long w[640];
    int heap[320], hn;

    for (int i = 0; i < n; i++)
        f[i] = freq[i];
    for (;;) {
        memset(len, 0, (size_t)n);
        int used = 0, last = -1;
        for (int i = 0; i < n; i++)
            if (f[i])
                used++, last = i;
        if (used == 0)
            return;
        if (used == 1) {
            len[last] = 1;
            len[last == 0 ? 1 : 0] = 1;
            return;
        }
        /* a heap of nodes by weight */
        hn = 0;
        node_count = n;
        for (int i = 0; i < n; i++) {
            if (!f[i])
                continue;
            w[i] = f[i];
            int k = hn++;
            while (k > 0 && w[heap[(k - 1) / 2]] > w[i]) {
                heap[k] = heap[(k - 1) / 2];
                k = (k - 1) / 2;
            }
            heap[k] = i;
        }
        while (hn > 1) {
            int a, b;
            for (int t = 0; t < 2; t++) {
                int top = heap[0], x = heap[--hn], k = 0;
                for (;;) {
                    int c = 2 * k + 1;
                    if (c >= hn)
                        break;
                    if (c + 1 < hn && w[heap[c + 1]] < w[heap[c]])
                        c++;
                    if (w[heap[c]] >= w[x])
                        break;
                    heap[k] = heap[c];
                    k = c;
                }
                if (hn > 0)
                    heap[k] = x;
                if (t == 0)
                    a = top;
                else
                    b = top;
            }
            int m = node_count++;
            w[m] = w[a] + w[b];
            parent[a] = parent[b] = m;
            int k = hn++;
            while (k > 0 && w[heap[(k - 1) / 2]] > w[m]) {
                heap[k] = heap[(k - 1) / 2];
                k = (k - 1) / 2;
            }
            heap[k] = m;
        }
        int root = heap[0], depth[640], over = 0;
        depth[root] = 0;
        for (int m = node_count - 1; m >= n; m--)
            if (m != root)
                depth[m] = depth[parent[m]] + 1;
        for (int i = 0; i < n; i++)
            if (f[i]) {
                int d = depth[parent[i]] + 1;
                if (d > limit)
                    over = 1;
                len[i] = (unsigned char)d;
            }
        if (!over)
            return;
        for (int i = 0; i < n; i++)
            if (f[i])
                f[i] = (f[i] + 1) / 2;
    }
}

/* canonical codes, bit-reversed for writing */
static void make_codes(const unsigned char *len, int n, unsigned short *code)
{
    int count[MAXBITS + 1] = { 0 }, next[MAXBITS + 1];
    for (int i = 0; i < n; i++)
        count[len[i]]++;
    count[0] = 0;
    int c = 0;
    for (int l = 1; l <= MAXBITS; l++) {
        c = (c + count[l - 1]) << 1;
        next[l] = c;
    }
    for (int i = 0; i < n; i++) {
        if (!len[i])
            continue;
        unsigned v = (unsigned)next[len[i]]++, r = 0;
        for (int k = 0; k < len[i]; k++)
            r = r << 1 | (v >> k & 1);
        code[i] = (unsigned short)r;
    }
}

static void write_symbols(const unsigned char *ll, const unsigned short *lc,
                          const unsigned char *dl, const unsigned short *dc)
{
    for (size_t i = 0; i < nsym; i++) {
        unsigned d = sym_dist[i];
        if (d == 0) {
            put_bits(lc[sym_lc[i]], ll[sym_lc[i]]);
            continue;
        }
        int l = lcode[sym_lc[i] + MIN_MATCH];
        put_bits(lc[257 + l], ll[257 + l]);
        put_bits(sym_lc[i] + MIN_MATCH - len_base[l], len_extra[l]);
        int c = dist_code(d);
        put_bits(dc[c], dl[c]);
        put_bits(d - dist_base[c], dist_extra[c]);
    }
    put_bits(lc[256], ll[256]);
}

/* the bits of the symbols with these lengths */
static unsigned long sym_cost(const unsigned char *ll, const unsigned char *dl)
{
    unsigned long b = 0;
    for (int i = 0; i < 286; i++)
        if (lit_freq[i])
            b += (unsigned long)lit_freq[i] * (ll[i] + (i > 256 ? len_extra[i - 257] : 0));
    for (int i = 0; i < 30; i++)
        if (dist_freq[i])
            b += (unsigned long)dist_freq[i] * (dl[i] + dist_extra[i]);
    return b;
}

/* write the block of symbols, with the raw bytes from block_start */
static void flush_block(bool last)
{
    unsigned char ll[286], dl[30], fl[288], fd[30];
    unsigned short lc[286], dc[30];

    lit_freq[256] = 1;
    lengths(lit_freq, 286, 15, ll);
    lengths(dist_freq, 30, 15, dl);
    int hlit = 286, hdist = 30;
    while (hlit > 257 && ll[hlit - 1] == 0)
        hlit--;
    while (hdist > 1 && dl[hdist - 1] == 0)
        hdist--;

    /* the lengths, run-length coded */
    unsigned char all[316], rl_sym[316], rl_extra[316];
    int na = 0, nr = 0;
    for (int i = 0; i < hlit; i++)
        all[na++] = ll[i];
    for (int i = 0; i < hdist; i++)
        all[na++] = dl[i];
    unsigned cl_freq[19] = { 0 };
    for (int i = 0; i < na; ) {
        int v = all[i], run = 1;
        while (i + run < na && all[i + run] == v)
            run++;
        if (v == 0 && run >= 3) {
            int r = run > 138 ? 138 : run;
            rl_sym[nr] = r >= 11 ? 18 : 17;
            rl_extra[nr++] = (unsigned char)(r >= 11 ? r - 11 : r - 3);
            i += r;
        } else if (v != 0 && run >= 4) {
            rl_sym[nr] = (unsigned char)v, rl_extra[nr++] = 0;
            int r = run - 1 > 6 ? 6 : run - 1;
            rl_sym[nr] = 16, rl_extra[nr++] = (unsigned char)(r - 3);
            i += 1 + r;
        } else {
            rl_sym[nr] = (unsigned char)v, rl_extra[nr++] = 0;
            i++;
        }
    }
    for (int i = 0; i < nr; i++)
        cl_freq[rl_sym[i]]++;
    unsigned char cll[19];
    unsigned short clc[19];
    lengths(cl_freq, 19, 7, cll);
    int hclen = 19;
    while (hclen > 4 && cll[cl_order[hclen - 1]] == 0)
        hclen--;

    unsigned long dyn = 3 + 14 + 3UL * (unsigned)hclen + sym_cost(ll, dl);
    for (int i = 0; i < nr; i++)
        dyn += cll[rl_sym[i]] + (rl_sym[i] == 16 ? 2 : rl_sym[i] == 17 ? 3 : rl_sym[i] == 18 ? 7 : 0);
    for (int i = 0; i < 288; i++)     /* 286 and 287 count for the codes */
        fl[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    for (int i = 0; i < 30; i++)
        fd[i] = 5;
    unsigned long fix = 3 + sym_cost(fl, fd);
    unsigned long raw = (unsigned long)sym_bytes;
    unsigned long sto = 3 + 7 + 32 + 8 * raw;

    if (raw > 65535)            /* stored blocks of 65535 bytes at most */
        sto += (raw / 65535) * (3 + 7 + 32);
    if (sto <= fix && sto <= dyn) {
        const unsigned char *p = win + block_start;
        do {
            unsigned n = raw > 65535 ? 65535 : (unsigned)raw;
            raw -= n;
            put_bits(last && raw == 0, 1);
            put_bits(0, 2);
            align_bits();
            put_bits(n, 16);
            put_bits(~n & 0xffff, 16);
            out_bytes(cout, p, n);
            p += n;
        } while (raw > 0);
    } else if (fix <= dyn) {
        unsigned short flc[288], fdc[30];
        make_codes(fl, 288, flc);
        make_codes(fd, 30, fdc);
        put_bits(last, 1);
        put_bits(1, 2);
        write_symbols(fl, flc, fd, fdc);
    } else {
        make_codes(ll, 286, lc);
        make_codes(dl, 30, dc);
        make_codes(cll, 19, clc);
        put_bits(last, 1);
        put_bits(2, 2);
        put_bits((unsigned)(hlit - 257), 5);
        put_bits((unsigned)(hdist - 1), 5);
        put_bits((unsigned)(hclen - 4), 4);
        for (int i = 0; i < hclen; i++)
            put_bits(cll[cl_order[i]], 3);
        for (int i = 0; i < nr; i++) {
            put_bits(clc[rl_sym[i]], cll[rl_sym[i]]);
            if (rl_sym[i] >= 16)
                put_bits(rl_extra[i], rl_sym[i] == 16 ? 2 : rl_sym[i] == 17 ? 3 : 7);
        }
        write_symbols(ll, lc, dl, dc);
    }
    block_start += sym_bytes;
    sym_bytes = 0;
    nsym = 0;
    memset(lit_freq, 0, sizeof lit_freq);
    memset(dist_freq, 0, sizeof dist_freq);
}

static void tally(unsigned dist, unsigned lc)
{
    sym_dist[nsym] = (unsigned short)dist;
    sym_lc[nsym] = (unsigned short)lc;
    nsym++;
    sym_bytes += dist == 0 ? 1 : (long)lc + MIN_MATCH;
    if (dist == 0) {
        lit_freq[lc]++;
    } else {
        lit_freq[257 + lcode[lc + MIN_MATCH]]++;
        dist_freq[dist_code(dist)]++;
    }
}

/* read more input into the window, sliding it down when needed */
static void fill(void)
{
    if (strstart >= 2 * CWSIZE - MIN_LOOKAHEAD) {
        if (block_start < CWSIZE)
            flush_block(false);
        memmove(win, win + CWSIZE, CWSIZE);
        strstart -= CWSIZE;
        block_start -= CWSIZE;
        match_start -= CWSIZE;
        for (int i = 0; i < HSIZE; i++)
            head[i] = head[i] >= CWSIZE ? head[i] - CWSIZE : NIL;
        for (int i = 0; i < CWSIZE; i++)
            prev[i] = prev[i] >= CWSIZE ? prev[i] - CWSIZE : NIL;
    }
    while (!in_done && lookahead < MIN_LOOKAHEAD) {
        long end = strstart + lookahead;
        if (end >= 2 * CWSIZE)
            break;
        int c = in_byte(cin);
        if (c < 0) {
            in_done = true;
            break;
        }
        win[end] = (unsigned char)c;
        lookahead++;
        /* take what the input buffer has in one go */
        size_t k = cin->len - cin->pos;
        if (k > (size_t)(2 * CWSIZE - end - 1))
            k = (size_t)(2 * CWSIZE - end - 1);
        memcpy(win + end + 1, cin->buf + cin->pos, k);
        cin->pos += k;
        cin->count += k;
        lookahead += (long)k;
    }
}

#define HASH(h, c) ((((h) << 5) ^ (c)) & (HSIZE - 1))

/* insert the string at strstart; the previous one with the same hash */
static int insert(long pos)
{
    hash = HASH(HASH(HASH(0u, win[pos]), win[pos + 1]), win[pos + 2]);
    int m = head[hash];
    prev[pos & WMASK] = m;
    head[hash] = (int)pos;
    return m;
}

static int max_chain, good_length, nice_length, max_lazy;

/* the longest match for strstart, starting from cur, better than prevlen */
static int longest(int cur, int prevlen)
{
    int chain = max_chain, best = prevlen;
    long limit = strstart > MAX_DIST ? strstart - MAX_DIST : 0;
    int maxlen = lookahead < MAX_MATCH ? (int)lookahead : MAX_MATCH;
    if (prevlen >= good_length)
        chain >>= 2;
    if (best >= maxlen)
        return best;
    const unsigned char *s = win + strstart;
    while (cur != NIL && cur >= limit && chain-- > 0) {
        const unsigned char *m = win + cur;
        if (m[best] == s[best] && m[0] == s[0] && m[1] == s[1]) {
            int l = 2;
            while (l < maxlen && m[l] == s[l])
                l++;
            if (l > best) {
                best = l;
                match_start = cur;
                if (l >= nice_length || l >= maxlen)
                    break;
            }
        }
        cur = prev[cur & WMASK];
    }
    return best;
}

void gzip_compress(struct in *in, struct out *out, int level, time_t mtime)
{
    static const int config[10][4] = {      /* good, lazy, nice, chain */
        { 0, 0, 0, 0 }, { 4, 4, 8, 4 }, { 4, 5, 16, 8 }, { 4, 6, 32, 32 },
        { 4, 4, 16, 16 }, { 8, 16, 32, 32 }, { 8, 16, 128, 128 },
        { 8, 32, 128, 256 }, { 32, 128, 258, 1024 }, { 32, 258, 258, 4096 }
    };
    crc_init();
    good_length = config[level][0];
    max_lazy = config[level][1];
    nice_length = config[level][2];
    max_chain = config[level][3];
    for (int l = MIN_MATCH; l <= MAX_MATCH; l++)
        lcode[l] = (unsigned char)len_code((unsigned)l);

    cin = in, cout = out;
    unsigned long long mt = mtime > 0 ? (unsigned long long)mtime : 0;
    if (mt > 0xffffffffULL)
        mt = 0;
    unsigned char hdr[10] = { 0x1f, 0x8b, 8, 0,
        (unsigned char)mt, (unsigned char)(mt >> 8), (unsigned char)(mt >> 16),
        (unsigned char)(mt >> 24), (unsigned char)(level == 9 ? 2 : level == 1 ? 4 : 0), 3 };
    out_bytes(out, hdr, sizeof hdr);
    obits = 0, ocnt = 0;
    for (int i = 0; i < HSIZE; i++)
        head[i] = NIL;
    strstart = block_start = 0;
    lookahead = 0;
    in_done = false;
    nsym = 0;
    memset(lit_freq, 0, sizeof lit_freq);
    memset(dist_freq, 0, sizeof dist_freq);
    ccrc = 0;

    int match_len = MIN_MATCH - 1, prev_len;
    long prev_match;
    bool avail = false;         /* a literal at strstart - 1 waits */
    long crc_done = 0;          /* bytes of win in the CRC */
    sym_bytes = 0;
    match_start = 0;
    for (;;) {
        if (lookahead < MIN_LOOKAHEAD) {
            long before = strstart;
            fill();
            crc_done -= before - strstart;      /* after a slide */
            ccrc = crc32_update(ccrc, win + crc_done, (size_t)(strstart + lookahead - crc_done));
            crc_done = strstart + lookahead;
            if (lookahead == 0)
                break;
        }
        int cur = lookahead >= MIN_MATCH ? insert(strstart) : NIL;
        prev_len = match_len;
        prev_match = match_start;
        match_len = MIN_MATCH - 1;
        if (cur != NIL && prev_len < max_lazy && strstart - cur <= MAX_DIST) {
            match_len = longest(cur, prev_len);
            /* a short match far away costs more than its literals */
            if (match_len == MIN_MATCH && strstart - match_start > 4096)
                match_len = MIN_MATCH - 1;
        }
        if (prev_len >= MIN_MATCH && match_len <= prev_len) {
            /* the match at strstart - 1 is the one */
            long max_insert = strstart + lookahead - MIN_MATCH;
            tally((unsigned)(strstart - 1 - prev_match), (unsigned)(prev_len - MIN_MATCH));
            lookahead -= prev_len - 1;
            for (int k = prev_len - 2; k > 0; k--)
                if (++strstart <= max_insert)
                    insert(strstart);
            strstart++;
            avail = false;
            match_len = MIN_MATCH - 1;
            if (nsym == SYMS)
                flush_block(false);
        } else {
            if (avail) {
                tally(0, win[strstart - 1]);
                if (nsym == SYMS)
                    flush_block(false);
            }
            avail = true;
            strstart++;
            lookahead--;
        }
    }
    if (avail)
        tally(0, win[strstart - 1]);
    flush_block(true);
    align_bits();
    unsigned char tr[8];
    uintmax_t size = in->count;
    for (int i = 0; i < 4; i++) {
        tr[i] = (unsigned char)(ccrc >> (8 * i));
        tr[4 + i] = (unsigned char)(size >> (8 * i));
    }
    out_bytes(out, tr, sizeof tr);
}
