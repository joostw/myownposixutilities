/*
 * compress.c - compress, uncompress, zcat: compress and expand data
 * (POSIX.1-2024, XCU compress, uncompress, zcat; XSI)
 *
 * Synopsis:
 *   compress [-fv] [-b value] [-g | -m algo] [file...]
 *   compress -c [-fv] [-b value] [-g | -m algo] [file]
 *   compress -d [-cfv] [file...]
 *   uncompress [-cdfv] [file...]
 *   zcat [file...]
 *
 * One program; uncompress (compress -d) and zcat (compress -c -d) are
 * links to it.  The algorithms are lzw (the .Z format of compress 4.0,
 * -b 9 to 16 bits, 16 by default) and deflate or gzip (the gzip format of
 * RFC 1952, .gz, -b 1 to 9, 6 by default); see lzw.c and gzip.c.  When
 * expanding, the format is found from the data (0x1f 0x9d or 0x1f 0x8b),
 * whatever the name of the file.
 *
 * A file is compressed to the name with the suffix added; the output is
 * made with O_EXCL, given the owner (when allowed), mode and times of the
 * input, and then the input is removed.  When expanding, a name that
 * ends in .Z or .gz loses it; any other name (or a missing one not ending
 * in .Z) is the output name, and the input is that name with .Z added.
 *
 * Choices where the standard leaves room:
 *  - The usage status is 1 for compress, as 2 means "not compressed", and
 *    2 for uncompress and zcat, as for the other utilities; an unknown
 *    -m algo is status 3, before any file is touched.
 *  - A file is not compressed (status 2, without -f) when the output
 *    would be larger than the input; with -c or the standard input the
 *    data has been written by then, and only the status says so.
 *  - Compressing a file that already ends in the suffix, or one that is
 *    not a regular file, is an error, and so is a symbolic link unless
 *    -f is given (with -f it is followed and the link removed).
 *  - A file with more than one link is an error without -f; with -f it is
 *    processed and that link removed.
 *  - With -f, an input that cannot be removed leaves both files, with a
 *    warning, and is not an error.
 *  - With -c, expanding a file that does not exist and does not end in .Z
 *    tries the name with .Z added.
 *  - -v writes "name: Compression: 12.34% -- replaced with name.Z" (just
 *    the percentage for the standard input and -c), and "name.Z: --
 *    replaced with name" when expanding.
 *  - Input after a gzip member must be another member: anything else is
 *    an error; an empty input is not in a compressed format.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "compress.h"

static const char *prog = "compress";
static bool opt_c, opt_d, opt_f, opt_v;
static bool gz;                 /* the algorithm: gzip, else lzw */
static int value = -1;          /* -b */
static int status;

/* ---- buffered input and output ---- */

int in_refill(struct in *in)
{
    if (in->eof || in->err)
        return -1;
    for (;;) {
        ssize_t n = read(in->fd, in->buf, sizeof in->buf);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            in->err = errno;
            return -1;
        }
        if (n == 0) {
            in->eof = true;
            return -1;
        }
        in->len = (size_t)n;
        in->pos = 1;
        in->count++;
        return in->buf[0];
    }
}

static void out_write(struct out *out, const unsigned char *p, size_t n)
{
    while (n > 0 && !out->err) {
        ssize_t w = write(out->fd, p, n);
        if (w < 0) {
            if (errno != EINTR)
                out->err = errno;
            continue;
        }
        p += w;
        n -= (size_t)w;
    }
}

void out_bytes(struct out *out, const void *p, size_t n)
{
    const unsigned char *s = p;
    out->count += n;
    if (out->len + n > sizeof out->buf) {
        out_write(out, out->buf, out->len);
        out->len = 0;
        if (n >= sizeof out->buf) {
            out_write(out, s, n);
            return;
        }
    }
    memcpy(out->buf + out->len, s, n);
    out->len += n;
}

void out_flush(struct out *out)
{
    out_write(out, out->buf, out->len);
    out->len = 0;
}

static struct in in;
static struct out out;

/* ---- the files ---- */

static void warn(const char *name, const char *msg)
{
    fprintf(stderr, "%s: %s: %s\n", prog, name, msg);
}

static void error(const char *name, const char *msg)
{
    warn(name, msg);
    if (status < 1 || status == 2)
        status = 1;
}

static void usage(void)
{
    if (strcmp(prog, "zcat") == 0)
        fprintf(stderr, "usage: zcat [file...]\n");
    else if (strcmp(prog, "uncompress") == 0)
        fprintf(stderr, "usage: uncompress [-cdfv] [file...]\n");
    else
        fprintf(stderr, "usage: compress [-cfv] [-b value] [-g | -m algo] [file...]\n"
                        "       compress -d [-cfv] [file...]\n");
    exit(strcmp(prog, "compress") == 0 ? 1 : 2);
}

static bool yes(void)
{
    static regex_t re;
    static int ready;           /* 0 not yet, 1 regex, -1 the fallback */
    if (!ready) {
        const char *y = nl_langinfo(YESEXPR);
        ready = (y && *y && regcomp(&re, y, REG_EXTENDED | REG_NOSUB) == 0) ? 1 : -1;
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, stdin);
    bool r = false;
    if (n > 0) {
        if (line[n - 1] == '\n')
            line[n - 1] = '\0';
        r = ready == 1 ? regexec(&re, line, 0, NULL, 0) == 0 : (line[0] == 'y' || line[0] == 'Y');
    }
    free(line);
    return r;
}

static bool ends_with(const char *s, const char *suf)
{
    size_t n = strlen(s), k = strlen(suf);
    return n > k && strcmp(s + n - k, suf) == 0;
}

/* the percentage saved, as "12.34" */
static void ratio(char *buf, size_t size, uintmax_t before, uintmax_t after)
{
    if (before == 0) {
        snprintf(buf, size, "0.00");
        return;
    }
    double r = 100.0 * ((double)before - (double)after) / (double)before;
    snprintf(buf, size, "%.2f", r);
}

/* compress or expand in to out; false (with a message) on an error */
static bool convert(const char *iname, const char *oname, time_t mtime)
{
    if (!opt_d) {
        if (gz)
            gzip_compress(&in, &out, value, mtime);
        else
            lzw_compress(&in, &out, value);
    } else {
        int a = in_byte(&in), b = in_byte(&in);
        const char *msg = NULL;
        if (!in.err) {
            if (a == 0x1f && b == 0x9d)
                msg = lzw_decompress(&in, &out);
            else if (a == 0x1f && b == 0x8b)
                msg = gzip_decompress(&in, &out);
            else
                msg = "not in compressed format";
        }
        if (msg && !in.err) {
            out_flush(&out);
            error(iname, msg);
            return false;
        }
    }
    out_flush(&out);
    if (in.err) {
        error(iname, strerror(in.err));
        return false;
    }
    if (out.err) {
        error(oname, strerror(out.err));
        return false;
    }
    return true;
}

static void reset(int ifd, int ofd)
{
    in.fd = ifd, in.pos = in.len = 0, in.eof = false, in.err = 0, in.count = 0;
    out.fd = ofd, out.len = 0, out.err = 0, out.count = 0;
}

/* the standard input to the standard output, or a file with -c */
static void to_stdout(int fd, const char *name, time_t mtime)
{
    reset(fd, 1);
    if (!convert(name, "standard output", mtime))
        return;
    if (!opt_d) {
        if (opt_v) {
            char r[32];
            ratio(r, sizeof r, in.count, out.count);
            fprintf(stderr, "%s: Compression: %s%%\n", name, r);
        }
        if (!opt_f && out.count > in.count && status == 0)
            status = 2;
    }
}

/* the name that will be made, or NULL with a message */
static char *out_name(const char *name, const char **iname)
{
    char *o;
    *iname = name;
    if (!opt_d) {
        const char *suf = gz ? ".gz" : ".Z";
        if (ends_with(name, suf)) {
            fprintf(stderr, "%s: %s: already has %s suffix -- no change\n", prog, name, suf);
            if (status != 1)
                status = 1;
            return NULL;
        }
        o = malloc(strlen(name) + 4);
        if (!o)
            abort();
        strcpy(o, name);
        strcat(o, suf);
        return o;
    }
    struct stat st;
    if (ends_with(name, ".Z") || (ends_with(name, ".gz") && stat(name, &st) == 0)) {
        size_t k = strlen(name) - (ends_with(name, ".Z") ? 2 : 3);
        o = malloc(k + 1);
        if (!o)
            abort();
        memcpy(o, name, k);
        o[k] = '\0';
        return o;
    }
    /* the input is name.Z (obsolescent) */
    char *i = malloc(strlen(name) + 3);
    o = strdup(name);
    if (!i || !o)
        abort();
    strcpy(i, name);
    strcat(i, ".Z");
    *iname = i;
    return o;
}

/* is the last component of path too long, with the suffix? */
static bool too_long(const char *path)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char *dir = strdup(path);
    if (!dir)
        abort();
    char *sl = strrchr(dir, '/');
    if (sl)
        sl[sl == dir ? 1 : 0] = '\0';
    errno = 0;
    long max = pathconf(sl ? dir : ".", _PC_NAME_MAX);
    free(dir);
    if (max < 0)
        max = errno ? NAME_MAX : -1;
    return max >= 0 && strlen(base) > (size_t)max;
}

static void do_file(const char *name)
{
    if (strcmp(name, "-") == 0) {
        to_stdout(0, "standard input", 0);
        return;
    }
    const char *iname;
    struct stat st;
    if (opt_c) {
        iname = name;
        if (opt_d && stat(name, &st) != 0 && errno == ENOENT && !ends_with(name, ".Z")) {
            char *z = malloc(strlen(name) + 3);
            if (!z)
                abort();
            strcpy(z, name);
            strcat(z, ".Z");
            if (stat(z, &st) == 0)
                iname = z;
        }
        int fd = open(iname, O_RDONLY);
        if (fd < 0) {
            error(iname, strerror(errno));
            return;
        }
        if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
            error(iname, "is a directory");
            close(fd);
            return;
        }
        to_stdout(fd, iname, S_ISREG(st.st_mode) ? st.st_mtime : 0);
        close(fd);
        return;
    }

    char *oname = out_name(name, &iname);
    if (!oname)
        return;
    if (lstat(iname, &st) != 0) {
        error(iname, strerror(errno));
        goto out;
    }
    if (S_ISLNK(st.st_mode)) {
        if (!opt_f) {
            error(iname, "is a symbolic link -- not changed");
            goto out;
        }
        if (stat(iname, &st) != 0) {
            error(iname, strerror(errno));
            goto out;
        }
    }
    if (!S_ISREG(st.st_mode)) {
        error(iname, "not a regular file -- not changed");
        goto out;
    }
    if (st.st_nlink > 1 && !opt_f) {
        fprintf(stderr, "%s: %s: has %ju other links -- not changed\n", prog, iname,
                (uintmax_t)st.st_nlink - 1);
        if (status != 1)
            status = 1;
        goto out;
    }
    if (too_long(oname)) {
        error(oname, strerror(ENAMETOOLONG));
        goto out;
    }
    int ifd = open(iname, O_RDONLY);
    if (ifd < 0) {
        error(iname, strerror(errno));
        goto out;
    }
    struct stat ost;
    if (lstat(oname, &ost) == 0) {
        if (!opt_f) {
            bool tty = isatty(0);
            if (tty)
                fprintf(stderr, "%s: %s already exists; overwrite (y or n)? ", prog, oname);
            if (!tty || !yes()) {
                error(oname, tty ? "not overwritten" : "already exists");
                close(ifd);
                goto out;
            }
        }
        if (unlink(oname) != 0) {
            error(oname, strerror(errno));
            close(ifd);
            goto out;
        }
    }
    int ofd = open(oname, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (ofd < 0) {
        error(oname, strerror(errno));
        close(ifd);
        goto out;
    }
    reset(ifd, ofd);
    bool ok = convert(iname, oname, st.st_mtime);
    close(ifd);
    if (ok && !opt_d && !opt_f && out.count > in.count) {
        if (opt_v)
            fprintf(stderr, "%s: -- not compressed, it would grow\n", iname);
        if (status == 0)
            status = 2;
        ok = false;
    }
    if (ok) {
        /* the owner, mode and times of the input */
        mode_t mode = st.st_mode & 07777;
        if (fchown(ofd, st.st_uid, st.st_gid) != 0)
            mode &= ~(mode_t)(S_ISUID | S_ISGID);
        struct timespec ts[2] = { st.st_atim, st.st_mtim };
        if (fchmod(ofd, mode) != 0 || futimens(ofd, ts) != 0) {
            error(oname, strerror(errno));
            ok = false;
        }
    }
    if (close(ofd) != 0 && ok) {
        error(oname, strerror(errno));
        ok = false;
    }
    if (!ok) {
        unlink(oname);
        goto out;
    }
    if (unlink(iname) != 0) {
        int e = errno;
        if (opt_f) {
            fprintf(stderr, "%s: %s: warning: cannot be removed: %s\n", prog, iname, strerror(e));
        } else {
            unlink(oname);
            error(iname, strerror(e));
            goto out;
        }
    }
    if (opt_v) {
        if (opt_d) {
            fprintf(stderr, "%s: -- replaced with %s\n", iname, oname);
        } else {
            char r[32];
            ratio(r, sizeof r, in.count, out.count);
            fprintf(stderr, "%s: Compression: %s%% -- replaced with %s\n", iname, r, oname);
        }
    }
out:
    if (iname != name)
        free((char *)iname);
    free(oname);
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    const char *p = strrchr(argv[0], '/');
    p = p ? p + 1 : argv[0];
    const char *opts = "b:cdfgm:v";
    if (strcmp(p, "uncompress") == 0)
        prog = "uncompress", opt_d = true, opts = "cdfv";
    else if (strcmp(p, "zcat") == 0)
        prog = "zcat", opt_d = opt_c = true, opts = "";
    const char *bopt = NULL, *algo = NULL;
    int c;
    while ((c = getopt(argc, argv, opts)) != -1) {
        switch (c) {
        case 'b': bopt = optarg; break;
        case 'c': opt_c = true; break;
        case 'd': opt_d = true; break;
        case 'f': opt_f = true; break;
        case 'g': algo = "gzip"; break;
        case 'm': algo = optarg; break;
        case 'v': opt_v = true; break;
        default: usage();
        }
    }
    if (algo) {
        if (strcmp(algo, "gzip") == 0 || strcmp(algo, "deflate") == 0) {
            gz = true;
        } else if (strcmp(algo, "lzw") != 0) {
            fprintf(stderr, "%s: %s: unknown algorithm\n", prog, algo);
            return 3;
        }
    }
    int lo = gz ? 1 : 9, hi = gz ? 9 : 16;
    value = gz ? 6 : 16;
    if (bopt) {
        char *end;
        errno = 0;
        long v = strtol(bopt, &end, 10);
        if (*bopt == '\0' || *end || errno || v < lo || v > hi) {
            fprintf(stderr, "%s: -b %s: the value must be %d to %d\n", prog, bopt, lo, hi);
            usage();
        }
        value = (int)v;
    }

    if (optind == argc) {
        to_stdout(0, "standard input", 0);
    } else {
        for (int i = optind; i < argc; i++)
            do_file(argv[i]);
    }
    return status;
}
