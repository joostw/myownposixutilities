/*
 * ln.c - ln: link files (POSIX.1-2024, XCU ln)
 *
 * Synopsis:
 *   ln [-fs] [-L|-P] source_file target_file
 *   ln [-fs] [-L|-P] source_file... target_dir
 *
 * Without -L or -P, a hard link to a symbolic link links the symbolic
 * link itself (-P; the default is implementation-defined).
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool opt_f, opt_s, opt_L, opt_P;
static int had_error;

/* split a pathname into its directory and its last component (trailing
   slashes removed); the results are malloc'ed */
static void split_path(const char *path, char **dir, char **last)
{
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/')
        n--;
    size_t i = n;
    while (i > 0 && path[i - 1] != '/')
        i--;
    *last = strndup(path + i, n - i);
    if (i == 0)
        *dir = strdup(".");
    else {
        size_t d = i;
        while (d > 1 && path[d - 1] == '/')
            d--;
        *dir = strndup(path, d);
    }
    if (!*dir || !*last) {
        fprintf(stderr, "ln: out of memory\n");
        exit(1);
    }
}

/* do two pathnames name the same directory entry? */
static bool same_entry(const char *a, const char *b)
{
    char *da, *la, *db, *lb;
    struct stat sa, sb;
    bool same = false;
    split_path(a, &da, &la);
    split_path(b, &db, &lb);
    if (strcmp(la, lb) == 0 && stat(da, &sa) == 0 && stat(db, &sb) == 0 &&
        sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino)
        same = true;
    free(da);
    free(la);
    free(db);
    free(lb);
    return same;
}

static int do_link(const char *src, const char *dst)
{
    struct stat dst_lst;
    bool dst_exists = (lstat(dst, &dst_lst) == 0);

    if (dst_exists) {
        if (!opt_f) {
            fprintf(stderr, "ln: %s: already exists\n", dst);
            return 1;
        }
        /* The destination must not be the same directory entry as the
         * source (the same name in the same directory: not merely the same
         * file, so "ln -sf t link" replaces a link that points to t, and
         * "ln -f a b" replaces another hard link b to a).  This is checked
         * with -s too, so that "ln -sf foo foo" does not remove foo. */
        if (same_entry(src, dst)) {
            fprintf(stderr, "ln: %s and %s are the same directory entry\n", src, dst);
            return 1;
        }
        if (unlink(dst) == -1) {
            fprintf(stderr, "ln: %s: %s\n", dst, strerror(errno));
            return 1;
        }
    }

    if (opt_s) {
        if (symlink(src, dst) == -1) {
            fprintf(stderr, "ln: %s: %s\n", dst, strerror(errno));
            return 1;
        }
        return 0;
    }

    /* Hard link — use linkat() when source is a symlink so that -L/-P is honoured */
    struct stat src_lst;
    if (lstat(src, &src_lst) == 0 && S_ISLNK(src_lst.st_mode)) {
        int flags = opt_L ? AT_SYMLINK_FOLLOW : 0;
        if (linkat(AT_FDCWD, src, AT_FDCWD, dst, flags) == -1) {
            fprintf(stderr, "ln: %s: %s\n", dst, strerror(errno));
            return 1;
        }
    } else {
        if (link(src, dst) == -1) {
            fprintf(stderr, "ln: %s: %s\n", dst, strerror(errno));
            return 1;
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    int opt;
    while ((opt = getopt(argc, argv, "fsLP")) != -1) {
        switch (opt) {
        case 'f': opt_f = true;                    break;
        case 's': opt_s = true;                    break;
        case 'L': opt_L = true;  opt_P = false;   break;
        case 'P': opt_P = true;  opt_L = false;   break;
        default:
            fprintf(stderr, "usage: ln [-fs] [-L|-P] source_file target_file\n"
                            "       ln [-fs] [-L|-P] source_file... target_dir\n");
            return 1;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc < 2) {
        fprintf(stderr, "usage: ln [-fs] [-L|-P] source_file target_file\n"
                        "       ln [-fs] [-L|-P] source_file... target_dir\n");
        return 1;
    }

    const char *target = argv[argc - 1];
    struct stat target_st;
    bool target_is_dir = (stat(target, &target_st) == 0 && S_ISDIR(target_st.st_mode));

    if (!target_is_dir) {
        if (argc > 2) {
            fprintf(stderr, "ln: target '%s' is not a directory\n", target);
            return 1;
        }
        return do_link(argv[0], target);
    }

    /* Second form: link each source into target_dir */
    size_t tlen = strlen(target);
    bool trailing_slash = (tlen > 0 && target[tlen - 1] == '/');

    for (int i = 0; i < argc - 1; i++) {
        char *src_copy = strdup(argv[i]);
        if (!src_copy) {
            fprintf(stderr, "ln: out of memory\n");
            return 1;
        }
        const char *base = basename(src_copy);

        size_t blen = strlen(base);
        size_t need = tlen + (trailing_slash ? 0 : 1) + blen + 1;
        char *dst = malloc(need);
        if (!dst) {
            fprintf(stderr, "ln: out of memory\n");
            free(src_copy);
            return 1;
        }
        size_t pos = 0;
        memcpy(dst, target, tlen);            pos += tlen;
        if (!trailing_slash) dst[pos++] = '/';
        memcpy(dst + pos, base, blen + 1);

        if (do_link(argv[i], dst))
            had_error = 1;

        free(dst);
        free(src_copy);
    }

    return had_error ? 1 : 0;
}
