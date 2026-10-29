/*
 * tar.c - the ustar interchange format and the pax interchange format
 * (ustar with typeflag x and g extended headers).  Old (pre-POSIX) tar
 * archives and GNU long-name records are accepted on input.
 */
#include "pax.h"

Kv *opt_global, *opt_local;
char **opt_delete;
int nopt_delete;
char *opt_exthdr_name, *opt_globexthdr_name;
int opt_linkdata, opt_times;
char *opt_invalid;

static Kv *globals;        /* typeflag g records read so far */
static int global_seq;

int keyword_deleted(const char *key)
{
    for (int i = 0; i < nopt_delete; i++)
        if (fnmatch(opt_delete[i], key, 0) == 0)
            return 1;
    return 0;
}

/* ---------- numbers in header fields ---------- */

static int parse_octal(const unsigned char *p, int n, uintmax_t *out)
{
    uintmax_t v = 0;
    int i = 0, any = 0;
    if (n > 0 && (p[0] & 0x80)) {
        /* base-256 (an extension used by other implementations) */
        v = p[0] & 0x3f;
        for (i = 1; i < n; i++)
            v = (v << 8) | p[i];
        *out = v;
        return 1;
    }
    while (i < n && (p[i] == ' ' || p[i] == 0))
        if (p[i++] == 0 && !any) {
            /* an all-NUL field is zero */
        }
    for (i = 0; i < n && p[i] == ' '; i++)
        ;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) {
        v = v * 8 + (uintmax_t)(p[i] - '0');
        any = 1;
    }
    *out = v;
    return 1;
}

static void put_octal(unsigned char *p, int n, uintmax_t v)
{
    /* n-1 digits and a NUL */
    for (int i = n - 2; i >= 0; i--) {
        p[i] = (unsigned char)('0' + (v & 7));
        v >>= 3;
    }
    p[n - 1] = 0;
}

static int fits_octal(int n, uintmax_t v)
{
    int digits = n - 1;
    return digits >= 22 || v < ((uintmax_t)1 << (3 * digits));
}

static char *field_str(const unsigned char *p, int n)
{
    int l = 0;
    while (l < n && p[l])
        l++;
    return xstrndup((const char *)p, (size_t)l);
}

/* ---------- extended header records ---------- */

static void parse_records(const char *d, size_t n, Kv **into)
{
    size_t off = 0;
    while (off < n) {
        size_t len = 0, i = off;
        const char *key, *eq;
        while (i < n && d[i] >= '0' && d[i] <= '9')
            len = len * 10 + (size_t)(d[i++] - '0');
        if (i >= n || d[i] != ' ' || len == 0 || off + len > n || d[off + len - 1] != '\n') {
            warnx_("invalid extended header record");
            return;
        }
        key = d + i + 1;
        eq = memchr(key, '=', (size_t)(d + off + len - key));
        if (!eq) {
            warnx_("invalid extended header record");
            return;
        }
        {
            char *k = xstrndup(key, (size_t)(eq - key));
            size_t vlen = (size_t)(d + off + len - 1 - (eq + 1));
            if (!keyword_deleted(k))
                kv_set(into, k, eq + 1, vlen);
            free(k);
        }
        off += len;
    }
}

static int parse_time(const char *s, struct timespec *ts)
{
    char *end;
    long long sec;
    long nsec = 0;
    int neg = s[0] == '-';
    errno = 0;
    sec = strtoll(s, &end, 10);
    if (end == s || errno)
        return 0;
    if (*end == '.') {
        int digits = 0;
        end++;
        while (isdigit((unsigned char)*end)) {
            if (digits < 9) {
                nsec = nsec * 10 + (*end - '0');
                digits++;
            }
            end++;
        }
        while (digits++ < 9)
            nsec *= 10;
        if (neg && nsec) {
            /* truncate toward the earlier time */
            sec -= 1;
            nsec = 1000000000L - nsec;
        }
    }
    if (*end)
        return 0;
    ts->tv_sec = (time_t)sec;
    ts->tv_nsec = nsec;
    return 1;
}

static int parse_dec(const char *s, uintmax_t *v)
{
    char *end;
    errno = 0;
    *v = strtoumax(s, &end, 10);
    return end != s && !*end && !errno;
}

/* apply keyword records (already merged) to an entry */
static void apply_records(Entry *e, Kv *recs)
{
    for (Kv *k = recs; k; k = k->next) {
        uintmax_t v;
        int empty = k->vlen == 0;
        /* path, linkpath, uname and gname with values: decode_entry_names */
        if (strcmp(k->key, "uname") == 0) {
            if (empty) {
                free(e->uname);
                e->uname = NULL;
            }
        } else if (strcmp(k->key, "gname") == 0) {
            if (empty) {
                free(e->gname);
                e->gname = NULL;
            }
        } else if (strcmp(k->key, "uid") == 0) {
            if (!empty && parse_dec(k->val, &v))
                e->uid = (uid_t)v;
        } else if (strcmp(k->key, "gid") == 0) {
            if (!empty && parse_dec(k->val, &v))
                e->gid = (gid_t)v;
        } else if (strcmp(k->key, "size") == 0) {
            if (!empty && parse_dec(k->val, &v))
                e->size = (off_t)v;
        } else if (strcmp(k->key, "mtime") == 0) {
            if (!empty)
                parse_time(k->val, &e->mtime);
        } else if (strcmp(k->key, "atime") == 0) {
            if (empty)
                e->has_atime = 0;
            else if (parse_time(k->val, &e->atime))
                e->has_atime = 1;
        }
    }
}

/* merge in precedence order: g, -o k=v, x, -o k:=v */
static Kv *merge_records(Kv *x)
{
    Kv *m = NULL;
    for (Kv *k = globals; k; k = k->next)
        kv_set(&m, k->key, k->val, k->vlen);
    for (Kv *k = opt_global; k; k = k->next)
        if (!keyword_deleted(k->key))
            kv_set(&m, k->key, k->val, k->vlen);
    for (Kv *k = x; k; k = k->next)
        kv_set(&m, k->key, k->val, k->vlen);
    for (Kv *k = opt_local; k; k = k->next)
        if (!keyword_deleted(k->key))
            kv_set(&m, k->key, k->val, k->vlen);
    return m;
}

/* ---------- reading ---------- */

static int zero_block(const unsigned char *h)
{
    for (int i = 0; i < 512; i++)
        if (h[i])
            return 0;
    return 1;
}

static int checksum_ok(const unsigned char *h)
{
    unsigned long sum = 0;
    long ssum = 0;
    uintmax_t stored;
    for (int i = 0; i < 512; i++) {
        int c = (i >= 148 && i < 156) ? ' ' : h[i];
        sum += (unsigned long)c;
        ssum += (i >= 148 && i < 156) ? ' ' : (signed char)h[i];
    }
    parse_octal(h + 148, 8, &stored);
    return stored == sum || (long)stored == ssum;
}

static char *read_data_string(off_t size)
{
    char *d;
    if (size < 0 || size > 64 * 1024 * 1024)
        errx_("extended header too large");
    d = xmalloc((size_t)size + 1);
    arc_read_full(d, (size_t)size);
    d[size] = 0;
    arc_skip((512 - size % 512) % 512);
    return d;
}

off_t tar_end_off;

/* "dir/" -> "dir", "./" -> "."; "/" stays */
static void strip_trailing_slashes(char *s)
{
    size_t l = strlen(s);
    while (l > 1 && s[l - 1] == '/')
        s[--l] = 0;
}

int tar_read_header(Entry *e)
{
    unsigned char h[512];
    Kv *x = NULL;
    char *longname = NULL, *longlink = NULL;
    for (;;) {
        off_t here = arc_offset();
        int r = arc_read(h, 512);
        uintmax_t v;
        tar_end_off = here;
        if (r == 0)
            break;
        if (r < 0)
            errx_("unexpected end of archive");
        if (zero_block(h)) {
            /* end of archive: the second zero block may follow */
            break;
        }
        if (!checksum_ok(h))
            errx_("invalid header checksum: the archive is damaged");
        if (h[156] == 'x' || h[156] == 'g' || h[156] == 'L' || h[156] == 'K' || h[156] == 'X') {
            char *d;
            parse_octal(h + 124, 12, &v);
            d = read_data_string((off_t)v);
            if (h[156] == 'x' || h[156] == 'X')
                parse_records(d, (size_t)v, &x);
            else if (h[156] == 'g') {
                parse_records(d, (size_t)v, &globals);
            } else if (h[156] == 'L') {
                free(longname);
                longname = xstrndup(d, strnlen(d, (size_t)v));
            } else {
                free(longlink);
                longlink = xstrndup(d, strnlen(d, (size_t)v));
            }
            free(d);
            continue;
        }
        entry_init(e);
        memcpy(e->hdr, h, 512);
        e->hdrlen = 512;
        e->typeflag = h[156];
        e->fmt = memcmp(h + 257, "ustar", 5) == 0 ? F_USTAR : F_OLDTAR;
        {
            char *name = field_str(h, 100);
            if (e->fmt == F_USTAR && h[345]) {
                char *prefix = field_str(h + 345, 155);
                Buf b = { 0 };
                buf_adds(&b, prefix);
                buf_addc(&b, '/');
                buf_adds(&b, name);
                free(prefix);
                free(name);
                name = b.s;
            }
            e->path = longname ? longname : name;
            if (longname)
                free(name);
            longname = NULL;
        }
        e->link = longlink ? longlink : field_str(h + 157, 100);
        longlink = NULL;
        parse_octal(h + 100, 8, &v);
        e->perm = (mode_t)(v & 07777);
        parse_octal(h + 108, 8, &v);
        e->uid = (uid_t)v;
        parse_octal(h + 116, 8, &v);
        e->gid = (gid_t)v;
        parse_octal(h + 124, 12, &v);
        e->size = (off_t)v;
        parse_octal(h + 136, 12, &v);
        e->mtime.tv_sec = (time_t)v;
        e->mtime.tv_nsec = 0;
        if (e->fmt == F_USTAR) {
            char *u = field_str(h + 265, 32), *g = field_str(h + 297, 32);
            e->uname = *u ? u : (free(u), NULL);
            e->gname = *g ? g : (free(g), NULL);
            parse_octal(h + 329, 8, &v);
            e->devmajor = (unsigned long)v;
            parse_octal(h + 337, 8, &v);
            e->devminor = (unsigned long)v;
        }
        {
            Kv *m = merge_records(x);
            apply_records(e, m);
            decode_entry_names(e, m);
            e->xrec = m;
            if (x)
                e->fmt = F_PAX;
        }
        kv_free(&x);
        switch (e->typeflag) {
        case '0':
        case 0:
            e->type = T_REG;
            break;
        case '7':
            e->type = T_CONTIG;
            break;
        case '1':
            e->type = T_HARD;
            break;
        case '2':
            e->type = T_LNK;
            e->size = 0;
            break;
        case '3':
            e->type = T_CHR;
            e->size = 0;
            break;
        case '4':
            e->type = T_BLK;
            e->size = 0;
            break;
        case '5':
            e->type = T_DIR;
            e->size = 0;
            break;
        case '6':
            e->type = T_FIFO;
            e->size = 0;
            break;
        default:
            e->type = T_OTHER;
            break;
        }
        if (e->type == T_REG && e->fmt == F_OLDTAR) {
            size_t l = strlen(e->path);
            if (l > 0 && e->path[l - 1] == '/') {
                e->type = T_DIR;
                e->size = 0;
            }
        }
        if (e->type != T_LNK && e->type != T_HARD) {
            free(e->link);
            e->link = NULL;
        }
        strip_trailing_slashes(e->path);
        if (e->type == T_HARD && e->link)
            strip_trailing_slashes(e->link);
        return 1;
    }
    free(longname);
    free(longlink);
    kv_free(&x);
    return 0;
}

/* ---------- writing ---------- */

static int portable_string(const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (!((*p >= 0x20 && *p <= 0x7e) || (*p >= 0x07 && *p <= 0x0d)))
            return 0;
    return 1;
}

static int alnum_string(const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')))
            return 0;
    return 1;
}

/* -o hdrcharset=BINARY or hdrcharset:=BINARY: the names are written as they are */
static int binary_forced(void)
{
    Kv *k = kv_get(opt_local, "hdrcharset");
    if (!k)
        k = kv_get(opt_global, "hdrcharset");
    return k && strcmp(k->val, "BINARY") == 0;
}

/* split a pathname into the prefix and name fields; 0 if impossible */
static int split_name(const char *path, char *prefix, char *name)
{
    size_t len = strlen(path);
    memset(prefix, 0, 156);
    memset(name, 0, 101);
    if (len <= 100) {
        memcpy(name, path, len);
        return 1;
    }
    if (len > 256)
        return 0;
    /* find a slash such that prefix <= 155 and name <= 100 */
    for (size_t i = len - 1; i > 0; i--) {
        if (path[i] != '/')
            continue;
        if (i <= 155 && len - i - 1 <= 100 && len - i - 1 > 0) {
            memcpy(prefix, path, i);
            memcpy(name, path + i + 1, len - i - 1);
            return 1;
        }
        if (i < len - 101)
            break;
    }
    return 0;
}

static void fill_checksum(unsigned char *h)
{
    unsigned long sum = 0;
    memset(h + 148, ' ', 8);
    for (int i = 0; i < 512; i++)
        sum += h[i];
    put_octal(h + 148, 7, sum);
    h[155] = ' ';
}

static void add_record(Buf *b, const char *key, const char *val, size_t vlen)
{
    size_t base = strlen(key) + vlen + 3, len = base, digits;
    char num[32];
    for (;;) {
        snprintf(num, sizeof num, "%zu", len);
        digits = strlen(num);
        if (base + digits == len)
            break;
        len = base + digits;
    }
    buf_fmt(b, "%zu %s=", len, key);
    buf_add(b, val, vlen);
    buf_addc(b, '\n');
}

static void time_string(struct timespec ts, char *out, size_t n)
{
    if (ts.tv_nsec) {
        char frac[16];
        int l;
        snprintf(frac, sizeof frac, "%09ld", ts.tv_nsec);
        l = 9;
        while (l > 1 && frac[l - 1] == '0')
            frac[--l] = 0;
        snprintf(out, n, "%lld.%s", (long long)ts.tv_sec, frac);
    } else
        snprintf(out, n, "%lld", (long long)ts.tv_sec);
}

static char *subst_hdr_name(const char *tmpl, const char *path, int seq)
{
    Buf b = { 0 };
    char *dirc = xstrdup(path), *basec = xstrdup(path);
    char *d, *f;
    size_t l = strlen(dirc);
    while (l > 1 && dirc[l - 1] == '/')
        dirc[--l] = 0;
    d = strrchr(dirc, '/');
    if (!d)
        d = ".";
    else if (d == dirc)
        d = "/";
    else {
        *d = 0;
        d = dirc;
    }
    l = strlen(basec);
    while (l > 1 && basec[l - 1] == '/')
        basec[--l] = 0;
    f = strrchr(basec, '/');
    f = f && f[1] ? f + 1 : basec;
    for (const char *p = tmpl; *p; p++) {
        if (*p == '%' && p[1]) {
            p++;
            switch (*p) {
            case 'd':
                buf_adds(&b, d);
                break;
            case 'f':
                buf_adds(&b, f);
                break;
            case 'p':
                buf_fmt(&b, "%ld", (long)getpid());
                break;
            case 'n':
                buf_fmt(&b, "%d", seq);
                break;
            case '%':
                buf_addc(&b, '%');
                break;
            default:
                buf_addc(&b, '%');
                buf_addc(&b, *p);
            }
        } else
            buf_addc(&b, *p);
    }
    free(dirc);
    free(basec);
    if (!b.s)
        return xstrdup("");
    return b.s;
}

static void write_ext_block(int type, const char *name, Buf *recs, Entry *e)
{
    unsigned char h[512];
    char prefix[156], nm[101];
    memset(h, 0, sizeof h);
    if (!split_name(name, prefix, nm)) {
        /* too long: PaxHeaders.<pid>/ and as much of the base name as fits */
        const char *base = strrchr(name, '/');
        char head[40];
        size_t hl, bl;
        base = base ? base + 1 : name;
        snprintf(head, sizeof head, "PaxHeaders.%ld/", (long)getpid());
        hl = strlen(head);
        bl = strlen(base);
        if (bl > 100 - hl)
            bl = 100 - hl;
        memset(prefix, 0, sizeof prefix);
        memset(nm, 0, sizeof nm);
        memcpy(nm, head, hl);
        memcpy(nm + hl, base, bl);
    }
    memcpy(h, nm, strnlen(nm, 100));
    memcpy(h + 345, prefix, strnlen(prefix, 155));
    put_octal(h + 100, 8, 0600);
    put_octal(h + 108, 8, e && fits_octal(8, e->uid) ? e->uid : 0);
    put_octal(h + 116, 8, e && fits_octal(8, e->gid) ? e->gid : 0);
    put_octal(h + 124, 12, recs->len);
    put_octal(h + 136, 12, e && e->mtime.tv_sec > 0 ? (uintmax_t)e->mtime.tv_sec : (uintmax_t)time(NULL));
    h[156] = (unsigned char)type;
    memcpy(h + 257, "ustar", 6);
    memcpy(h + 263, "00", 2);
    fill_checksum(h);
    arc_write(h, 512);
    arc_write(recs->s, recs->len);
    arc_pad_write((512 - recs->len % 512) % 512);
}

void write_global_header(void)
{
    Buf recs = { 0 };
    char *name;
    const char *tmpdir;
    if (arc_fmt != F_PAX || !opt_global)
        return;
    for (Kv *k = opt_global; k; k = k->next)
        if (!keyword_deleted(k->key))
            add_record(&recs, k->key, k->val, k->vlen);
    if (!recs.len)
        return;
    tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir)
        tmpdir = "/tmp";
    if (opt_globexthdr_name)
        name = subst_hdr_name(opt_globexthdr_name, "", ++global_seq);
    else {
        Buf t = { 0 };
        buf_fmt(&t, "%s/GlobalHead.%%p.%%n", tmpdir);
        name = subst_hdr_name(t.s, "", ++global_seq);
        buf_free(&t);
    }
    write_ext_block('g', name, &recs, NULL);
    free(name);
    buf_free(&recs);
}

static const char *user_name(uid_t uid)
{
    static uid_t last = (uid_t)-1;
    static char name[64];
    struct passwd *pw;
    if (uid == last)
        return name[0] ? name : NULL;
    last = uid;
    pw = getpwuid(uid);
    snprintf(name, sizeof name, "%s", pw ? pw->pw_name : "");
    return name[0] ? name : NULL;
}

static const char *group_name(gid_t gid)
{
    static gid_t last = (gid_t)-1;
    static char name[64];
    struct group *gr;
    if (gid == last)
        return name[0] ? name : NULL;
    last = gid;
    gr = getgrgid(gid);
    snprintf(name, sizeof name, "%s", gr ? gr->gr_name : "");
    return name[0] ? name : NULL;
}

void tar_write_entry(Entry *e)
{
    unsigned char h[512];
    char prefix[156], name[101];
    Buf recs = { 0 };
    char tname[PATH_MAX + 2];
    const char *path = e->path;
    const char *un = user_name(e->uid), *gn = group_name(e->gid);
    int pax = arc_fmt == F_PAX, fits_name, typeflag;
    off_t size = 0;
    int write_data = 0;

    /* directories are written with a trailing slash */
    if (e->type == T_DIR) {
        size_t l = strlen(path);
        if (l + 2 <= sizeof tname && (l == 0 || path[l - 1] != '/')) {
            snprintf(tname, sizeof tname, "%s/", path);
            path = tname;
        }
    }
    switch (e->type) {
    case T_REG:
        typeflag = '0';
        size = e->st.st_size;
        write_data = 1;
        break;
    case T_HARD:
        typeflag = '1';
        if (pax && opt_linkdata) {
            size = e->st.st_size;
            write_data = 1;
        }
        break;
    case T_LNK:
        typeflag = '2';
        break;
    case T_CHR:
        typeflag = '3';
        break;
    case T_BLK:
        typeflag = '4';
        break;
    case T_DIR:
        typeflag = '5';
        break;
    case T_FIFO:
        typeflag = '6';
        break;
    case T_SOCK:
        warnx_("%s: sockets cannot be archived", e->path);
        return;
    default:
        warnx_("%s: unknown file type, not archived", e->path);
        return;
    }
    memset(h, 0, sizeof h);
    fits_name = split_name(path, prefix, name);
    if (pax) {
        /* records given with -o keyword:=value come first */
        for (Kv *k = opt_local; k; k = k->next)
            if (!keyword_deleted(k->key))
                add_record(&recs, k->key, k->val, k->vlen);
#define NEED(key) (!kv_get(opt_local, key) && !keyword_deleted(key))
        /* the names are recorded in UTF-8, translated from the codeset of
           the locale; a file with a name that cannot be translated gets
           hdrcharset=BINARY and its names as they are */
        int want_path = (!fits_name || !portable_string(path)) && NEED("path");
        int want_link = e->link && (strlen(e->link) > 100 || !portable_string(e->link)) &&
                        NEED("linkpath");
        int want_un = un && !alnum_string(un) && NEED("uname");
        int want_gn = gn && !alnum_string(gn) && NEED("gname");
        int binary = binary_forced();
        char *u_path = NULL, *u_link = NULL, *u_un = NULL, *u_gn = NULL;
        if (!binary && ((want_path && !to_utf8(path, &u_path)) ||
                        (want_link && !to_utf8(e->link, &u_link)) ||
                        (want_un && !to_utf8(un, &u_un)) ||
                        (want_gn && !to_utf8(gn, &u_gn))))
            binary = 1;
        if (binary) {
            free(u_path);
            free(u_link);
            free(u_un);
            free(u_gn);
            u_path = want_path ? xstrdup(path) : NULL;
            u_link = want_link ? xstrdup(e->link) : NULL;
            u_un = want_un ? xstrdup(un) : NULL;
            u_gn = want_gn ? xstrdup(gn) : NULL;
            if ((want_path || want_link || want_un || want_gn) && NEED("hdrcharset") &&
                !kv_get(opt_global, "hdrcharset"))
                add_record(&recs, "hdrcharset", "BINARY", 6);
        }
        if (want_path)
            add_record(&recs, "path", u_path, strlen(u_path));
        if (want_link)
            add_record(&recs, "linkpath", u_link, strlen(u_link));
        if (!fits_octal(12, (uintmax_t)size) && NEED("size")) {
            char b[32];
            snprintf(b, sizeof b, "%jd", (intmax_t)size);
            add_record(&recs, "size", b, strlen(b));
        }
        if (!fits_octal(8, e->uid) && NEED("uid")) {
            char b[32];
            snprintf(b, sizeof b, "%ju", (uintmax_t)e->uid);
            add_record(&recs, "uid", b, strlen(b));
        }
        if (!fits_octal(8, e->gid) && NEED("gid")) {
            char b[32];
            snprintf(b, sizeof b, "%ju", (uintmax_t)e->gid);
            add_record(&recs, "gid", b, strlen(b));
        }
        if (want_un)
            add_record(&recs, "uname", u_un, strlen(u_un));
        if (want_gn)
            add_record(&recs, "gname", u_gn, strlen(u_gn));
        free(u_path);
        free(u_link);
        free(u_un);
        free(u_gn);
        if ((opt_times || e->mtime.tv_nsec || e->mtime.tv_sec < 0 ||
             !fits_octal(12, (uintmax_t)e->mtime.tv_sec)) && NEED("mtime")) {
            char b[64];
            time_string(e->mtime, b, sizeof b);
            add_record(&recs, "mtime", b, strlen(b));
        }
        if (opt_times && NEED("atime")) {
            char b[64];
            time_string(e->st.st_atim, b, sizeof b);
            add_record(&recs, "atime", b, strlen(b));
        }
#undef NEED
        if (!fits_name) {
            /* best effort in the ustar fields */
            size_t l = strlen(path);
            memset(prefix, 0, sizeof prefix);
            memset(name, 0, sizeof name);
            memcpy(name, path + (l > 100 ? l - 100 : 0), l > 100 ? 100 : l);
        }
    } else {
        if (!fits_name) {
            warnx_("%s: pathname too long for the ustar format; not archived", e->path);
            return;
        }
        if (e->link && strlen(e->link) > 100) {
            warnx_("%s: link name too long for the ustar format; not archived", e->path);
            return;
        }
        if (!fits_octal(12, (uintmax_t)size)) {
            warnx_("%s: file too large for the ustar format; not archived", e->path);
            return;
        }
    }
    memcpy(h, name, strnlen(name, 100));
    put_octal(h + 100, 8, e->perm & 07777);
    if (fits_octal(8, e->uid))
        put_octal(h + 108, 8, e->uid);
    else {
        if (!pax)
            warnx_("%s: user ID too large for the ustar format; stored as 0", e->path);
        put_octal(h + 108, 8, 0);
    }
    if (fits_octal(8, e->gid))
        put_octal(h + 116, 8, e->gid);
    else {
        if (!pax)
            warnx_("%s: group ID too large for the ustar format; stored as 0", e->path);
        put_octal(h + 116, 8, 0);
    }
    put_octal(h + 124, 12, fits_octal(12, (uintmax_t)size) ? (uintmax_t)size : 0);
    if (e->mtime.tv_sec >= 0 && fits_octal(12, (uintmax_t)e->mtime.tv_sec))
        put_octal(h + 136, 12, (uintmax_t)e->mtime.tv_sec);
    else
        put_octal(h + 136, 12, 0);
    h[156] = (unsigned char)typeflag;
    if (e->link) {
        size_t l = strlen(e->link);
        memcpy(h + 157, e->link, l > 100 ? 100 : l);
    }
    memcpy(h + 257, "ustar", 6);
    memcpy(h + 263, "00", 2);
    if (un)
        memcpy(h + 265, un, strlen(un) > 31 ? 31 : strlen(un));
    if (gn)
        memcpy(h + 297, gn, strlen(gn) > 31 ? 31 : strlen(gn));
    if (e->type == T_CHR || e->type == T_BLK) {
        put_octal(h + 329, 8, major(e->st.st_rdev));
        put_octal(h + 337, 8, minor(e->st.st_rdev));
    }
    memcpy(h + 345, prefix, strnlen(prefix, 155));
    fill_checksum(h);
    if (recs.len) {
        char *xn = subst_hdr_name(opt_exthdr_name ? opt_exthdr_name : "%d/PaxHeaders.%p/%f", path, 0);
        write_ext_block('x', xn, &recs, e);
        free(xn);
    }
    buf_free(&recs);
    arc_write(h, 512);
    if (write_data) {
        e->size = size;
        copy_file_to_archive(e);
        arc_pad_write((size_t)((512 - size % 512) % 512));
    }
}

void tar_write_end(void)
{
    arc_pad_write(1024);
}


