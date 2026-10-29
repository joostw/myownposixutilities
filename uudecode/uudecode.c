/*
 * uudecode.c - uudecode: decode a binary file (POSIX.1-2024, XCU uudecode)
 *
 * Synopsis: uudecode [-o outfile] [file]
 *
 * The input is searched for a "begin MODE NAME" line (the historical
 * encoding) or a "begin-base64 MODE NAME" line; the data after it is
 * decoded up to the "end" line (after the line of length 0) or the "===="
 * line, into NAME, or the -o outfile; "-" and /dev/stdout are the standard
 * output.  The file gets the mode bits of MODE exactly (with fchmod(), so
 * the mask has no say), also when it existed and could be written.
 *
 * Choices where the standard leaves room:
 *  - MODE is octal or symbolic, as for chmod; the symbolic operations start
 *    from 0, and a clause without who applies to all (the mask is not
 *    used).
 *  - NAME is the rest of the begin line after MODE and one blank.
 *  - Lines before the begin line are skipped, and what follows the end is
 *    not read; a second encoded file in the input is not decoded.
 *  - In the historical encoding a space and '`' are both the value 0, and
 *    the characters of a line beyond its length are ignored; in Base64,
 *    characters not of the alphabet are ignored, and "=" pads.
 *  - No begin line, a missing end line, a line or quantum that is not
 *    valid, a file that cannot be written: a diagnostic, status 1.  Data
 *    decoded before an error stays in the output file.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *inname = "standard input";

static void die(const char *fmt, const char *arg)
{
    fprintf(stderr, "uudecode: ");
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    exit(1);
}

/* ---- the mode: octal, or symbolic as for chmod, from 0 ---- */

static bool parse_mode(const char *s, mode_t *out)
{
    if (*s >= '0' && *s <= '7') {
        mode_t m = 0;
        for (const char *p = s; *p; p++) {
            if (*p < '0' || *p > '7')
                return false;
            m = m * 8 + (mode_t)(*p - '0');
            if (m > 07777)
                return false;
        }
        *out = m;
        return true;
    }
    mode_t m = 0;
    const char *p = s;
    for (;;) {
        int who = 0;
        for (;; p++) {
            if (*p == 'u') who |= 1;
            else if (*p == 'g') who |= 2;
            else if (*p == 'o') who |= 4;
            else if (*p == 'a') who |= 7;
            else break;
        }
        if (!who)
            who = 7;
        mode_t cls = 0;
        if (who & 1) cls |= S_IRWXU | S_ISUID;
        if (who & 2) cls |= S_IRWXG | S_ISGID;
        if (who & 4) cls |= S_IRWXO | S_ISVTX;
        if (*p != '+' && *p != '-' && *p != '=')
            return false;
        while (*p == '+' || *p == '-' || *p == '=') {
            char op = *p++;
            mode_t perm = 0;
            if (*p == 'u' || *p == 'g' || *p == 'o') {
                mode_t c = *p == 'u' ? (m >> 6) & 7 : *p == 'g' ? (m >> 3) & 7 : m & 7;
                perm = c << 6 | c << 3 | c;
                p++;
            } else
                for (;; p++) {
                    if (*p == 'r') perm |= 0444;
                    else if (*p == 'w') perm |= 0222;
                    else if (*p == 'x' || *p == 'X') perm |= 0111;
                    else if (*p == 's') perm |= S_ISUID | S_ISGID;
                    else if (*p == 't') perm |= S_ISVTX;
                    else break;
                }
            perm &= cls;
            if (op == '+')
                m |= perm;
            else if (op == '-')
                m &= ~perm;
            else
                m = (m & ~cls) | perm;
        }
        if (!*p)
            break;
        if (*p != ',')
            return false;
        p++;
    }
    *out = m;
    return true;
}

/* ---- output ---- */

static FILE *out;
static const char *outname;

static void put(const unsigned char *b, size_t n)
{
    if (n && fwrite(b, 1, n, out) != n)
        die("%s: write error", outname);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    const char *ofile = NULL;
    int c;
    while ((c = getopt(argc, argv, "o:")) != -1) {
        if (c != 'o') {
            fprintf(stderr, "usage: uudecode [-o outfile] [file]\n");
            return 2;
        }
        ofile = optarg;
    }
    if (argc - optind > 1) {
        fprintf(stderr, "usage: uudecode [-o outfile] [file]\n");
        return 2;
    }
    FILE *in = stdin;
    if (optind < argc) {
        inname = argv[optind];
        if (!(in = fopen(inname, "r")))
            die("%s", strerror(errno));
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    bool b64 = false, found = false;
    mode_t mode = 0;
    char *name = NULL;
    while ((n = getline(&line, &cap, in)) >= 0) {
        line[strcspn(line, "\r\n")] = '\0';
        const char *rest;
        if (strncmp(line, "begin-base64 ", 13) == 0) {
            b64 = true;
            rest = line + 13;
        } else if (strncmp(line, "begin ", 6) == 0) {
            b64 = false;
            rest = line + 6;
        } else
            continue;
        const char *sp = strchr(rest, ' ');
        if (!sp || !sp[1])
            continue;
        char *m = strndup(rest, (size_t)(sp - rest));
        if (!m || !parse_mode(m, &mode)) {
            free(m);
            continue;
        }
        free(m);
        name = strdup(sp + 1);
        found = true;
        break;
    }
    if (!found)
        die("%s: no begin line", inname);
    outname = ofile ? ofile : name;
    bool to_stdout = strcmp(outname, "-") == 0 || strcmp(outname, "/dev/stdout") == 0;
    if (to_stdout) {
        out = stdout;
        outname = "standard output";
    } else {
        if (access(outname, F_OK) == 0 && access(outname, W_OK) != 0)
            die("%s: not writable", outname);
        int fd = open(outname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) {
            fprintf(stderr, "uudecode: %s: %s\n", outname, strerror(errno));
            return 1;
        }
        fchmod(fd, mode);       /* exactly; not being able to is no error */
        if (!(out = fdopen(fd, "wb")))
            die("%s: cannot open", outname);
    }
    bool ended = false;
    if (!b64) {
        while ((n = getline(&line, &cap, in)) >= 0) {
            line[strcspn(line, "\r\n")] = '\0';
            if (!line[0])
                continue;
            int len = (line[0] - ' ') & 077;
            if (len == 0) {
                /* the line of length 0; then "end" */
                while ((n = getline(&line, &cap, in)) >= 0) {
                    line[strcspn(line, "\r\n")] = '\0';
                    if (strcmp(line, "end") == 0) {
                        ended = true;
                        break;
                    }
                    if (line[0])
                        break;
                }
                break;
            }
            size_t have = strlen(line + 1);
            size_t need = ((size_t)len + 2) / 3 * 4;
            if (have < need) {
                /* trailing blanks may have been lost: they are zeros */
                if (have + 2 < need)
                    die("%s: a short line", inname);
            }
            unsigned char bytes[64];
            size_t k = 0;
            for (size_t i = 0; i < need; i += 4) {
                int v[4];
                for (int j = 0; j < 4; j++) {
                    char ch = i + (size_t)j < have ? line[1 + i + (size_t)j] : ' ';
                    if (ch < ' ' || ch > '`')
                        die("%s: a character that is not of the encoding", inname);
                    v[j] = (ch - ' ') & 077;
                }
                bytes[k++] = (unsigned char)(v[0] << 2 | v[1] >> 4);
                bytes[k++] = (unsigned char)(v[1] << 4 | v[2] >> 2);
                bytes[k++] = (unsigned char)(v[2] << 6 | v[3]);
            }
            put(bytes, (size_t)len);
        }
        if (!ended)
            die("%s: no end line", inname);
    } else {
        static signed char val[256];
        static const char alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        memset(val, -1, sizeof val);
        for (int i = 0; i < 64; i++)
            val[(unsigned char)alpha[i]] = (signed char)i;
        unsigned acc = 0;
        int nbits = 0, pads = 0;
        while ((n = getline(&line, &cap, in)) >= 0) {
            line[strcspn(line, "\r\n")] = '\0';
            if (strcmp(line, "====") == 0) {
                ended = true;
                break;
            }
            for (char *p = line; *p; p++) {
                if (*p == '=') {
                    pads++;
                    continue;
                }
                int v = val[(unsigned char)*p];
                if (v < 0)
                    continue;
                if (pads)
                    die("%s: data after the padding", inname);
                acc = acc << 6 | (unsigned)v;
                nbits += 6;
                if (nbits >= 8) {
                    nbits -= 8;
                    unsigned char b = (unsigned char)(acc >> nbits);
                    put(&b, 1);
                    acc &= (1u << nbits) - 1;
                }
            }
        }
        if (!ended)
            die("%s: no ==== line", inname);
        if (nbits >= 6)
            die("%s: an incomplete quantum", inname);
    }
    if (fflush(out) != 0 || ferror(out))
        die("%s: write error", outname);
    if (!to_stdout && fclose(out) != 0)
        die("%s: write error", outname);
    return 0;
}
