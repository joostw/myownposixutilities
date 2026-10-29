/*
 * archive.c - reading and writing the archive file, with blocking on
 * output and format detection on input.
 */
#include "pax.h"

int arc_fd = -1;
long blocksize;
int arc_fmt;
dev_t arc_dev;
ino_t arc_ino;
int arc_is_file;

static const char *arc_name = "standard input";

/* ---------- reading ---------- */

static unsigned char *pb;     /* pushed-back bytes */
static size_t pblen, pbcap;
static off_t roff;            /* logical read offset */
static int rseekable;

void arc_open_read(const char *name)
{
    struct stat st;
    if (name && strcmp(name, "-") != 0) {
        arc_fd = open(name, O_RDONLY);
        if (arc_fd < 0) {
            warn_("%s", name);
            exit(1);
        }
        arc_name = name;
    } else {
        arc_fd = 0;
        arc_name = "standard input";
    }
    if (fstat(arc_fd, &st) == 0) {
        rseekable = S_ISREG(st.st_mode);
        arc_dev = st.st_dev;
        arc_ino = st.st_ino;
        arc_is_file = S_ISREG(st.st_mode);
    }
}

void arc_unread(const void *p, size_t n)
{
    if (pblen + n > pbcap) {
        pbcap = pblen + n + 512;
        pb = xrealloc(pb, pbcap);
    }
    memmove(pb + n, pb, pblen);
    memcpy(pb, p, n);
    pblen += n;
    roff -= (off_t)n;
}

int arc_read(void *p, size_t n)
{
    unsigned char *q = p;
    size_t got = 0;
    if (pblen) {
        size_t k = pblen < n ? pblen : n;
        memcpy(q, pb, k);
        memmove(pb, pb + k, pblen - k);
        pblen -= k;
        got = k;
    }
    while (got < n) {
        ssize_t r = read(arc_fd, q + got, n - got);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            warn_("%s: read error", arc_name);
            exit(1);
        }
        if (r == 0)
            break;
        got += (size_t)r;
    }
    roff += (off_t)got;
    if (got == 0)
        return 0;
    if (got < n) {
        /* a partial record: report it as end of archive to the caller */
        memset(q + got, 0, n - got);
        return -1;
    }
    return 1;
}

void arc_read_full(void *p, size_t n)
{
    if (arc_read(p, n) != 1)
        errx_("%s: unexpected end of archive", arc_name);
}

off_t arc_offset(void) { return roff; }

void arc_skip(off_t n)
{
    char buf[65536];
    if (n <= 0)
        return;
    if (pblen) {
        size_t k = pblen < (size_t)n ? pblen : (size_t)n;
        memmove(pb, pb + k, pblen - k);
        pblen -= k;
        n -= (off_t)k;
        roff += (off_t)k;
    }
    if (rseekable && n > (off_t)sizeof buf) {
        struct stat st;
        off_t here = lseek(arc_fd, 0, SEEK_CUR);
        if (here >= 0 && fstat(arc_fd, &st) == 0 && S_ISREG(st.st_mode) && here + n > st.st_size)
            errx_("%s: unexpected end of archive", arc_name);
        if (lseek(arc_fd, n, SEEK_CUR) >= 0) {
            roff += n;
            return;
        }
    }
    while (n > 0) {
        size_t k = n > (off_t)sizeof buf ? sizeof buf : (size_t)n;
        if (arc_read(buf, k) != 1)
            errx_("%s: unexpected end of archive", arc_name);
        n -= (off_t)k;
    }
}

/* the archive format, from its first bytes */
int detect_format(void)
{
    unsigned char h[512];
    int r = arc_read(h, sizeof h), fmt = F_NONE;
    size_t n = r == 1 ? sizeof h : 0;
    if (r == -1) {
        /* short archive: find how much was read */
        n = (size_t)(arc_offset());
        if (n > sizeof h)
            n = sizeof h;
    }
    if (r == 0)
        errx_("%s: empty archive", arc_name);
    if (n >= 6 && memcmp(h, "070707", 6) == 0)
        fmt = F_CPIO;
    else if (n >= 6 && (memcmp(h, "070701", 6) == 0 || memcmp(h, "070702", 6) == 0))
        fmt = F_NEWC;
    else if (n == 512) {
        unsigned long sum = 0, stored = 0;
        int i, have = 0;
        for (i = 0; i < 512; i++)
            sum += (i >= 148 && i < 156) ? ' ' : h[i];
        for (i = 148; i < 156; i++) {
            if (h[i] >= '0' && h[i] <= '7') {
                stored = stored * 8 + (unsigned long)(h[i] - '0');
                have = 1;
            } else if (have || (h[i] != ' ' && h[i] != 0))
                break;
        }
        if (have && stored == sum)
            fmt = memcmp(h + 257, "ustar", 5) == 0 ? F_USTAR : F_OLDTAR;
        else {
            int zero = 1;
            for (i = 0; i < 512; i++)
                if (h[i])
                    zero = 0;
            if (zero)
                fmt = F_USTAR;   /* an empty tar archive */
        }
    }
    arc_unread(h, n);
    if (fmt == F_NONE)
        errx_("%s: unknown archive format", arc_name);
    return fmt;
}

/* ---------- writing ---------- */

static unsigned char *wbuf;
static size_t wlen;
static off_t woff;            /* logical bytes written */
static int wfile;

void arc_open_write(const char *name, int append)
{
    struct stat st;
    if (name && strcmp(name, "-") != 0) {
        arc_fd = open(name, append ? O_RDWR : (O_WRONLY | O_CREAT | O_TRUNC), 0666);
        if (arc_fd < 0) {
            warn_("%s", name);
            exit(1);
        }
        arc_name = name;
    } else {
        arc_fd = append ? 0 : 1;
        arc_name = append ? "standard input" : "standard output";
    }
    if (fstat(arc_fd, &st) == 0) {
        arc_dev = st.st_dev;
        arc_ino = st.st_ino;
        arc_is_file = S_ISREG(st.st_mode);
        wfile = S_ISREG(st.st_mode);
        rseekable = wfile;
    }
    wbuf = xmalloc((size_t)blocksize);
    wlen = 0;
}

static void flush_block(void)
{
    size_t off = 0;
    while (off < wlen) {
        ssize_t r = write(arc_fd, wbuf + off, wlen - off);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            warn_("%s: write error", arc_name);
            exit(1);
        }
        off += (size_t)r;
    }
    wlen = 0;
}

void arc_write(const void *p, size_t n)
{
    const unsigned char *q = p;
    while (n) {
        size_t k = (size_t)blocksize - wlen;
        if (k > n)
            k = n;
        memcpy(wbuf + wlen, q, k);
        wlen += k;
        q += k;
        n -= k;
        woff += (off_t)k;
        if (wlen == (size_t)blocksize)
            flush_block();
    }
}

void arc_pad_write(size_t n)
{
    static const unsigned char z[512];
    while (n) {
        size_t k = n > sizeof z ? sizeof z : n;
        arc_write(z, k);
        n -= k;
    }
}

/* appending: continue writing at logical offset off of the archive */
void arc_seek_write(off_t off)
{
    off_t bstart = off - off % blocksize;
    ssize_t r;
    if (lseek(arc_fd, bstart, SEEK_SET) < 0)
        errx_("%s: cannot seek to append", arc_name);
    wlen = (size_t)(off - bstart);
    if (wlen) {
        size_t got = 0;
        while (got < wlen) {
            r = read(arc_fd, wbuf + got, wlen - got);
            if (r <= 0)
                errx_("%s: cannot read the archive to append", arc_name);
            got += (size_t)r;
        }
        if (lseek(arc_fd, bstart, SEEK_SET) < 0)
            errx_("%s: cannot seek to append", arc_name);
    }
    woff = off;
}

void arc_finish_write(void)
{
    if (wlen) {
        memset(wbuf + wlen, 0, (size_t)blocksize - wlen);
        wlen = (size_t)blocksize;
        flush_block();
    }
    if (wfile) {
        off_t end = lseek(arc_fd, 0, SEEK_CUR);
        if (end >= 0 && ftruncate(arc_fd, end) < 0) {
            /* not fatal */
        }
    }
    if (arc_fd > 2 && close(arc_fd) < 0)
        warn_("%s", arc_name);
}
