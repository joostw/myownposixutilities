/*
 * ls — list directory contents  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis:
 *   [XSI] ls [-ikqrs] [-gln o] [-A|-a] [-C|-m|-x|-1] \
 *            [-F|-p] [-H|-L] [-R|-d] [-S|-f|-t] [-c|-u] [file...]
 *
 * Options:
 *   -A  All entries except . and ..
 *   -a  All entries including . and ..
 *   -C  Multi-column output, sorted down columns (default on tty)
 *   -c  Use ctime for sort/display instead of mtime
 *   -d  Do not descend into directories
 *   -F  Append type indicator (/ * | = @)
 *   -f  Unsorted; implies -a; ignores -r, -S, -t
 *   -g  [XSI] Long format without owner
 *   -H  Follow command-line symlinks
 *   -i  Show inode number
 *   -k  1024-byte block size (for -s and the total line)
 *   -L  Follow all symlinks
 *   -l  Long format
 *   -m  Stream (comma-separated) output
 *   -n  Long format with numeric UID/GID
 *   -o  [XSI] Long format without group
 *   -p  Append / after directory names
 *   -q  Replace non-printable chars with '?'
 *   -R  Recursive
 *   -r  Reverse sort order
 *   -S  Sort by size (descending), then name
 *   -s  Show block count per file
 *   -t  Sort by modification time (most recent first), then name
 *   -u  Use atime for sort/display instead of mtime
 *   -x  Multi-column output, sorted across rows
 *   -1  One entry per line
 *
 * Exit status: 0 = success, >0 = error.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <locale.h>
#include <pwd.h>
#include <stdbool.h>
#include <wchar.h>
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ================================================================ */
/*  Option state                                                      */
/* ================================================================ */

static bool opt_A, opt_a, opt_d, opt_F, opt_f,
            opt_g, opt_H, opt_i, opt_k, opt_L,
            opt_n, opt_o, opt_p, opt_q, opt_R, opt_r,
            opt_s;

static bool long_fmt;   /* enabled by -l, -g, -n, -o */

typedef enum { SORT_NAME, SORT_TIME, SORT_SIZE, SORT_NONE } sort_t;
typedef enum { FMT_ONE, FMT_LONG, FMT_DOWN, FMT_ACROSS, FMT_STREAM } fmt_t;
typedef enum { TIME_MTIME, TIME_CTIME, TIME_ATIME } time_sel_t;

static sort_t     sort_mode  = SORT_NAME;
static fmt_t      out_fmt    = FMT_ONE;
static time_sel_t time_sel   = TIME_MTIME;
static int        term_width = 80;
static bool       had_error;

/* ================================================================ */
/*  Entry                                                             */
/* ================================================================ */

typedef struct {
    char       *name;     /* basename (allocated) */
    char       *path;     /* full path for lstat / readlink (allocated) */
    struct stat st;       /* always lstat() */
    struct stat tgt;      /* stat() of symlink target */
    bool        tgt_ok;   /* stat() of target succeeded */
    bool        use_tgt;  /* use tgt for display info (-L or -H+cmdline) */
} entry_t;

static const struct stat *estat(const entry_t *e)
{
    return (e->use_tgt && e->tgt_ok) ? &e->tgt : &e->st;
}

static time_t etime(const entry_t *e)
{
    const struct stat *s = estat(e);
    switch (time_sel) {
    case TIME_CTIME: return s->st_ctime;
    case TIME_ATIME: return s->st_atime;
    default:         return s->st_mtime;
    }
}

/* ================================================================ */
/*  Infinite-loop detection for -R                                    */
/* ================================================================ */

typedef struct { dev_t dev; ino_t ino; } devino_t;
static devino_t *visited;
static size_t    n_visited;
static size_t    cap_visited;

static bool mark_visited(dev_t dev, ino_t ino)
{
    for (size_t i = 0; i < n_visited; i++)
        if (visited[i].dev == dev && visited[i].ino == ino)
            return false;
    if (n_visited == cap_visited) {
        size_t nc  = cap_visited ? cap_visited * 2 : 16;
        devino_t *t = realloc(visited, nc * sizeof *t);
        if (!t) return true;
        visited     = t;
        cap_visited = nc;
    }
    visited[n_visited++] = (devino_t){dev, ino};
    return true;
}

static void unmark_visited(dev_t dev, ino_t ino)
{
    for (size_t i = 0; i < n_visited; i++) {
        if (visited[i].dev == dev && visited[i].ino == ino) {
            visited[i] = visited[--n_visited];
            return;
        }
    }
}

/* ================================================================ */
/*  Helpers                                                           */
/* ================================================================ */

static int get_term_width(void)
{
    const char *cols = getenv("COLUMNS");
    if (cols && *cols) {
        char *end;
        long  w = strtol(cols, &end, 10);
        if (*end == '\0' && w > 0 && w <= 32767)
            return (int)w;
    }
#ifdef TIOCGWINSZ
    {
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
            return ws.ws_col;
    }
#endif
    return 80;
}

/*
 * Check for embedded newlines and warn (Issue 8 FUTURE DIRECTIONS:
 * encouraged to treat as an error when newline is a terminator/separator).
 */
static void check_newline_in_name(const char *name)
{
    if (!opt_q && strchr(name, '\n')) {
        fprintf(stderr, "ls: filename contains embedded newline: ");
        for (const unsigned char *p = (const unsigned char *)name; *p; p++)
            fputc(*p == '\n' ? '?' : *p, stderr);
        fputc('\n', stderr);
        had_error = true;
    }
}

static void write_name(const char *name)
{
    if (!opt_q) { fputs(name, stdout); return; }
    /*
     * Replace non-printable characters with '?', multibyte-aware.
     * Invalid byte sequences are each replaced by a single '?'.
     */
    const char *p        = name;
    size_t      left     = strlen(name);
    mbstate_t   st;
    memset(&st, 0, sizeof st);
    while (left > 0) {
        wchar_t wc;
        size_t  n = mbrtowc(&wc, p, left, &st);
        if (n == (size_t)-1 || n == (size_t)-2) {
            /* Invalid or incomplete sequence: consume one byte as '?' */
            putchar('?');
            p++; left--;
            memset(&st, 0, sizeof st);
        } else if (n == 0) {
            break; /* NUL — shouldn't appear in filenames */
        } else {
            if (iswprint((wint_t)wc))
                fwrite(p, 1, n, stdout);
            else
                putchar('?');
            p += n; left -= n;
        }
    }
}

static void mode_str(const struct stat *st, char buf[11])
{
    char t;
    switch (st->st_mode & S_IFMT) {
    case S_IFDIR:  t = 'd'; break;
    case S_IFBLK:  t = 'b'; break;
    case S_IFCHR:  t = 'c'; break;
    case S_IFLNK:  t = 'l'; break;
    case S_IFIFO:  t = 'p'; break;
    case S_IFSOCK: t = 's'; break;
    default:       t = '-'; break;
    }
    buf[0] = t;
    buf[1] = (st->st_mode & S_IRUSR) ? 'r' : '-';
    buf[2] = (st->st_mode & S_IWUSR) ? 'w' : '-';
    buf[3] = (st->st_mode & S_ISUID)
           ? ((st->st_mode & S_IXUSR) ? 's' : 'S')
           : ((st->st_mode & S_IXUSR) ? 'x' : '-');
    buf[4] = (st->st_mode & S_IRGRP) ? 'r' : '-';
    buf[5] = (st->st_mode & S_IWGRP) ? 'w' : '-';
    buf[6] = (st->st_mode & S_ISGID)
           ? ((st->st_mode & S_IXGRP) ? 's' : 'S')
           : ((st->st_mode & S_IXGRP) ? 'x' : '-');
    buf[7] = (st->st_mode & S_IROTH) ? 'r' : '-';
    buf[8] = (st->st_mode & S_IWOTH) ? 'w' : '-';
    buf[9] = (st->st_mode & S_ISVTX)
           ? ((st->st_mode & S_IXOTH) ? 't' : 'T')
           : ((st->st_mode & S_IXOTH) ? 'x' : '-');
    buf[10] = '\0';
}

static void fmt_time(time_t t, char buf[32])
{
    time_t     now = time(NULL);
    struct tm *tm  = localtime(&t);
    if (!tm) { strcpy(buf, "???"); return; }
    /* "recent" = not in the future and within the last ~6 months */
    if (t <= now && (now - t) < 15778800L)
        strftime(buf, 32, "%b %e %H:%M", tm);
    else
        strftime(buf, 32, "%b %e  %Y", tm);   /* two spaces before year */
}

static const char *owner_str(uid_t uid, char *buf, size_t n)
{
    if (!opt_n) {
        struct passwd *pw = getpwuid(uid);
        if (pw) return pw->pw_name;
    }
    snprintf(buf, n, "%u", (unsigned)uid);
    return buf;
}

static const char *group_str(gid_t gid, char *buf, size_t n)
{
    if (!opt_n) {
        struct group *gr = getgrgid(gid);
        if (gr) return gr->gr_name;
    }
    snprintf(buf, n, "%u", (unsigned)gid);
    return buf;
}

/*
 * Returns the -F indicator character for a non-symlink file,
 * or '\0' if none applies.  For symlinks use '@' directly.
 */
static char file_indicator(const struct stat *s)
{
    switch (s->st_mode & S_IFMT) {
    case S_IFDIR:  return '/';
    case S_IFIFO:  return '|';
    case S_IFSOCK: return '=';
    default:
        if (s->st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)) return '*';
        return '\0';
    }
}

/* Blocks in display units (512-byte or 1024-byte) from st_blocks. */
static unsigned long display_blks(blkcnt_t b)
{
    /* st_blocks is in 512-byte units; -k wants 1024-byte units. */
    if (opt_k) return (unsigned long)(b + 1) / 2;
    return (unsigned long)b;
}

/* Display width of an entry (name + optional inode/blocks/indicator). */
static size_t entry_width(const entry_t *e)
{
    size_t w = strlen(e->name);
    if (opt_i) w += (size_t)snprintf(NULL, 0, "%llu ",
                                     (unsigned long long)e->st.st_ino);
    if (opt_s) w += (size_t)snprintf(NULL, 0, "%lu ",
                                     display_blks(estat(e)->st_blocks));
    if (opt_F) {
        bool is_lnk = S_ISLNK(e->st.st_mode) && !opt_L;
        w += (is_lnk || file_indicator(estat(e))) ? 1 : 0;
    } else if (opt_p && S_ISDIR(estat(e)->st_mode)) {
        w++;
    }
    return w;
}

/* Column widths for long-format output — computed once per directory listing. */
static int g_nlink_w = 1;
static int g_owner_w = 1;
static int g_group_w = 1;
static int g_size_w  = 1;

static void compute_long_widths(const entry_t *list, size_t n)
{
    g_nlink_w = 1; g_owner_w = 1; g_group_w = 1; g_size_w = 1;
    for (size_t i = 0; i < n; i++) {
        const struct stat *s = estat(&list[i]);
        char buf[32];
        int nw = snprintf(NULL, 0, "%u",  (unsigned)s->st_nlink);
        int ow = (int)strlen(owner_str(s->st_uid, buf, sizeof buf));
        int gw = (int)strlen(group_str(s->st_gid, buf, sizeof buf));
        int sw = snprintf(NULL, 0, "%llu", (unsigned long long)s->st_size);
        if (nw > g_nlink_w) g_nlink_w = nw;
        if (ow > g_owner_w) g_owner_w = ow;
        if (gw > g_group_w) g_group_w = gw;
        if (sw > g_size_w)  g_size_w  = sw;
    }
}

/* ================================================================ */
/*  Sorting                                                           */
/* ================================================================ */

static int cmp_name(const char *a, const char *b)
{
    int r = strcoll(a, b);
    if (r == 0) r = strcmp(a, b); /* byte-by-byte tiebreak per SUSv5 Issue 8 */
    return r;
}

/* For operand-level sorting: always by name (collating sequence), with -r. */
static int cmp_entries_by_name(const void *a, const void *b)
{
    const entry_t *ea = a, *eb = b;
    int r = cmp_name(ea->name, eb->name);
    return opt_r ? -r : r;
}

static int cmp_entries(const void *a, const void *b)
{
    const entry_t *ea = a, *eb = b;
    int r = 0;

    if (sort_mode == SORT_SIZE) {
        off_t sa = estat(ea)->st_size, sb = estat(eb)->st_size;
        r = (sb > sa) - (sa > sb); /* descending */
    } else if (sort_mode == SORT_TIME) {
        time_t ta = etime(ea), tb = etime(eb);
        r = (tb > ta) - (ta > tb); /* most recent first */
    }

    if (r == 0) r = cmp_name(ea->name, eb->name);
    return opt_r ? -r : r;
}

/* ================================================================ */
/*  Output                                                            */
/* ================================================================ */

static void print_inode_blocks(const entry_t *e)
{
    if (opt_i) printf("%llu ", (unsigned long long)e->st.st_ino);
    if (opt_s) printf("%lu ",  display_blks(estat(e)->st_blocks));
}

static void print_long_entry(const entry_t *e)
{
    const struct stat *s = estat(e);
    char mode[11], tbuf[32], ownbuf[32], grpbuf[32];
    mode_str(s, mode);
    fmt_time(etime(e), tbuf);

    print_inode_blocks(e);

    /* mode nlink [owner] [group] size|device date name */
    fputs(mode, stdout);
    printf(" %*u", g_nlink_w, (unsigned)s->st_nlink);
    if (!opt_g) printf(" %-*s", g_owner_w, owner_str(s->st_uid, ownbuf, sizeof ownbuf));
    if (!opt_o) printf(" %-*s", g_group_w, group_str(s->st_gid, grpbuf, sizeof grpbuf));

    if (S_ISCHR(s->st_mode) || S_ISBLK(s->st_mode))
        printf(" %3u, %3u", (unsigned)major(s->st_rdev),
                             (unsigned)minor(s->st_rdev));
    else
        printf(" %*llu", g_size_w, (unsigned long long)s->st_size);

    printf(" %s ", tbuf);
    check_newline_in_name(e->name);
    write_name(e->name);

    bool is_lnk = S_ISLNK(e->st.st_mode) && !opt_L;
    if (is_lnk) {
        /* link type indicator (@) immediately follows the link name */
        if (opt_F) putchar('@');

        char target[4096];
        ssize_t tlen = readlink(e->path, target, sizeof target - 1);
        if (tlen >= 0) {
            target[tlen] = '\0';
            printf(" -> ");
            fputs(target, stdout);
            /* file type indicator for the resolved target */
            if ((opt_F || opt_p) && e->tgt_ok) {
                char fi = opt_F ? file_indicator(&e->tgt)
                                : (S_ISDIR(e->tgt.st_mode) ? '/' : '\0');
                if (fi) putchar(fi);
            }
        }
    } else {
        if (opt_F) {
            char fi = file_indicator(s);
            if (fi) putchar(fi);
        } else if (opt_p && S_ISDIR(s->st_mode)) {
            putchar('/');
        }
    }
    putchar('\n');
}

static void print_short(const entry_t *e)
{
    print_inode_blocks(e);
    check_newline_in_name(e->name);
    write_name(e->name);
    const struct stat *s = estat(e);
    bool is_lnk = S_ISLNK(e->st.st_mode) && !opt_L;
    char ind = '\0';
    if (opt_F)
        ind = is_lnk ? '@' : file_indicator(s);
    else if (opt_p && S_ISDIR(s->st_mode))
        ind = '/';
    if (ind) putchar(ind);
}

static void print_one_per_line(const entry_t *list, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        print_short(&list[i]);
        putchar('\n');
    }
}

static void print_long_list(const entry_t *list, size_t n)
{
    compute_long_widths(list, n);
    for (size_t i = 0; i < n; i++)
        print_long_entry(&list[i]);
}

static void print_stream(const entry_t *list, size_t n)
{
    if (n == 0) return;
    int pos = 0;
    for (size_t i = 0; i < n; i++) {
        bool last = (i == n - 1);
        size_t w  = entry_width(&list[i]);

        print_short(&list[i]);
        pos += (int)w;

        if (last) {
            putchar('\n');
        } else {
            putchar(',');
            pos++;
            /* Decide separator: space or newline based on whether the next
               entry fits on the current line. */
            size_t nw = entry_width(&list[i + 1]);
            if (pos + 1 + (int)nw > term_width) {
                putchar('\n');
                pos = 0;
            } else {
                putchar(' ');
                pos++;
            }
        }
    }
}

static void print_columns(const entry_t *list, size_t n, bool across)
{
    if (n == 0) return;

    size_t max_w = 0;
    for (size_t i = 0; i < n; i++) {
        size_t w = entry_width(&list[i]);
        if (w > max_w) max_w = w;
    }

    int col_width = (int)max_w + 2; /* 2-space separator */
    int ncols     = term_width / col_width;
    if (ncols < 1) ncols = 1;
    size_t nrows  = (n + (size_t)ncols - 1) / (size_t)ncols;

    for (size_t row = 0; row < nrows; row++) {
        for (int col = 0; col < ncols; col++) {
            size_t idx = across ? row * (size_t)ncols + (size_t)col
                                : (size_t)col * nrows + row;
            if (idx >= n) break;

            /* Is this the last entry in the row? */
            size_t next = across ? idx + 1
                                 : (size_t)(col + 1) * nrows + row;
            bool last_in_row = ((col == ncols - 1) || (next >= n));

            size_t w = entry_width(&list[idx]);
            print_short(&list[idx]);

            if (last_in_row) {
                putchar('\n');
            } else {
                int pad = col_width - (int)w;
                if (pad > 0) printf("%*s", pad, "");
            }
        }
    }
}

/* Print total blocks line for -l/-n/-g/-o/-s directory listings. */
static void print_total(const entry_t *list, size_t n)
{
    if (!long_fmt && !opt_s) return;
    blkcnt_t total = 0;
    for (size_t i = 0; i < n; i++)
        total += estat(&list[i])->st_blocks;
    printf("total %lu\n", display_blks(total));
}

static void output_entries(const entry_t *list, size_t n)
{
    switch (out_fmt) {
    case FMT_LONG:   print_long_list(list, n); break;
    case FMT_DOWN:   print_columns(list, n, false); break;
    case FMT_ACROSS: print_columns(list, n, true);  break;
    case FMT_STREAM: print_stream(list, n); break;
    case FMT_ONE:    print_one_per_line(list, n); break;
    }
}

/* ================================================================ */
/*  Directory reading                                                 */
/* ================================================================ */

static int read_dir(const char *path, entry_t **out, size_t *out_n)
{
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "ls: cannot open directory '%s': %s\n",
                path, strerror(errno));
        had_error = true;
        return -1;
    }

    entry_t *list = NULL;
    size_t count = 0, cap = 0;

    errno = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;

        /* Dot-file filtering */
        if (nm[0] == '.') {
            if (!opt_a && !opt_f) {
                if (!opt_A) continue;                       /* skip all dot-files */
                if (nm[1] == '\0') continue;               /* skip "." */
                if (nm[1] == '.' && nm[2] == '\0') continue; /* skip ".." */
            }
        }

        /* Build full path */
        size_t plen = strlen(path);
        size_t nlen = strlen(nm);
        char  *full = malloc(plen + 1 + nlen + 1);
        if (!full) { perror("ls"); had_error = true; break; }
        memcpy(full, path, plen);
        full[plen] = '/';
        memcpy(full + plen + 1, nm, nlen + 1);

        /* Grow list */
        if (count == cap) {
            size_t nc = cap ? cap * 2 : 32;
            entry_t *tmp = realloc(list, nc * sizeof *tmp);
            if (!tmp) { perror("ls"); free(full); had_error = true; break; }
            list = tmp;
            cap  = nc;
        }

        entry_t *e = &list[count];
        e->path    = full;
        e->tgt_ok  = false;
        e->use_tgt = false;

        if ((e->name = strdup(nm)) == NULL) {
            perror("ls"); free(full); had_error = true; break;
        }

        if (lstat(full, &e->st) != 0) {
            fprintf(stderr, "ls: cannot stat '%s': %s\n", full, strerror(errno));
            had_error = true;
            free(e->name); free(e->path);
            continue;
        }

        /* -L: follow all symlinks */
        if (opt_L && S_ISLNK(e->st.st_mode)) {
            if (stat(full, &e->tgt) == 0) {
                e->tgt_ok  = true;
                e->use_tgt = true;
            }
        } else if (S_ISLNK(e->st.st_mode)) {
            /* Stat the target for -F indicators, but don't use it for display */
            stat(full, &e->tgt);
            e->tgt_ok = true;
        }

        count++;
        errno = 0;
    }

    if (errno != 0) {
        fprintf(stderr, "ls: reading directory '%s': %s\n",
                path, strerror(errno));
        had_error = true;
    }
    closedir(d);

    *out   = list;
    *out_n = count;
    return 0;
}

/* ================================================================ */
/*  Listing                                                           */
/* ================================================================ */

/*
 * State for directory headers.
 * need_nl_before_header: true when we must emit a blank line before
 *   the next "\nname:\n" header (suppressed for the very first header).
 * show_headers: set when multiple directories or mix of files+dirs.
 */
static bool need_nl_before_header;
static bool show_headers;

/* Forward declaration */
static void list_dir(const char *path, const char *display);

static void list_dir(const char *path, const char *display)
{
    struct stat dst;
    if (lstat(path, &dst) == 0) {
        if (!mark_visited(dst.st_dev, dst.st_ino)) {
            fprintf(stderr, "ls: %s: not listing already-visited directory\n",
                    path);
            return;
        }
    }

    if (show_headers) {
        if (need_nl_before_header) putchar('\n');
        printf("%s:\n", display);
    }
    need_nl_before_header = true;

    entry_t *list = NULL;
    size_t   n    = 0;
    if (read_dir(path, &list, &n) < 0) {
        if (dst.st_dev || dst.st_ino) /* only if lstat succeeded */
            unmark_visited(dst.st_dev, dst.st_ino);
        return;
    }

    if (sort_mode != SORT_NONE)
        qsort(list, n, sizeof *list, cmp_entries);

    print_total(list, n);
    output_entries(list, n);

    /* Recurse for -R */
    if (opt_R) {
        for (size_t i = 0; i < n; i++) {
            const entry_t *e = &list[i];
            /* Skip non-directories and dot/dotdot */
            if (!S_ISDIR(estat(e)->st_mode)) continue;
            if (strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0) continue;
            /* Per spec: don't recurse through symlinks to dirs unless -L */
            if (S_ISLNK(e->st.st_mode) && !opt_L) continue;
            /* show_headers must be true during recursion */
            show_headers = true;
            list_dir(e->path, e->path);
        }
    }

    for (size_t i = 0; i < n; i++) {
        free(list[i].name);
        free(list[i].path);
    }
    free(list);

    if (dst.st_dev || dst.st_ino)
        unmark_visited(dst.st_dev, dst.st_ino);
}

/* ================================================================ */
/*  main                                                              */
/* ================================================================ */

static void usage(void)
{
    fprintf(stderr,
        "usage: ls [-AaCcdfFgHiKLlmnopqrRsStux1] [file...]\n");
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    int opt;
    while ((opt = getopt(argc, argv, "AaCcdfFgHikLlmnopqrRsStux1")) != -1) {
        switch (opt) {
        case 'A': opt_A = true; break;
        case 'a': opt_a = true; break;
        case 'C': out_fmt = FMT_DOWN;   long_fmt = false; break;
        case 'c': time_sel = TIME_CTIME; break;
        case 'd': opt_d = true; break;
        case 'F': opt_F = true; break;
        case 'f': opt_f = true; break;
        case 'g':
            opt_g    = true;
            long_fmt = true;
            out_fmt  = FMT_LONG;
            break;
        case 'H': opt_H = true; break;
        case 'i': opt_i = true; break;
        case 'k': opt_k = true; break;
        case 'L': opt_L = true; break;
        case 'l':
            long_fmt = true;
            out_fmt  = FMT_LONG;
            break;
        case 'm': out_fmt = FMT_STREAM; long_fmt = false; break;
        case 'n':
            opt_n    = true;
            long_fmt = true;
            out_fmt  = FMT_LONG;
            break;
        case 'o':
            opt_o    = true;
            long_fmt = true;
            out_fmt  = FMT_LONG;
            break;
        case 'p': opt_p = true; break;
        case 'q': opt_q = true; break;
        case 'R': opt_R = true; break;
        case 'r': opt_r = true; break;
        case 'S': sort_mode = SORT_SIZE; break;
        case 's': opt_s = true; break;
        case 't': sort_mode = SORT_TIME; break;
        case 'u': time_sel = TIME_ATIME; break;
        case 'x': out_fmt = FMT_ACROSS; long_fmt = false; break;
        case '1': out_fmt = FMT_ONE; break;
        default:
            usage();
            return 2;
        }
    }

    /* -f: unsorted directory order; implies -a; -r/-S/-t shall be ignored. */
    if (opt_f) {
        opt_a     = true;
        sort_mode = SORT_NONE;
        opt_r     = false;
    }

    /* Long-format options disable -C/-m/-x (last-wins handled in parsing).
       If a long-format option set out_fmt = FMT_LONG, it overrides -C/-m/-x
       only if it was specified after them — which the switch already does. */

    /* Default output format for terminals: multi-column (-C). */
    if (out_fmt == FMT_ONE && !long_fmt && isatty(STDOUT_FILENO))
        out_fmt = FMT_DOWN;

    if (out_fmt == FMT_DOWN || out_fmt == FMT_ACROSS || out_fmt == FMT_STREAM)
        term_width = get_term_width();

    int     n_files = argc - optind;
    char  **files   = argv + optind;

    /* No operands: list "." */
    if (n_files == 0) {
        show_headers           = opt_R;
        need_nl_before_header  = false;
        list_dir(".", ".");
        return had_error ? 1 : 0;
    }

    /*
     * Multiple operands: stat each, separate into non-dirs and dirs,
     * output non-dirs first (sorted by name), then dirs (sorted by name).
     */
    typedef struct {
        char       *name;
        struct stat  st;
        struct stat  tgt;
        bool         tgt_ok;
        bool         is_dir;  /* true → descend; false → show as file */
        bool         err;
    } operand_t;

    operand_t *ops = calloc((size_t)n_files, sizeof *ops);
    if (!ops) { perror("ls"); return 2; }

    int n_nondirs = 0, n_dirs = 0;
    for (int i = 0; i < n_files; i++) {
        ops[i].name = files[i];
        if (lstat(files[i], &ops[i].st) != 0) {
            fprintf(stderr, "ls: cannot access '%s': %s\n",
                    files[i], strerror(errno));
            had_error = true;
            ops[i].err = true;
            continue;
        }
        /* -H: follow command-line symlinks */
        if ((opt_L || opt_H) && S_ISLNK(ops[i].st.st_mode)) {
            if (stat(files[i], &ops[i].tgt) == 0)
                ops[i].tgt_ok = true;
        } else if (S_ISLNK(ops[i].st.st_mode)) {
            /* Stat target for -F/-p indicator but don't use for display */
            ops[i].tgt_ok = (stat(files[i], &ops[i].tgt) == 0);
        }

        /*
         * Determine whether this operand should be descended into.
         * Per spec: follow a symlink-to-dir when -H or -L is set, OR when
         * none of -d/-F/-l is active (the default case).  In all other cases
         * (i.e. -d, -F, or -l without -H/-L) show the symlink as a file.
         */
        bool follow_for_descend = opt_L || opt_H
                                || (!opt_d && !opt_F && !long_fmt);
        struct stat *eff = (ops[i].tgt_ok && follow_for_descend
                            && S_ISLNK(ops[i].st.st_mode))
                         ? &ops[i].tgt : &ops[i].st;
        ops[i].is_dir = S_ISDIR(eff->st_mode) && !opt_d;

        if (ops[i].is_dir) n_dirs++;
        else                n_nondirs++;
    }

    /* Show headers if more than one directory, or dirs + non-dirs, or -R. */
    show_headers          = (n_dirs > 1) || (n_dirs > 0 && n_nondirs > 0) || opt_R;
    need_nl_before_header = false;

    /* Output non-directory operands first, sorted by name. */
    if (n_nondirs > 0) {
        entry_t *nd = calloc((size_t)n_nondirs, sizeof *nd);
        if (!nd) { perror("ls"); free(ops); return 2; }
        int k = 0;
        for (int i = 0; i < n_files; i++) {
            if (ops[i].is_dir || ops[i].err) continue;
            nd[k].name    = strdup(ops[i].name);
            nd[k].path    = strdup(ops[i].name);
            nd[k].st      = ops[i].st;
            nd[k].tgt     = ops[i].tgt;
            nd[k].tgt_ok  = ops[i].tgt_ok;
            nd[k].use_tgt = ops[i].tgt_ok && (opt_L || opt_H);
            if (!nd[k].name || !nd[k].path) { perror("ls"); break; }
            k++;
        }
        n_nondirs = k;
        /* Spec: non-dir operands sorted by collating sequence, not sort_mode. */
        qsort(nd, (size_t)n_nondirs, sizeof *nd, cmp_entries_by_name);
        /* No "total" line for plain file operands */
        output_entries(nd, (size_t)n_nondirs);
        need_nl_before_header = (n_dirs > 0);
        for (int i = 0; i < n_nondirs; i++) {
            free(nd[i].name);
            free(nd[i].path);
        }
        free(nd);
    }

    /* Output directory operands, sorted by name. */
    if (n_dirs > 0) {
        /* Collect indices of directory operands */
        int *di = malloc((size_t)n_dirs * sizeof *di);
        if (!di) { perror("ls"); free(ops); return 2; }
        int k = 0;
        for (int i = 0; i < n_files; i++)
            if (ops[i].is_dir) di[k++] = i;

        /* Sort by name (collating sequence) — insertion sort is fine for n_dirs */
        for (int i = 1; i < n_dirs; i++) {
            int key = di[i], j = i - 1;
            while (j >= 0 && cmp_name(ops[di[j]].name, ops[key].name) > 0) {
                di[j + 1] = di[j];
                j--;
            }
            di[j + 1] = key;
        }
        /* Reverse if -r is set */
        if (opt_r) {
            for (int i = 0, j = n_dirs - 1; i < j; i++, j--) {
                int tmp = di[i]; di[i] = di[j]; di[j] = tmp;
            }
        }

        for (int i = 0; i < n_dirs; i++)
            list_dir(ops[di[i]].name, ops[di[i]].name);

        free(di);
    }

    free(ops);
    free(visited);
    return had_error ? 1 : 0;
}
