/*
 * extract.c - creating files from archive members (read mode) and from
 * source files (copy mode), with the -k, -u and -p rules; moving data
 * between files and the archive.
 */
#include "pax.h"

int keep_atime = 1, keep_mtime = 1, keep_owner, keep_mode;
int is_root;

/* ---------- data ---------- */

static int write_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t r = write(fd, p, n);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

/* copy e->size bytes of member data from the archive to fd (or skip) */
void read_entry_data(Entry *e, int fd)
{
    char buf[65536];
    off_t left = e->size, pad = 0;
    int werr = 0;
    if (arc_fmt == F_USTAR || arc_fmt == F_PAX || arc_fmt == F_OLDTAR)
        pad = (512 - e->size % 512) % 512;
    else if (arc_fmt == F_NEWC)
        pad = (4 - e->size % 4) % 4;
    if (fd < 0) {
        arc_skip(left + pad);
        return;
    }
    while (left > 0) {
        size_t k = left > (off_t)sizeof buf ? sizeof buf : (size_t)left;
        arc_read_full(buf, k);
        if (!werr && write_all(fd, buf, k) < 0) {
            warn_("%s: write error", e->path);
            werr = 1;
        }
        left -= (off_t)k;
    }
    arc_skip(pad);
}

/* copy a file's contents (e->size bytes) into the archive */
int copy_file_to_archive(Entry *e)
{
    char buf[65536];
    off_t left = e->size;
    int fd = open(e->src, O_RDONLY);
    if (fd < 0) {
        warn_("%s", e->src);
        arc_pad_write((size_t)left);
        return -1;
    }
    while (left > 0) {
        size_t k = left > (off_t)sizeof buf ? sizeof buf : (size_t)left;
        ssize_t r = read(fd, buf, k);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            if (r < 0)
                warn_("%s: read error", e->src);
            else
                warnx_("%s: file shrank while being archived; padded with zeros", e->src);
            arc_pad_write((size_t)left);
            break;
        }
        arc_write(buf, (size_t)r);
        left -= r;
    }
    if (opt_t) {
        struct timespec ts[2];
        ts[0] = e->st.st_atim;
        ts[1].tv_sec = 0;
        ts[1].tv_nsec = UTIME_OMIT;
        futimens(fd, ts);
    }
    close(fd);
    return 0;
}

/* ---------- attributes ---------- */

typedef struct DirFix {
    char *path;
    Entry e;
    int created;
    struct DirFix *next;
} DirFix;
static DirFix *dirfixes;
static mode_t cur_umask;

static uid_t owner_uid(Entry *e)
{
    if (e->uname) {
        struct passwd *pw = getpwnam(e->uname);
        if (pw)
            return pw->pw_uid;
    }
    return e->uid;
}

static gid_t owner_gid(Entry *e)
{
    if (e->gname) {
        struct group *gr = getgrnam(e->gname);
        if (gr)
            return gr->gr_gid;
    }
    return e->gid;
}

/* set ownership, mode and times of an extracted file */
static void set_attributes(Entry *e, const char *path, int created)
{
    int owner_ok = 0;
    if (keep_owner) {
        if (lchown(path, owner_uid(e), owner_gid(e)) < 0)
            warn_("%s: cannot preserve the owner and group", path);
        else
            owner_ok = 1;
    }
    if (e->type != T_LNK && e->type != T_HARD) {
        if (keep_mode) {
            mode_t m = e->perm & (owner_ok ? 07777 : 01777);
            if (chmod(path, m) < 0)
                warn_("%s: cannot preserve the file mode", path);
        } else if (e->type == T_DIR && created) {
            /* created writable; now as creat() with the archived mode */
            if (chmod(path, e->perm & 0777 & ~cur_umask) < 0)
                warn_("%s: cannot set the file mode", path);
        }
    }
    if (e->type != T_HARD && (keep_mtime || (keep_atime && e->has_atime))) {
        struct timespec ts[2];
        ts[0].tv_sec = 0;
        ts[0].tv_nsec = UTIME_OMIT;
        ts[1].tv_sec = 0;
        ts[1].tv_nsec = UTIME_OMIT;
        if (keep_atime && e->has_atime)
            ts[0] = e->atime;
        if (keep_mtime)
            ts[1] = e->mtime;
        if (utimensat(AT_FDCWD, path, ts, AT_SYMLINK_NOFOLLOW) < 0 && e->type != T_LNK)
            warn_("%s: cannot set the file times", path);
    }
}

void finish_directories(void)
{
    /* last extracted first, so that inner directories are done before outer ones */
    DirFix *d = dirfixes;
    while (d) {
        DirFix *n = d->next;
        set_attributes(&d->e, d->path, d->created);
        free(d->path);
        entry_free(&d->e);
        free(d);
        d = n;
    }
    dirfixes = NULL;
}

static void defer_dir(Entry *e, const char *path, int created)
{
    DirFix *d = xmalloc(sizeof *d);
    d->path = xstrdup(path);
    entry_init(&d->e);
    d->e.type = T_DIR;
    d->e.perm = e->perm;
    d->e.uid = e->uid;
    d->e.gid = e->gid;
    d->e.uname = e->uname ? xstrdup(e->uname) : NULL;
    d->e.gname = e->gname ? xstrdup(e->gname) : NULL;
    d->e.mtime = e->mtime;
    d->e.atime = e->atime;
    d->e.has_atime = e->has_atime;
    d->created = created;
    d->next = dirfixes;
    dirfixes = d;
}

/* create the missing intermediate directories of path */
static void make_parents(const char *path)
{
    char *p = xstrdup(path);
    size_t l = strlen(p);
    while (l > 1 && p[l - 1] == '/')
        p[--l] = 0;
    for (char *s = p + 1; *s; s++) {
        struct stat st;
        if (*s != '/')
            continue;
        *s = 0;
        if (stat(p, &st) < 0) {
            if (mkdir(p, 0777) < 0 && errno != EEXIST) {
                /* reported when the file itself cannot be created */
            }
        }
        *s = '/';
    }
    free(p);
}

/* copy data to a newly created file */
static int fill_file(Entry *e, int fd, int src_fd, const char *path)
{
    if (src_fd < 0) {
        read_entry_data(e, fd);
        return 0;
    }
    {
        char buf[65536];
        ssize_t r;
        for (;;) {
            r = read(src_fd, buf, sizeof buf);
            if (r < 0 && errno == EINTR)
                continue;
            if (r <= 0)
                break;
            if (write_all(fd, buf, (size_t)r) < 0) {
                warn_("%s", path);
                return -1;
            }
        }
        if (r < 0) {
            warn_("%s", e->src);
            return -1;
        }
    }
    return 0;
}

/*
 * Create the file described by e at path dest.  Data comes from the
 * archive (src_fd < 0) or from src_fd (copy mode).  Returns 0 on success,
 * 1 if the file was skipped, -1 on error.
 */
int extract_entry(Entry *e, int src_fd, const char *dest)
{
    struct stat st;
    int exists, fd, created = 1;
    static int umask_known;
    if (!umask_known) {
        cur_umask = umask(0);
        umask(cur_umask);
        umask_known = 1;
    }
    if (!*dest)
        goto skip;
    exists = lstat(dest, &st) == 0;
    if (!exists)
        make_parents(dest);
    if (exists) {
        if (e->type == T_DIR && S_ISDIR(st.st_mode)) {
            if (opt_k)
                return 1;
            if (opt_u && !newer_than_existing(e, dest))
                return 1;
            defer_dir(e, dest, 0);
            return 0;
        }
        if (e->type == T_FIFO && S_ISFIFO(st.st_mode) && !opt_u && !opt_k) {
            set_attributes(e, dest, 0);
            return 0;
        }
        if (opt_k)
            goto skip;
        if (opt_u && !newer_than_existing(e, dest))
            goto skip;
        if (e->type == T_HARD) {
            /* linking a name to the file it already is */
            char *t = e->link;
            struct stat ts;
            if (t && stat(t, &ts) == 0 && stat(dest, &st) == 0 && ts.st_dev == st.st_dev &&
                ts.st_ino == st.st_ino)
                goto data_only;
        }
        if (S_ISDIR(st.st_mode)) {
            if (rmdir(dest) < 0) {
                warn_("%s: cannot replace directory", dest);
                goto skip;
            }
        } else if (unlink(dest) < 0) {
            warn_("%s: cannot remove", dest);
            goto skip;
        }
    }
    switch (e->type) {
    case T_DIR:
        if (mkdir(dest, 0700 | (e->perm & 0777)) < 0) {
            warn_("%s: cannot create directory", dest);
            return -1;
        }
        defer_dir(e, dest, created);
        return 0;
    case T_LNK:
        if (symlink(e->link ? e->link : "", dest) < 0) {
            warn_("%s: cannot create symbolic link", dest);
            goto skip;
        }
        set_attributes(e, dest, 1);
        return 0;
    case T_HARD:
        if (!e->link || link(e->link, dest) < 0) {
            if (src_fd < 0 && e->size > 0) {
                /* the data is here (-o linkdata): make it a regular file */
                Entry t = *e;
                t.type = T_REG;
                return extract_entry(&t, src_fd, dest);
            }
            warn_("%s: cannot link to %s", dest, e->link ? e->link : "?");
            goto skip;
        }
    data_only:
        if (e->size > 0 || src_fd >= 0) {
            /* a link with data (pax -o linkdata, cpio) */
            if (src_fd < 0) {
                fd = open(dest, O_WRONLY | O_TRUNC);
                if (fd < 0) {
                    warn_("%s", dest);
                    goto skip;
                }
                read_entry_data(e, fd);
                close(fd);
                {
                    Entry t = *e;
                    t.type = T_REG;
                    set_attributes(&t, dest, 1);
                }
            }
        }
        return 0;
    case T_FIFO:
        if (mkfifo(dest, e->perm & 0777) < 0) {
            warn_("%s: cannot create FIFO", dest);
            return -1;
        }
        set_attributes(e, dest, 1);
        return 0;
    case T_CHR:
    case T_BLK:
        if (mknod(dest, (e->type == T_CHR ? S_IFCHR : S_IFBLK) | (e->perm & 0777),
                  makedev(e->devmajor, e->devminor)) < 0) {
            warn_("%s: cannot create special file", dest);
            return -1;
        }
        set_attributes(e, dest, 1);
        return 0;
    case T_SOCK:
        warnx_("%s: cannot create a socket", dest);
        goto skip;
    case T_OTHER:
        warnx_("%s: unknown file type %c; extracted as a regular file", dest,
               e->typeflag >= ' ' && e->typeflag < 127 ? e->typeflag : '?');
        /* FALLTHROUGH */
    default:
        fd = open(dest, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, e->perm & 0777);
        if (fd < 0) {
            warn_("%s: cannot create", dest);
            goto skip;
        }
        fill_file(e, fd, src_fd, dest);
        if (close(fd) < 0)
            warn_("%s", dest);
        set_attributes(e, dest, 1);
        return 0;
    }
skip:
    if (src_fd < 0)
        read_entry_data(e, -1);
    return 1;
}
