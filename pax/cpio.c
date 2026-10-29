/*
 * cpio.c - the octet-oriented cpio interchange format (odc, magic 070707).
 * The SVR4 "newc" format (070701/070702) is accepted on input.
 */
#include "pax.h"

off_t cpio_end_off;

static int get_num(const unsigned char *p, int n, int base, uintmax_t *v)
{
    uintmax_t r = 0;
    for (int i = 0; i < n; i++) {
        int c = p[i], d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            return 0;
        if (d >= base)
            return 0;
        r = r * (uintmax_t)base + (uintmax_t)d;
    }
    *v = r;
    return 1;
}

static int type_of_mode(mode_t m)
{
    switch (m & 0170000) {
    case 0100000:
        return T_REG;
    case 0040000:
        return T_DIR;
    case 0120000:
        return T_LNK;
    case 0020000:
        return T_CHR;
    case 0060000:
        return T_BLK;
    case 0010000:
        return T_FIFO;
    case 0140000:
        return T_SOCK;
    case 0110000:
        return T_CONTIG;
    default:
        return T_OTHER;
    }
}

int cpio_read_header(Entry *e)
{
    unsigned char h[110];
    uintmax_t v[13];
    size_t namesize;
    off_t start = arc_offset();
    int newc;
    if (arc_read(h, 6) != 1)
        errx_("unexpected end of archive (no trailer)");
    if (memcmp(h, "070707", 6) == 0)
        newc = 0;
    else if (memcmp(h, "070701", 6) == 0 || memcmp(h, "070702", 6) == 0)
        newc = 1;
    else
        errx_("invalid cpio header: the archive is damaged");
    entry_init(e);
    if (!newc) {
        static const int w[] = { 6, 6, 6, 6, 6, 6, 6, 11, 6, 11 };
        int off = 6;
        arc_read_full(h + 6, 70);
        for (int i = 0; i < 10; i++) {
            if (!get_num(h + off, w[i], 8, &v[i]))
                errx_("invalid cpio header: the archive is damaged");
            off += w[i];
        }
        e->cdev = (unsigned long)v[0];
        e->cino = (unsigned long)v[1];
        e->perm = (mode_t)(v[2] & 07777);
        e->type = type_of_mode((mode_t)v[2]);
        e->uid = (uid_t)v[3];
        e->gid = (gid_t)v[4];
        e->nlink = (unsigned long)v[5];
        e->devmajor = major((dev_t)v[6]);
        e->devminor = minor((dev_t)v[6]);
        e->mtime.tv_sec = (time_t)v[7];
        namesize = (size_t)v[8];
        e->size = (off_t)v[9];
        e->hdrlen = 76;
        e->fmt = F_CPIO;
    } else {
        arc_read_full(h + 6, 104);
        for (int i = 0; i < 13; i++)
            if (!get_num(h + 6 + 8 * i, 8, 16, &v[i]))
                errx_("invalid cpio header: the archive is damaged");
        e->cino = (unsigned long)v[0];
        e->perm = (mode_t)(v[1] & 07777);
        e->type = type_of_mode((mode_t)v[1]);
        e->uid = (uid_t)v[2];
        e->gid = (gid_t)v[3];
        e->nlink = (unsigned long)v[4];
        e->mtime.tv_sec = (time_t)v[5];
        e->size = (off_t)v[6];
        e->cdev = (unsigned long)(v[7] << 16 | v[8]);
        e->devmajor = (unsigned long)v[9];
        e->devminor = (unsigned long)v[10];
        namesize = (size_t)v[11];
        e->hdrlen = 110;
        e->fmt = F_NEWC;
    }
    memcpy(e->hdr, h, (size_t)e->hdrlen);
    if (namesize == 0 || namesize > 65536)
        errx_("invalid cpio header: the archive is damaged");
    {
        char *name = xmalloc(namesize + 1);
        arc_read_full(name, namesize);
        name[namesize] = 0;
        e->path = xstrndup(name, strnlen(name, namesize));
        free(name);
        if (newc)
            arc_skip((off_t)((4 - (110 + namesize) % 4) % 4));
    }
    if (strcmp(e->path, "TRAILER!!!") == 0) {
        cpio_end_off = start;
        entry_free(e);
        return 0;
    }
    if (e->type == T_LNK) {
        if (e->size > PATH_MAX * 4)
            errx_("%s: symbolic link too long", e->path);
        e->link = xmalloc((size_t)e->size + 1);
        arc_read_full(e->link, (size_t)e->size);
        e->link[e->size] = 0;
        if (newc)
            arc_skip((off_t)((4 - e->size % 4) % 4));
        e->size = 0;
    }
    if (e->type == T_DIR || e->type == T_FIFO) {
        /* no data */
        if (e->size) {
            arc_skip(e->size);
            e->size = 0;
        }
    }
    return 1;
}

/* ---------- writing (odc) ---------- */

typedef struct Ident {
    dev_t dev;
    ino_t ino;
    unsigned long n;
    struct Ident *next;
} Ident;
static Ident *idents[1024];
static unsigned long next_ino = 1;

static unsigned long ident_of(dev_t dev, ino_t ino)
{
    unsigned h = (unsigned)((dev * 31 + ino) % 1024);
    for (Ident *i = idents[h]; i; i = i->next)
        if (i->dev == dev && i->ino == ino)
            return i->n;
    {
        Ident *i = xmalloc(sizeof *i);
        i->dev = dev;
        i->ino = ino;
        i->n = next_ino++;
        i->next = idents[h];
        idents[h] = i;
        return i->n;
    }
}

static int put_field(char *out, int width, uintmax_t v)
{
    char tmp[32];
    int l = snprintf(tmp, sizeof tmp, "%0*jo", width, v);
    if (l > width)
        return 0;
    memcpy(out, tmp, (size_t)width);
    return 1;
}

static int write_hdr(Entry *e, mode_t mode, uintmax_t rdev, const char *name, uintmax_t fsize,
                     unsigned long ident, unsigned long nlink)
{
    char h[77];
    size_t nlen = strlen(name) + 1;
    unsigned long cdev = ident / 0777777, cino = ident % 0777777;
    int ok = 1;
    memcpy(h, "070707", 6);
    ok &= put_field(h + 6, 6, cdev);
    ok &= put_field(h + 12, 6, cino);
    ok &= put_field(h + 18, 6, mode);
    if (!put_field(h + 24, 6, e ? e->uid : 0)) {
        warnx_("%s: user ID too large for the cpio format", name);
        return 0;
    }
    if (!put_field(h + 30, 6, e ? e->gid : 0)) {
        warnx_("%s: group ID too large for the cpio format", name);
        return 0;
    }
    ok &= put_field(h + 36, 6, nlink);
    if (!put_field(h + 42, 6, rdev))
        put_field(h + 42, 6, 0);
    if (!put_field(h + 48, 11, e && e->mtime.tv_sec > 0 ? (uintmax_t)e->mtime.tv_sec : 0)) {
        warnx_("%s: modification time cannot be represented in the cpio format", name);
        return 0;
    }
    if (!put_field(h + 59, 6, nlen)) {
        warnx_("%s: pathname too long for the cpio format", name);
        return 0;
    }
    if (!put_field(h + 65, 11, fsize)) {
        warnx_("%s: file too large for the cpio format", name);
        return 0;
    }
    if (!ok) {
        warnx_("%s: cannot be represented in the cpio format", name);
        return 0;
    }
    arc_write(h, 76);
    arc_write(name, nlen);
    return 1;
}

void cpio_write_entry(Entry *e)
{
    mode_t mode = e->perm & 07777;
    uintmax_t rdev = 0;
    off_t size = 0;
    unsigned long id = ident_of(e->st.st_dev, e->st.st_ino);
    switch (e->type) {
    case T_REG:
    case T_HARD:
        mode |= 0100000;
        size = e->st.st_size;
        break;
    case T_DIR:
        mode |= 0040000;
        break;
    case T_LNK:
        mode |= 0120000;
        size = (off_t)strlen(e->link);
        break;
    case T_CHR:
        mode |= 0020000;
        rdev = e->st.st_rdev;
        break;
    case T_BLK:
        mode |= 0060000;
        rdev = e->st.st_rdev;
        break;
    case T_FIFO:
        mode |= 0010000;
        break;
    case T_SOCK:
        mode |= 0140000;
        break;
    default:
        warnx_("%s: unknown file type, not archived", e->path);
        return;
    }
    if (!write_hdr(e, mode, rdev, e->path, (uintmax_t)size, id, (unsigned long)e->st.st_nlink))
        return;
    if (e->type == T_LNK)
        arc_write(e->link, (size_t)size);
    else if (size) {
        e->size = size;
        copy_file_to_archive(e);
    }
}

void cpio_write_end(void)
{
    write_hdr(NULL, 0, 0, "TRAILER!!!", 0, 0, 1);
}
