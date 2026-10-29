/*
 * walk.c - traversal of file hierarchies for write and copy modes:
 * -d, -H, -L, -X and detection of infinite loops.
 */
#include "pax.h"

typedef struct Anc {
    dev_t dev;
    ino_t ino;
    struct Anc *up;
} Anc;

int walk_prune;

static int type_of(const struct stat *st)
{
    if (S_ISREG(st->st_mode))
        return T_REG;
    if (S_ISDIR(st->st_mode))
        return T_DIR;
    if (S_ISLNK(st->st_mode))
        return T_LNK;
    if (S_ISCHR(st->st_mode))
        return T_CHR;
    if (S_ISBLK(st->st_mode))
        return T_BLK;
    if (S_ISFIFO(st->st_mode))
        return T_FIFO;
    if (S_ISSOCK(st->st_mode))
        return T_SOCK;
    return T_OTHER;
}

static int cmpstr(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void visit(const char *path, struct stat *st, dev_t root_dev, Anc *anc, WalkFn fn, int operand)
{
    Entry e;
    entry_init(&e);
    e.path = xstrdup(path);
    e.src = xstrdup(path);
    e.st = *st;
    e.type = type_of(st);
    e.perm = st->st_mode & 07777;
    e.uid = st->st_uid;
    e.gid = st->st_gid;
    e.mtime = st->st_mtim;
    e.atime = st->st_atim;
    e.has_atime = 1;
    e.nlink = (unsigned long)st->st_nlink;
    e.size = e.type == T_REG ? st->st_size : 0;
    e.is_operand = operand;
    if (e.type == T_CHR || e.type == T_BLK) {
        e.devmajor = major(st->st_rdev);
        e.devminor = minor(st->st_rdev);
    }
    if (e.type == T_LNK) {
        char buf[PATH_MAX + 1];
        ssize_t n = readlink(path, buf, sizeof buf - 1);
        if (n < 0) {
            warn_("%s", path);
            entry_free(&e);
            return;
        }
        buf[n] = 0;
        e.link = xstrdup(buf);
    }
    walk_prune = 0;
    fn(&e);
    entry_free(&e);
    if (S_ISDIR(st->st_mode) && !opt_d && !walk_prune) {
        DIR *d;
        struct dirent *de;
        char **names = NULL;
        int n = 0;
        Anc me;
        if (opt_X && st->st_dev != root_dev)
            return;
        for (Anc *a = anc; a; a = a->up)
            if (a->dev == st->st_dev && a->ino == st->st_ino) {
                warnx_("%s: infinite loop in the file hierarchy", path);
                exit(1);
            }
        me.dev = st->st_dev;
        me.ino = st->st_ino;
        me.up = anc;
        d = opendir(path);
        if (!d) {
            warn_("%s", path);
            return;
        }
        while ((de = readdir(d))) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                continue;
            names = xrealloc(names, (size_t)(n + 1) * sizeof(char *));
            names[n++] = xstrdup(de->d_name);
        }
        closedir(d);
        if (n > 1)
            qsort(names, (size_t)n, sizeof(char *), cmpstr);
        for (int i = 0; i < n; i++) {
            Buf child = { 0 };
            struct stat cst;
            size_t pl = strlen(path);
            buf_adds(&child, path);
            if (pl == 0 || path[pl - 1] != '/')
                buf_addc(&child, '/');
            buf_adds(&child, names[i]);
            if ((opt_L ? stat(child.s, &cst) : lstat(child.s, &cst)) < 0 &&
                (!opt_L || lstat(child.s, &cst) < 0)) {
                warn_("%s", child.s);
            } else
                visit(child.s, &cst, root_dev, &me, fn, 0);
            buf_free(&child);
            free(names[i]);
        }
        free(names);
    }
}

void walk_operand(const char *path, WalkFn fn)
{
    struct stat st;
    int r;
    if (opt_H || opt_L) {
        r = stat(path, &st);
        if (r < 0)
            r = lstat(path, &st);
    } else
        r = lstat(path, &st);
    if (r < 0) {
        warn_("%s", path);
        return;
    }
    visit(path, &st, st.st_dev, NULL, fn, 1);
}
