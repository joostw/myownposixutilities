#define _POSIX_C_SOURCE 200809L

#include "posix_re.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strncasecmp (XSI) */
#include <wchar.h>     /* wchar_t, mbstowcs */
#include <wctype.h>    /* towlower */

/* ------------------------------------------------------------------ */
/*  Internal types                                                      */
/* ------------------------------------------------------------------ */

struct pat_node {
    bool            re_valid;  /* true → use .re; false → PAT_FIXED */
    regex_t         re;
    char           *str;       /* always set */
    size_t          str_len;
    /*
     * For PAT_FIXED + icase: the pattern pre-converted to wide characters
     * and each character lowercased.  Used by the multibyte-correct
     * case-insensitive search path.  NULL when not applicable or when
     * mbstowcs fails (invalid encoding in the pattern itself).
     */
    wchar_t        *wstr;
    size_t          wstr_len;
    struct pat_node *next;
};

struct pset {
    struct pat_node *head;
    struct pat_node *tail;
    pat_mode_t       mode;
    bool             icase;
};

/* ------------------------------------------------------------------ */
/*  Construction                                                        */
/* ------------------------------------------------------------------ */

pset_t *pset_new(pat_mode_t mode, bool icase)
{
    pset_t *ps = calloc(1, sizeof *ps);
    if (ps) {
        ps->mode  = mode;
        ps->icase = icase;
    }
    return ps;
}

static int add_one(pset_t *ps, const char *pat, size_t plen, const char *src)
{
    struct pat_node *n = calloc(1, sizeof *n);
    if (!n) goto oom;

    n->str = malloc(plen + 1);
    if (!n->str) { free(n); goto oom; }
    memcpy(n->str, pat, plen);
    n->str[plen] = '\0';
    n->str_len   = plen;

    if (ps->mode != PAT_FIXED) {
        int cflags = (ps->mode == PAT_ERE ? REG_EXTENDED : 0)
                   | (ps->icase           ? REG_ICASE    : 0);
        int rc = regcomp(&n->re, n->str, cflags);
        if (rc != 0) {
            char buf[256];
            regerror(rc, &n->re, buf, sizeof buf);
            fprintf(stderr, "grep: %s: %s\n", src, buf);
            free(n->str); free(n);
            return -1;
        }
        n->re_valid = true;
    } else if (ps->icase && plen > 0) {
        /*
         * Pre-convert the fixed pattern to lowercase wide characters.
         * This is done once at compile time so that each line match only
         * needs to convert the haystack (not the needle again).
         *
         * Allocation: plen wide chars suffice because each multibyte
         * character occupies at least one byte.
         */
        wchar_t *w = malloc((plen + 1) * sizeof *w);
        if (w) {
            size_t wlen = mbstowcs(w, n->str, plen + 1);
            if (wlen != (size_t)-1) {
                for (size_t i = 0; i < wlen; i++) w[i] = towlower(w[i]);
                n->wstr     = w;
                n->wstr_len = wlen;
            } else {
                free(w);   /* invalid encoding: fall back to strncasecmp */
            }
        }
    }

    if (ps->tail) ps->tail->next = n; else ps->head = n;
    ps->tail = n;
    return 0;

oom:
    perror("grep");
    return -1;
}

/*
 * Split str on '\n' and add each segment as a pattern.
 *
 * Patterns in a pattern_list are *separated* by newlines (spec: "patterns
 * separated by <newline> characters").  A trailing '\n' is therefore a
 * separator with nothing following it — it does not create an extra null
 * pattern.  Two adjacent '\n' characters do create a null pattern (the
 * empty segment between them), consistent with the spec note "A null
 * pattern can be specified by two adjacent <newline> characters."
 */
int pset_add_str(pset_t *ps, const char *str, const char *src)
{
    const char *p = str;
    for (;;) {
        const char *nl  = strchr(p, '\n');
        size_t      len = nl ? (size_t)(nl - p) : strlen(p);
        /* Skip a final empty segment produced by a trailing '\n'. */
        if (!nl && len == 0 && p > str) break;
        if (add_one(ps, p, len, src) != 0) return -1;
        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}

int pset_add_file(pset_t *ps, const char *path)
{
    FILE   *fp   = fopen(path, "r");
    char   *line = NULL;
    size_t  cap  = 0;
    ssize_t n;
    int     rc   = 0;

    if (!fp) {
        fprintf(stderr, "grep: %s: %s\n", path, strerror(errno));
        return -1;
    }
    while ((n = getline(&line, &cap, fp)) >= 0) {
        size_t len = (size_t)n;
        if (len > 0 && line[len - 1] == '\n') len--;   /* strip terminator */
        if (add_one(ps, line, len, path) != 0) { rc = -1; break; }
    }
    if (ferror(fp)) {
        fprintf(stderr, "grep: %s: %s\n", path, strerror(errno));
        rc = -1;
    }
    free(line);
    fclose(fp);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Matching                                                            */
/* ------------------------------------------------------------------ */

static bool match_re(const regex_t *re, const char *line, size_t len,
                     bool whole)
{
    regmatch_t m;
    if (regexec(re, line, 1, &m, 0) != 0) return false;
    return !whole || (m.rm_so == 0 && (size_t)m.rm_eo == len);
}

/*
 * Wide-character case-insensitive substring search.
 *
 * needle is already lowercase-folded (done once at pattern-compile time).
 * The haystack is converted from multibyte on each call.
 *
 * Returns false on OOM or invalid encoding (caller may fall back).
 */
static bool wcs_find(const char *hay, size_t hbytes,
                     const wchar_t *needle, size_t nwc, bool whole)
{
    wchar_t *whay = malloc((hbytes + 1) * sizeof *whay);
    if (!whay) return false;

    size_t whlen = mbstowcs(whay, hay, hbytes + 1);
    if (whlen == (size_t)-1) { free(whay); return false; }

    bool found = false;
    if (whole) {
        if (whlen == nwc) {
            found = true;
            for (size_t i = 0; i < nwc; i++)
                if ((wchar_t)towlower(whay[i]) != needle[i]) { found = false; break; }
        }
    } else {
        for (size_t i = 0; !found && i + nwc <= whlen; i++) {
            size_t j;
            for (j = 0; j < nwc; j++)
                if ((wchar_t)towlower(whay[i + j]) != needle[j]) break;
            if (j == nwc) found = true;
        }
    }

    free(whay);
    return found;
}

/*
 * Fixed-string match.
 *
 * For case-sensitive matching: strstr / memcmp (fast, byte-exact).
 * For case-insensitive matching with a pre-built wide pattern: wcs_find
 *   (multibyte-correct, uses towlower).
 * Fallback when no wide pattern (OOM or bad encoding at compile time):
 *   strncasecmp loop (byte-based; correct for ASCII, approximate for
 *   multibyte — same limitation as most historical implementations).
 */
static bool match_fixed(const char *line, size_t llen,
                        const struct pat_node *n, bool icase, bool whole)
{
    const char   *pat  = n->str;
    size_t        plen = n->str_len;

    if (!icase) {
        if (whole) return llen == plen && memcmp(line, pat, plen) == 0;
        if (plen == 0) return true;
        return plen <= llen && strstr(line, pat) != NULL;
    }

    /* Case-insensitive: prefer the wide-character path. */
    if (n->wstr != NULL)
        return wcs_find(line, llen, n->wstr, n->wstr_len, whole);

    /* Fallback (pattern had invalid encoding or OOM). */
    if (whole) return llen == plen && strncasecmp(line, pat, plen) == 0;
    if (plen == 0) return true;
    if (plen > llen) return false;
    for (size_t i = 0; i + plen <= llen; i++)
        if (strncasecmp(line + i, pat, plen) == 0) return true;
    return false;
}

bool pset_match(const pset_t *ps, const char *line, size_t len, bool whole)
{
    for (const struct pat_node *n = ps->head; n; n = n->next) {
        if (n->re_valid) {
            if (match_re(&n->re, line, len, whole)) return true;
        } else {
            if (match_fixed(line, len, n, ps->icase, whole)) return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/*  Cleanup                                                             */
/* ------------------------------------------------------------------ */

void pset_free(pset_t *ps)
{
    if (!ps) return;
    struct pat_node *n = ps->head;
    while (n) {
        struct pat_node *next = n->next;
        if (n->re_valid) regfree(&n->re);
        free(n->wstr);
        free(n->str);
        free(n);
        n = next;
    }
    free(ps);
}
