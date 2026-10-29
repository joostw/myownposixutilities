/*
 * main.c - option parsing and the four modes of pax: list, read, write
 * and copy.
 */
#include "pax.h"

int opt_r, opt_w, opt_a, opt_c, opt_d, opt_i, opt_k, opt_l, opt_n, opt_t, opt_u, opt_v, opt_X;
int opt_H, opt_L;
int opt_fmt;
static char *opt_f;
static long opt_b;

extern off_t tar_end_off, cpio_end_off;

static void usage(void)
{
    fprintf(stderr,
            "usage: pax [-dv] [-c|-n] [-H|-L] [-o options] [-f archive] [-s replstr]... [pattern...]\n"
            "       pax -r [-c|-n] [-dikuv] [-H|-L] [-f archive] [-o options]... [-p string]...\n"
            "              [-s replstr]... [pattern...]\n"
            "       pax -w [-dituvX] [-H|-L] [-b blocksize] [[-a] [-f archive]] [-o options]...\n"
            "              [-s replstr]... [-x format] [file...]\n"
            "       pax -r -w [-dikltuvX] [-H|-L] [-o options]... [-p string]... [-s replstr]...\n"
            "              [file...] directory\n");
    exit(1);
}

/* ---------- -o ---------- */

static void parse_o(const char *arg)
{
    const char *p = arg;
    for (;;) {
        Buf key = { 0 }, val = { 0 };
        int has_val = 0, local = 0;
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (!*p)
            break;
        while (*p && *p != '=' && *p != ',' && !(*p == ':' && p[1] == '='))
            buf_addc(&key, *p++);
        if (!key.s) {
            if (*p == ',') {
                p++;
                continue;
            }
            errx_("-o %s: missing keyword", arg);
        }
        if (*p == ':' && p[1] == '=') {
            local = 1;
            has_val = 1;
            p += 2;
        } else if (*p == '=') {
            has_val = 1;
            p++;
        }
        if (has_val && strcmp(key.s, "listopt") == 0) {
            /* the rest of the argument is the format */
            size_t old = opt_listopt ? strlen(opt_listopt) : 0, l = strlen(p);
            opt_listopt = xrealloc(opt_listopt, old + l + 1);
            memcpy(opt_listopt + old, p, l + 1);
            buf_free(&key);
            return;
        }
        if (has_val) {
            while (*p && *p != ',') {
                if (*p == '\\' && p[1] == ',') {
                    buf_addc(&val, ',');
                    p += 2;
                    continue;
                }
                buf_addc(&val, *p++);
            }
        }
        if (*p == ',')
            p++;
        {
            const char *k = key.s, *v = val.s ? val.s : "";
            if (strcmp(k, "delete") == 0 && has_val && !local) {
                opt_delete = xrealloc(opt_delete, (size_t)(nopt_delete + 1) * sizeof(char *));
                opt_delete[nopt_delete++] = xstrdup(v);
            } else if (strcmp(k, "exthdr.name") == 0 && has_val) {
                free(opt_exthdr_name);
                opt_exthdr_name = xstrdup(v);
            } else if (strcmp(k, "globexthdr.name") == 0 && has_val) {
                free(opt_globexthdr_name);
                opt_globexthdr_name = xstrdup(v);
            } else if (strcmp(k, "invalid") == 0 && has_val) {
                if (strcmp(v, "binary") && strcmp(v, "bypass") && strcmp(v, "rename") &&
                    strcmp(v, "UTF-8") && strcmp(v, "write"))
                    errx_("-o invalid=%s: unknown action", v);
                free(opt_invalid);
                opt_invalid = xstrdup(v);
            } else if (strcmp(k, "linkdata") == 0 && !has_val) {
                opt_linkdata = 1;
            } else if (strcmp(k, "times") == 0 && !has_val) {
                opt_times = 1;
            } else if (has_val) {
                if (strchr(k, '='))
                    errx_("-o %s: invalid keyword", k);
                kv_set(local ? &opt_local : &opt_global, k, v, val.len);
            } else
                errx_("-o %s: unknown option keyword", k);
        }
        buf_free(&key);
        buf_free(&val);
    }
}

static void parse_p(const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case 'a':
            keep_atime = 0;
            break;
        case 'e':
            keep_owner = keep_mode = keep_atime = keep_mtime = 1;
            break;
        case 'm':
            keep_mtime = 0;
            break;
        case 'o':
            keep_owner = 1;
            break;
        case 'p':
            keep_mode = 1;
            break;
        default:
            errx_("-p %c: unknown file characteristic", *s);
        }
    }
}

static long parse_blocksize(const char *s)
{
    long total = 1;
    const char *p = s;
    for (;;) {
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p || v <= 0)
            errx_("-b %s: invalid block size", s);
        if (*end == 'b') {
            v *= 512;
            end++;
        } else if (*end == 'k') {
            v *= 1024;
            end++;
        } else if (*end == 'w') {
            v *= 2;
            end++;
        }
        total *= v;
        if (total > 32 * 1024 * 1024)
            errx_("-b %s: block size too large", s);
        if (*end == 'x') {
            p = end + 1;
            continue;
        }
        if (*end)
            errx_("-b %s: invalid block size", s);
        break;
    }
    return total;
}

/* ---------- reading lines of file names from standard input ---------- */

static char *read_name_line(void)
{
    static char *line;
    static size_t cap;
    ssize_t n;
    for (;;) {
        n = getline(&line, &cap, stdin);
        if (n < 0)
            return NULL;
        if (n > 0 && line[n - 1] == '\n')
            line[--n] = 0;
        if (n > 0)
            return line;
    }
}

/* ---------- hard link tables ---------- */

typedef struct Link {
    dev_t dev;
    ino_t ino;
    char *name;
    struct Link *next;
} Link;
static Link *links[4096];

static const char *link_lookup(dev_t dev, ino_t ino, const char *name)
{
    unsigned h = (unsigned)((dev * 131 + ino) % 4096);
    for (Link *l = links[h]; l; l = l->next)
        if (l->dev == dev && l->ino == ino)
            return l->name;
    if (name) {
        Link *l = xmalloc(sizeof *l);
        l->dev = dev;
        l->ino = ino;
        l->name = xstrdup(name);
        l->next = links[h];
        links[h] = l;
    }
    return NULL;
}

/* ---------- write mode ---------- */

typedef struct Old {
    char *name;
    struct timespec mtime;
    struct Old *next;
} Old;
static Old *oldtab[4096];

static unsigned shash(const char *s)
{
    unsigned h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h % 4096;
}

static void old_add(const char *name, struct timespec t)
{
    unsigned h = shash(name);
    Old *o;
    for (o = oldtab[h]; o; o = o->next)
        if (strcmp(o->name, name) == 0) {
            if (t.tv_sec > o->mtime.tv_sec ||
                (t.tv_sec == o->mtime.tv_sec && t.tv_nsec > o->mtime.tv_nsec))
                o->mtime = t;
            return;
        }
    o = xmalloc(sizeof *o);
    o->name = xstrdup(name);
    o->mtime = t;
    o->next = oldtab[h];
    oldtab[h] = o;
}

static Old *old_find(const char *name)
{
    for (Old *o = oldtab[shash(name)]; o; o = o->next)
        if (strcmp(o->name, name) == 0)
            return o;
    return NULL;
}

static char *strip_slashes(const char *s)
{
    char *r = xstrdup(s);
    size_t l = strlen(r);
    while (l > 1 && r[l - 1] == '/')
        r[--l] = 0;
    return r;
}

static void write_one(Entry *e)
{
    if (arc_is_file && e->st.st_dev == arc_dev && e->st.st_ino == arc_ino) {
        fprintf(stderr, "%s: %s: the archive itself is not archived\n", progname, e->path);
        return;
    }
    if (opt_u) {
        char *n = strip_slashes(e->path);
        Old *o = old_find(n);
        free(n);
        if (o && !(e->mtime.tv_sec > o->mtime.tv_sec ||
                   (e->mtime.tv_sec == o->mtime.tv_sec && e->mtime.tv_nsec > o->mtime.tv_nsec)))
            return;
    }
    if (nsubsts()) {
        if (!apply_substs(&e->path, 0))
            return;
        if (e->type == T_LNK && e->link)
            apply_substs(&e->link, 1);
    }
    if (opt_i && !interactive_rename(&e->path))
        return;
    if (e->type != T_DIR && e->st.st_nlink > 1) {
        const char *first = link_lookup(e->st.st_dev, e->st.st_ino, e->path);
        if (first && arc_fmt != F_CPIO) {
            e->type = T_HARD;
            free(e->link);
            e->link = xstrdup(first);
        }
    }
    if (opt_v) {
        fprintf(stderr, "%s", e->path);
        fflush(stderr);
    }
    if (arc_fmt == F_CPIO)
        cpio_write_entry(e);
    else
        tar_write_entry(e);
    if (opt_v) {
        fputc('\n', stderr);
        fflush(stderr);
    }
}

/* scan an existing archive for -a: its format, end and member times */
static void scan_for_append(void)
{
    Entry e;
    int fmt = detect_format(), saw_x = 0;
    int family = (fmt == F_CPIO || fmt == F_NEWC) ? F_CPIO : F_USTAR;
    if (fmt == F_NEWC)
        errx_("appending to newc cpio archives is not supported");
    if (opt_fmt) {
        int want = opt_fmt == F_CPIO ? F_CPIO : F_USTAR;
        if (want != family)
            errx_("the archive is in a different format; cannot append");
    }
    arc_fmt = fmt == F_OLDTAR ? F_USTAR : fmt;
    for (;;) {
        int r = arc_fmt == F_CPIO ? cpio_read_header(&e) : tar_read_header(&e);
        if (!r)
            break;
        if (e.fmt == F_PAX)
            saw_x = 1;
        if (opt_u) {
            char *n = strip_slashes(e.path);
            old_add(n, e.mtime);
            free(n);
        }
        read_entry_data(&e, -1);
        entry_free(&e);
    }
    if (arc_fmt != F_CPIO)
        arc_fmt = (opt_fmt == F_PAX || saw_x) ? F_PAX : F_USTAR;
    if (opt_fmt == F_USTAR)
        arc_fmt = F_USTAR;
    arc_seek_write(arc_fmt == F_CPIO ? cpio_end_off : tar_end_off);
}

static void write_mode(int argc, char **argv)
{
    arc_fmt = opt_fmt ? opt_fmt : F_USTAR;
    if (!opt_b)
        opt_b = arc_fmt == F_CPIO ? 5120 : 10240;
    blocksize = opt_b;
    if (arc_fmt != F_CPIO && blocksize % 512)
        errx_("-b %ld: block size must be a multiple of 512", blocksize);
    if (opt_a) {
        if (!opt_f)
            errx_("-a requires -f");
        arc_open_write(opt_f, 1);
        scan_for_append();
    } else {
        arc_open_write(opt_f, 0);
        write_global_header();
    }
    if (argc > 0)
        for (int i = 0; i < argc; i++)
            walk_operand(argv[i], write_one);
    else {
        char *l;
        while ((l = read_name_line()))
            walk_operand(l, write_one);
    }
    if (arc_fmt == F_CPIO)
        cpio_write_end();
    else
        tar_write_end();
    arc_finish_write();
}

/* ---------- list and read modes ---------- */

typedef struct CpioId {
    unsigned long dev, ino;
    char *name;
    struct CpioId *next;
} CpioId;
static CpioId *cpioids[4096];

static const char *cpio_link(Entry *e)
{
    unsigned h = (unsigned)((e->cdev * 131 + e->cino) % 4096);
    for (CpioId *c = cpioids[h]; c; c = c->next)
        if (c->dev == e->cdev && c->ino == e->cino)
            return c->name;
    {
        CpioId *c = xmalloc(sizeof *c);
        c->dev = e->cdev;
        c->ino = e->cino;
        c->name = xstrdup(e->path);
        c->next = cpioids[h];
        cpioids[h] = c;
    }
    return NULL;
}

int all_patterns_done(void);

static void read_mode(int argc, char **argv)
{
    Entry e;
    patterns = argv;
    npatterns = argc;
    arc_open_read(opt_f);
    arc_fmt = detect_format();
    if (arc_fmt == F_OLDTAR)
        arc_fmt = F_USTAR;
    for (;;) {
        int r;
        char *orig;
        if (arc_fmt == F_CPIO || arc_fmt == F_NEWC)
            r = cpio_read_header(&e);
        else
            r = tar_read_header(&e);
        if (!r)
            break;
        if (!select_member(&e) || (opt_r && !invalid_action(&e))) {
            read_entry_data(&e, -1);
            entry_free(&e);
            continue;
        }
        orig = xstrdup(e.path);
        if (nsubsts() && !apply_substs(&e.path, 0)) {
            read_entry_data(&e, -1);
            entry_free(&e);
            free(orig);
            continue;
        }
        if (e.type == T_LNK && e.link && nsubsts())
            apply_substs(&e.link, 1);
        if (e.type == T_HARD && e.link) {
            char *m = map_link_name(e.link);
            if (m) {
                free(e.link);
                e.link = m;
            } else if (nsubsts())
                apply_substs(&e.link, 0);
        }
        if ((opt_i || (opt_r && invalid_rename(&e))) && !interactive_rename(&e.path)) {
            read_entry_data(&e, -1);
            entry_free(&e);
            free(orig);
            continue;
        }
        if ((e.fmt == F_CPIO || e.fmt == F_NEWC) && e.nlink > 1 && e.type == T_REG) {
            const char *first = cpio_link(&e);
            if (first) {
                e.type = T_HARD;
                e.link = xstrdup(first);
            }
        }
        if (!opt_r) {
            list_entry(&e);
            read_entry_data(&e, -1);
        } else {
            if (opt_v) {
                fprintf(stderr, "%s", e.path);
                fflush(stderr);
            }
            extract_entry(&e, -1, e.path);
            note_renamed(orig, e.path);
            if (opt_v) {
                fputc('\n', stderr);
                fflush(stderr);
            }
        }
        free(orig);
        entry_free(&e);
        if (opt_n && npatterns && all_patterns_done())
            break;
    }
    finish_directories();
    if (!opt_c)
        report_unmatched();
}

/* ---------- copy mode ---------- */

static char *dest_dir;
static dev_t dest_dev;
static ino_t dest_ino;

static void copy_one(Entry *e)
{
    Buf dp = { 0 };
    const char *first;
    if (e->type == T_DIR && e->st.st_dev == dest_dev && e->st.st_ino == dest_ino) {
        warnx_("%s: the destination directory is not copied into itself", e->path);
        walk_prune = 1;
        return;
    }
    if (opt_u) {
        /* checked against the destination file below */
    }
    if (nsubsts()) {
        if (!apply_substs(&e->path, 0))
            return;
        if (e->type == T_LNK && e->link)
            apply_substs(&e->link, 1);
    }
    if (opt_i && !interactive_rename(&e->path))
        return;
    buf_adds(&dp, dest_dir);
    if (dp.len && dp.s[dp.len - 1] != '/' && e->path[0] != '/')
        buf_addc(&dp, '/');
    buf_adds(&dp, e->path);
    if (opt_v) {
        fprintf(stderr, "%s", dp.s);
        fflush(stderr);
    }
    if (e->type != T_DIR && e->st.st_nlink > 1 &&
        (first = link_lookup(e->st.st_dev, e->st.st_ino, dp.s))) {
        Entry h = *e;
        h.type = T_HARD;
        h.link = (char *)first;
        h.size = 0;
        extract_entry(&h, 0, dp.s);
    } else if (opt_l && e->type != T_DIR) {
        struct stat st;
        int ok = 1;
        if (lstat(dp.s, &st) == 0) {
            if (opt_k || (opt_u && !newer_than_existing(e, dp.s)))
                ok = 0;
            else if (!S_ISDIR(st.st_mode))
                unlink(dp.s);
        }
        if (ok && link(e->src, dp.s) < 0) {
            /* make the link impossible: copy instead */
            int fd = e->type == T_REG ? open(e->src, O_RDONLY) : -1;
            if (e->type == T_REG && fd < 0)
                warn_("%s", e->src);
            else {
                extract_entry(e, fd >= 0 ? fd : 0, dp.s);
                if (fd >= 0)
                    close(fd);
            }
        }
    } else if (e->type == T_REG) {
        int fd = open(e->src, O_RDONLY);
        if (fd < 0)
            warn_("%s", e->src);
        else {
            extract_entry(e, fd, dp.s);
            if (opt_t) {
                struct timespec ts[2];
                ts[0] = e->st.st_atim;
                ts[1].tv_sec = 0;
                ts[1].tv_nsec = UTIME_OMIT;
                futimens(fd, ts);
            }
            close(fd);
        }
    } else
        extract_entry(e, 0, dp.s);
    if (opt_v) {
        fputc('\n', stderr);
        fflush(stderr);
    }
    buf_free(&dp);
}

static void copy_mode(int argc, char **argv)
{
    struct stat st;
    if (argc < 1)
        usage();
    dest_dir = argv[argc - 1];
    if (stat(dest_dir, &st) < 0) {
        warn_("%s", dest_dir);
        exit(1);
    }
    if (!S_ISDIR(st.st_mode))
        errx_("%s: not a directory", dest_dir);
    if (access(dest_dir, W_OK) < 0) {
        warn_("%s", dest_dir);
        exit(1);
    }
    dest_dev = st.st_dev;
    dest_ino = st.st_ino;
    arc_fmt = F_PAX;
    if (argc > 1)
        for (int i = 0; i < argc - 1; i++)
            walk_operand(argv[i], copy_one);
    else {
        char *l;
        while ((l = read_name_line()))
            walk_operand(l, copy_one);
    }
    finish_directories();
}

int main(int argc, char **argv)
{
    int c;
    setlocale(LC_ALL, "");
    is_root = geteuid() == 0;
    while ((c = getopt(argc, argv, ":ab:cdf:HikLlno:p:rs:tuvwx:X")) != -1) {
        switch (c) {
        case 'a':
            opt_a = 1;
            break;
        case 'b':
            opt_b = parse_blocksize(optarg);
            break;
        case 'c':
            opt_c = 1;
            opt_n = 0;
            break;
        case 'd':
            opt_d = 1;
            break;
        case 'f':
            opt_f = optarg;
            break;
        case 'H':
            opt_H = 1;
            opt_L = 0;
            break;
        case 'L':
            opt_L = 1;
            opt_H = 0;
            break;
        case 'i':
            opt_i = 1;
            break;
        case 'k':
            opt_k = 1;
            break;
        case 'l':
            opt_l = 1;
            break;
        case 'n':
            opt_n = 1;
            opt_c = 0;
            break;
        case 'o':
            parse_o(optarg);
            break;
        case 'p':
            parse_p(optarg);
            break;
        case 'r':
            opt_r = 1;
            break;
        case 's':
            add_subst(optarg);
            break;
        case 't':
            opt_t = 1;
            break;
        case 'u':
            opt_u = 1;
            break;
        case 'v':
            opt_v = 1;
            break;
        case 'w':
            opt_w = 1;
            break;
        case 'x':
            if (strcmp(optarg, "ustar") == 0)
                opt_fmt = F_USTAR;
            else if (strcmp(optarg, "pax") == 0)
                opt_fmt = F_PAX;
            else if (strcmp(optarg, "cpio") == 0)
                opt_fmt = F_CPIO;
            else
                errx_("-x %s: unknown archive format", optarg);
            break;
        case 'X':
            opt_X = 1;
            break;
        case ':':
            warnx_("option requires an argument -- %c", optopt);
            usage();
            break;
        default:
            warnx_("illegal option -- %c", optopt);
            usage();
        }
    }
    argc -= optind;
    argv += optind;
    if (opt_r && opt_w)
        copy_mode(argc, argv);
    else if (opt_w)
        write_mode(argc, argv);
    else
        read_mode(argc, argv);
    return exit_status;
}
