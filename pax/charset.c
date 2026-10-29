/*
 * charset.c - the character sets of pax extended header records.
 *
 * The path, linkpath, uname and gname records hold UTF-8 unless a
 * hdrcharset record says otherwise ("BINARY": bytes as they are).  When
 * reading they are translated to the codeset of the current locale, and
 * when writing from it, with iconv().  A value that cannot be
 * translated, holds a NUL, or (a pathname) is too long for the file
 * system is invalid, and -o invalid= says what to do (see
 * decode_entry_names and invalid_action).
 */
#include "pax.h"
#include <iconv.h>

/* the codeset of the current locale */
static const char *locale_codeset(void)
{
    const char *cs = nl_langinfo(CODESET);
    return cs && *cs ? cs : "ANSI_X3.4-1968";
}

/* the hdrcharset values that mean UTF-8 */
static int is_utf8_name(const char *cs)
{
    return strcmp(cs, "ISO-IR 10646 2000 UTF-8") == 0 || strcmp(cs, "UTF-8") == 0;
}

/*
 * The name iconv knows for a character set named in a header: the
 * standard's names of ISO 646 and the parts of ISO 8859 (the table of
 * the charset keyword), or the name as it is.
 */
static const char *iconv_name(const char *cs, char *buf, size_t n)
{
    int part, year;
    char end;
    if (strcmp(cs, "ISO-IR 646 1990") == 0)
        return "ASCII";
    if (sscanf(cs, "ISO-IR 8859 %d %d%c", &part, &year, &end) == 2 && part >= 1 && part <= 16) {
        snprintf(buf, n, "ISO-8859-%d", part);
        return buf;
    }
    return cs;
}

/*
 * Translate n bytes from one character set to another; the result is
 * NUL terminated.  0 if a character cannot be translated (or the input
 * holds a NUL, or iconv knows neither set).
 */
static int translate(const char *to, const char *from, const char *in, size_t n, char **out)
{
    iconv_t cd;
    size_t cap = n * 4 + 16, left_in = n, left_out;
    char *buf, *ip = (char *)in, *op;
    if (memchr(in, 0, n))
        return 0;
    cd = iconv_open(to, from);
    if (cd == (iconv_t)-1)
        return 0;
    buf = xmalloc(cap + 1);
    op = buf;
    left_out = cap;
    while (left_in > 0) {
        if (iconv(cd, &ip, &left_in, &op, &left_out) == (size_t)-1) {
            if (errno == E2BIG) {
                size_t used = (size_t)(op - buf);
                cap *= 2;
                buf = xrealloc(buf, cap + 1);
                op = buf + used;
                left_out = cap - used;
                continue;
            }
            iconv_close(cd);
            free(buf);
            return 0;
        }
    }
    /* the end of a stateful encoding */
    if (iconv(cd, NULL, NULL, &op, &left_out) == (size_t)-1) {
        iconv_close(cd);
        free(buf);
        return 0;
    }
    iconv_close(cd);
    *op = 0;
    if (memchr(buf, 0, (size_t)(op - buf))) {
        free(buf);
        return 0;
    }
    *out = buf;
    return 1;
}

/* a value of the current locale in UTF-8, for a record being written */
int to_utf8(const char *s, char **out)
{
    return translate("UTF-8", locale_codeset(), s, strlen(s), out);
}

/* a pathname that the file system cannot take */
static int too_long(const char *p)
{
    static long name_max;
    size_t comp = 0;
    if (!name_max) {
        name_max = pathconf(".", _PC_NAME_MAX);
        if (name_max <= 0)
            name_max = NAME_MAX;
    }
    if (strlen(p) >= PATH_MAX)
        return 1;
    for (; *p; p++) {
        if (*p == '/')
            comp = 0;
        else if (++comp > (size_t)name_max)
            return 1;
    }
    return 0;
}

/*
 * A value from an extended header record, for the entry: translated
 * from the record's character set, or the bytes as they are where that
 * is not possible (and *bad is set).
 */
static char *decode(const Kv *k, const char *charset, int *bad)
{
    char *t, buf[32];
    size_t n = k->vlen;
    if (charset && strcmp(charset, "BINARY") == 0)
        return xstrndup(k->val, strnlen(k->val, n));
    if (translate(locale_codeset(),
                  charset && !is_utf8_name(charset) ? iconv_name(charset, buf, sizeof buf) : "UTF-8",
                  k->val, n, &t))
        return t;
    *bad = 1;
    return xstrndup(k->val, strnlen(k->val, n));
}

/*
 * The names of an entry read with extended header records (the merged
 * records m): path and linkpath are translated, and the entry is marked
 * invalid when one of them cannot be, or does not suit the file system.
 * uname and gname are translated where possible and otherwise kept as
 * they are (the implementation-defined case of the standard).
 */
void decode_entry_names(Entry *e, Kv *m)
{
    Kv *cs = kv_get(m, "hdrcharset");
    const char *charset = cs && cs->vlen ? cs->val : NULL;
    Kv *k;
    int bad;
    if ((k = kv_get(m, "path")) && k->vlen) {
        bad = 0;
        e->raw_path = xstrndup(k->val, strnlen(k->val, k->vlen));
        free(e->path);
        e->path = decode(k, charset, &bad);
        if (bad)
            e->invalid = 1;
        else if (too_long(e->path))
            e->invalid = 2;
    }
    if ((k = kv_get(m, "linkpath")) && k->vlen && (e->typeflag == '1' || e->typeflag == '2')) {
        bad = 0;
        e->raw_link = xstrndup(k->val, strnlen(k->val, k->vlen));
        free(e->link);
        e->link = decode(k, charset, &bad);
        if (bad)
            e->invalid = 1;
        else if (!e->invalid && too_long(e->link))
            e->invalid = 2;
    }
    if ((k = kv_get(m, "uname")) && k->vlen) {
        bad = 0;
        free(e->uname);
        e->uname = decode(k, charset, &bad);
    }
    if ((k = kv_get(m, "gname")) && k->vlen) {
        bad = 0;
        free(e->gname);
        e->gname = decode(k, charset, &bad);
    }
}

/*
 * Read mode: what to do with an entry whose name or link name is
 * invalid (-o invalid=).  Returns 0 if the entry is to be skipped.
 * bypass, the default, skips the file (with a diagnostic, and the exit
 * status is 1); rename asks for a new name, as -i does; binary uses the
 * records' bytes without translation; UTF-8 and write keep the names
 * decode_entry_names made: translated where that is possible, the
 * records' bytes (UTF-8) where it is not.
 */
int invalid_action(Entry *e)
{
    const char *a = opt_invalid ? opt_invalid : "bypass";
    if (!e->invalid)
        return 1;
    if (strcmp(a, "rename") == 0)
        return 1;           /* asked where -i asks: invalid_rename() */
    if (strcmp(a, "bypass") == 0) {
        if (e->invalid == 1)
            warnx_("%s: name cannot be translated to the codeset of the current locale; "
                   "skipped (see -o invalid=)", e->path);
        else
            warnx_("%s: name too long for the file system; skipped (see -o invalid=)", e->path);
        return 0;
    }
    if (strcmp(a, "binary") == 0) {
        if (e->raw_path) {
            free(e->path);
            e->path = xstrdup(e->raw_path);
        }
        if (e->raw_link && e->link) {
            free(e->link);
            e->link = xstrdup(e->raw_link);
        }
    }
    return 1;
}

/* -o invalid=rename: the entry is renamed as with -i */
int invalid_rename(const Entry *e)
{
    return e->invalid && opt_invalid && strcmp(opt_invalid, "rename") == 0;
}
