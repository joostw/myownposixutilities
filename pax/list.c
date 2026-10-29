/*
 * list.c - the table of contents: names, the ls -l style verbose listing
 * and -o listopt=format.
 */
#include "pax.h"

char *opt_listopt;

void ls_mode_string(Entry *e, char *b)
{
    static const char types[] = { '-', 'd', 'l', '-', 'c', 'b', 'p', 's', '-', '-' };
    mode_t m = e->perm;
    b[0] = types[e->type];
    b[1] = m & 0400 ? 'r' : '-';
    b[2] = m & 0200 ? 'w' : '-';
    b[3] = (m & 04000) ? ((m & 0100) ? 's' : 'S') : (m & 0100 ? 'x' : '-');
    b[4] = m & 040 ? 'r' : '-';
    b[5] = m & 020 ? 'w' : '-';
    b[6] = (m & 02000) ? ((m & 010) ? 's' : 'S') : (m & 010 ? 'x' : '-');
    b[7] = m & 04 ? 'r' : '-';
    b[8] = m & 02 ? 'w' : '-';
    b[9] = (m & 01000) ? ((m & 01) ? 't' : 'T') : (m & 01 ? 'x' : '-');
    b[10] = 0;
}

static void ls_date(time_t t, char *out, size_t n)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&t);
    const char *fmt = (t > now - 15724800 && t <= now + 3600) ? "%b %e %H:%M" : "%b %e  %Y";
    if (!tm || !strftime(out, n, fmt, tm))
        snprintf(out, n, "%lld", (long long)t);
}

static void list_verbose(Entry *e)
{
    char mode[12], date[64], size[64], owner[64], group[64];
    ls_mode_string(e, mode);
    if (e->uname)
        snprintf(owner, sizeof owner, "%s", e->uname);
    else
        snprintf(owner, sizeof owner, "%lu", (unsigned long)e->uid);
    if (e->gname)
        snprintf(group, sizeof group, "%s", e->gname);
    else
        snprintf(group, sizeof group, "%lu", (unsigned long)e->gid);
    if (e->type == T_CHR || e->type == T_BLK)
        snprintf(size, sizeof size, "%lu, %lu", e->devmajor, e->devminor);
    else
        snprintf(size, sizeof size, "%jd", (intmax_t)(e->type == T_HARD ? e->size : e->size));
    ls_date(e->mtime.tv_sec, date, sizeof date);
    printf("%s %3lu %-8s %-8s %8s %s %s", mode, e->nlink ? e->nlink : 1, owner, group, size, date,
           e->path);
    if (e->type == T_LNK)
        printf(" -> %s", e->link ? e->link : "");
    else if (e->type == T_HARD)
        printf(" == %s", e->link ? e->link : "");
    putchar('\n');
}

/* ---------- -o listopt ---------- */

static int kv_base;   /* base of the last keyword_value: 8 for raw header fields */

static char *tar_field(Entry *e, const char *key)
{
    static const struct {
        const char *name;
        int off, len, is_str;
    } f[] = {
        { "name", 0, 100, 1 },      { "mode", 100, 8, 0 },     { "uid", 108, 8, 0 },
        { "gid", 116, 8, 0 },       { "size", 124, 12, 0 },    { "mtime", 136, 12, 0 },
        { "chksum", 148, 8, 0 },    { "typeflag", 156, 1, 1 }, { "linkname", 157, 100, 1 },
        { "magic", 257, 6, 1 },     { "version", 263, 2, 1 },  { "uname", 265, 32, 1 },
        { "gname", 297, 32, 1 },    { "devmajor", 329, 8, 0 }, { "devminor", 337, 8, 0 },
        { "prefix", 345, 155, 1 },
    };
    if (e->hdrlen != 512)
        return NULL;
    for (size_t i = 0; i < sizeof f / sizeof f[0]; i++) {
        if (strcmp(f[i].name, key) == 0) {
            const char *p = (const char *)e->hdr + f[i].off;
            size_t l = strnlen(p, (size_t)f[i].len);
            char *s = xstrndup(p, l);
            if (!f[i].is_str) {
                /* trailing blanks of numeric fields */
                size_t k = strlen(s);
                while (k && s[k - 1] == ' ')
                    s[--k] = 0;
                kv_base = 8;
            }
            return s;
        }
    }
    return NULL;
}

static char *cpio_field(Entry *e, const char *key)
{
    static const char *odc[] = { "c_magic", "c_dev", "c_ino", "c_mode", "c_uid", "c_gid", "c_nlink",
                                 "c_rdev", "c_mtime", "c_namesize", "c_filesize" };
    static const int w[] = { 6, 6, 6, 6, 6, 6, 6, 6, 11, 6, 11 };
    int off = 0;
    if (e->hdrlen != 76)
        return NULL;
    if (strcmp(key, "c_name") == 0 || strcmp(key, "name") == 0)
        return xstrdup(e->path);
    for (int i = 0; i < 11; i++) {
        if (strcmp(odc[i], key) == 0 || strcmp(odc[i] + 2, key) == 0) {
            kv_base = 8;
            return xstrndup((const char *)e->hdr + off, (size_t)w[i]);
        }
        off += w[i];
    }
    return NULL;
}

/* the value of a keyword for listopt */
static char *keyword_value(Entry *e, const char *key)
{
    Kv *k = kv_get(e->xrec, key);
    char *v;
    kv_base = 10;
    if (k && k->vlen)
        return xstrndup(k->val, k->vlen);
    if (strcmp(key, "path") == 0)
        return xstrdup(e->path);
    if (strcmp(key, "linkpath") == 0)
        return e->link ? xstrdup(e->link) : NULL;
    if ((v = tar_field(e, key)) || (v = cpio_field(e, key)))
        return v;
    if (strcmp(key, "uname") == 0 && e->uname)
        return xstrdup(e->uname);
    if (strcmp(key, "gname") == 0 && e->gname)
        return xstrdup(e->gname);
    {
        char b[64];
        if (strcmp(key, "uid") == 0)
            snprintf(b, sizeof b, "%lu", (unsigned long)e->uid);
        else if (strcmp(key, "gid") == 0)
            snprintf(b, sizeof b, "%lu", (unsigned long)e->gid);
        else if (strcmp(key, "size") == 0)
            snprintf(b, sizeof b, "%jd", (intmax_t)e->size);
        else if (strcmp(key, "mtime") == 0)
            snprintf(b, sizeof b, "%lld", (long long)e->mtime.tv_sec);
        else if (strcmp(key, "atime") == 0 && e->has_atime)
            snprintf(b, sizeof b, "%lld", (long long)e->atime.tv_sec);
        else if (strcmp(key, "mode") == 0) {
            snprintf(b, sizeof b, "0%o", (unsigned)e->perm);
            kv_base = 8;
        }
        else
            return NULL;
        return xstrdup(b);
    }
}

/* the printf numeric interpretation of a string */
static intmax_t num_value(const char *s, int *ok)
{
    char *end;
    intmax_t v;
    while (*s == ' ')
        s++;
    if (!*s) {
        *ok = 1;
        return 0;
    }
    errno = 0;
    v = strtoimax(s, &end, kv_base ? kv_base : 10);
    *ok = end != s;
    return v;
}

static void add_escape(Buf *b, const char **pp)
{
    const char *p = *pp;   /* at the backslash */
    p++;
    switch (*p) {
    case 'a': buf_addc(b, '\a'); break;
    case 'b': buf_addc(b, '\b'); break;
    case 'f': buf_addc(b, '\f'); break;
    case 'n': buf_addc(b, '\n'); break;
    case 'r': buf_addc(b, '\r'); break;
    case 't': buf_addc(b, '\t'); break;
    case 'v': buf_addc(b, '\v'); break;
    case '\\': buf_addc(b, '\\'); break;
    case 0:
        buf_addc(b, '\\');
        *pp = p;
        return;
    default:
        if (*p >= '0' && *p <= '7') {
            int v = 0, n = 0;
            while (n < 3 && *p >= '0' && *p <= '7') {
                v = v * 8 + (*p - '0');
                p++;
                n++;
            }
            buf_addc(b, v);
            *pp = p;
            return;
        }
        buf_addc(b, '\\');
        buf_addc(b, *p);
    }
    *pp = p + 1;
}

static void listopt_entry(Entry *e)
{
    Buf out = { 0 };
    const char *p = opt_listopt;
    while (*p) {
        char spec[64], key[256], sub[256], conv;
        size_t sl = 0;
        int haskey = 0;
        if (*p == '\\') {
            add_escape(&out, &p);
            continue;
        }
        if (*p != '%') {
            buf_addc(&out, *p++);
            continue;
        }
        if (p[1] == '%') {
            buf_addc(&out, '%');
            p += 2;
            continue;
        }
        p++;
        spec[sl++] = '%';
        /* flags, width, precision; (keyword) may come before the conversion */
        key[0] = sub[0] = 0;
        while (*p && strchr("-+ #0123456789.", *p) && sl < sizeof spec - 4)
            spec[sl++] = *p++;
        if (*p == '(') {
            const char *q = strchr(p, ')');
            size_t kl;
            if (!q) {
                warnx_("listopt: missing )");
                break;
            }
            kl = (size_t)(q - p - 1);
            if (kl >= sizeof key)
                kl = sizeof key - 1;
            memcpy(key, p + 1, kl);
            key[kl] = 0;
            haskey = 1;
            p = q + 1;
        }
        conv = *p ? *p++ : 0;
        spec[sl] = 0;
        if (conv == 'T') {
            /* time: (keyword=subformat) */
            char *eq = strchr(key, '=');
            const char *kw = "mtime", *fmt = "%b %e %H:%M %Y";
            char tb[512], *v;
            struct timespec ts = { 0, 0 };
            if (haskey) {
                if (eq) {
                    *eq = 0;
                    fmt = eq + 1;
                }
                if (key[0])
                    kw = key;
            }
            v = NULL;
            if (strcmp(kw, "mtime") == 0)
                ts = e->mtime;
            else if (strcmp(kw, "atime") == 0 && e->has_atime)
                ts = e->atime;
            else if ((v = keyword_value(e, kw)) != NULL && *v)
                ts.tv_sec = (time_t)strtoll(v, NULL, 0);
            else {
                free(v);
                v = NULL;
                fmt = "";
            }
            free(v);
            {
                struct tm *tm = localtime(&ts.tv_sec);
                if (!*fmt || !tm || !strftime(tb, sizeof tb, fmt, tm))
                    tb[0] = 0;
            }
            spec[sl] = 's';
            spec[sl + 1] = 0;
            buf_fmt(&out, spec, tb);
            continue;
        }
        if (conv == 'M') {
            char m[12];
            ls_mode_string(e, m);
            spec[sl] = 's';
            spec[sl + 1] = 0;
            buf_fmt(&out, spec, m);
            continue;
        }
        if (conv == 'D') {
            char d[64];
            if (e->type == T_CHR || e->type == T_BLK)
                snprintf(d, sizeof d, "%lu,%lu", e->devmajor, e->devminor);
            else if (haskey) {
                char *v = keyword_value(e, key);
                int ok;
                snprintf(d, sizeof d, "%ju", (uintmax_t)(v ? num_value(v, &ok) : 0));
                free(v);
            } else
                snprintf(d, sizeof d, " ");
            spec[sl] = 's';
            spec[sl + 1] = 0;
            buf_fmt(&out, spec, d);
            continue;
        }
        if (conv == 'F' || conv == 'L') {
            Buf pb = { 0 };
            if (haskey) {
                char *s = key, *t;
                while (s) {
                    char *v;
                    t = strchr(s, ',');
                    if (t)
                        *t = 0;
                    v = keyword_value(e, s);
                    if (v && *v) {
                        if (pb.len)
                            buf_addc(&pb, '/');
                        buf_adds(&pb, v);
                    }
                    free(v);
                    s = t ? t + 1 : NULL;
                }
            } else
                buf_adds(&pb, e->path);
            if (conv == 'L' && e->type == T_LNK) {
                buf_adds(&pb, " -> ");
                buf_adds(&pb, e->link ? e->link : "");
            }
            spec[sl] = 's';
            spec[sl + 1] = 0;
            buf_fmt(&out, spec, pb.s ? pb.s : "");
            buf_free(&pb);
            continue;
        }
        {
            char *v = haskey ? keyword_value(e, key) : NULL;
            const char *sv = v ? v : "";
            spec[sl] = conv;
            spec[sl + 1] = 0;
            switch (conv) {
            case 's':
                buf_fmt(&out, spec, sv);
                break;
            case 'c':
                buf_fmt(&out, spec, sv[0]);
                break;
            case 'd':
            case 'i':
            case 'o':
            case 'u':
            case 'x':
            case 'X': {
                int ok;
                intmax_t n = num_value(sv, &ok);
                char s2[70];
                snprintf(s2, sizeof s2, "%.*sj%c", (int)sl, spec, conv);
                buf_fmt(&out, s2, n);
                break;
            }
            default:
                buf_add(&out, spec, strlen(spec));
            }
            free(v);
        }
        (void)sub;
    }
    buf_addc(&out, '\n');
    fwrite(out.s, 1, out.len, stdout);
    buf_free(&out);
}

void list_entry(Entry *e)
{
    if (opt_listopt)
        listopt_entry(e);
    else if (opt_v)
        list_verbose(e);
    else
        printf("%s\n", e->path);
    fflush(stdout);
}
