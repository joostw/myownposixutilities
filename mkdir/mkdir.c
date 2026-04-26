/*
 * mkdir — make directories  (SUSv5 / IEEE Std 1003.1-2024)
 *
 * Synopsis: mkdir [-p] [-m mode] dir...
 */

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool   opt_p;
static bool   opt_m;
static mode_t opt_mode;
static int    had_error;

/*
 * Parse a chmod-style mode string.  base is the assumed initial mode
 * (used for symbolic + and -); for mkdir the spec says this is a=rwx (0777).
 * Returns 0 on success, -1 on error.
 */
static int parse_mode(const char *str, mode_t base, mode_t *out)
{
    /* Octal: string starts with a digit */
    if (*str >= '0' && *str <= '7') {
        char *end;
        unsigned long v = strtoul(str, &end, 8);
        if (*end != '\0' || v > 07777) {
            fprintf(stderr, "mkdir: invalid mode: '%s'\n", str);
            return -1;
        }
        *out = (mode_t)(v & 07777);
        return 0;
    }

    /* Symbolic mode: [who...][op perm...][,...]  */
    mode_t result = base & 07777;
    const char *p = str;

    while (*p) {
        /* Parse who letters */
        bool wu = false, wg = false, wo = false;
        while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
            switch (*p) {
            case 'u': wu = true; break;
            case 'g': wg = true; break;
            case 'o': wo = true; break;
            case 'a': wu = wg = wo = true; break;
            }
            p++;
        }
        if (!wu && !wg && !wo) wu = wg = wo = true; /* no who → all */

        /* Must have at least one op */
        if (*p != '+' && *p != '-' && *p != '=') {
            fprintf(stderr, "mkdir: invalid mode: '%s'\n", str);
            return -1;
        }

        while (*p == '+' || *p == '-' || *p == '=') {
            char op = *p++;
            mode_t perm = 0;

            /* Parse permission letters */
            while (*p && *p != '+' && *p != '-' && *p != '=' && *p != ',') {
                switch (*p) {
                case 'r':
                    if (wu) perm |= S_IRUSR;
                    if (wg) perm |= S_IRGRP;
                    if (wo) perm |= S_IROTH;
                    break;
                case 'w':
                    if (wu) perm |= S_IWUSR;
                    if (wg) perm |= S_IWGRP;
                    if (wo) perm |= S_IWOTH;
                    break;
                case 'x':
                    if (wu) perm |= S_IXUSR;
                    if (wg) perm |= S_IXGRP;
                    if (wo) perm |= S_IXOTH;
                    break;
                case 'X':
                    /* For directories, X is always equivalent to x */
                    if (wu) perm |= S_IXUSR;
                    if (wg) perm |= S_IXGRP;
                    if (wo) perm |= S_IXOTH;
                    break;
                case 's':
                    if (wu) perm |= S_ISUID;
                    if (wg) perm |= S_ISGID;
                    break;
                case 't':
                    perm |= S_ISVTX;
                    break;
                case 'u': {
                    mode_t u3 = (result >> 6) & 7;
                    if (wu) perm |= u3 << 6;
                    if (wg) perm |= u3 << 3;
                    if (wo) perm |= u3;
                    break;
                }
                case 'g': {
                    mode_t g3 = (result >> 3) & 7;
                    if (wu) perm |= g3 << 6;
                    if (wg) perm |= g3 << 3;
                    if (wo) perm |= g3;
                    break;
                }
                case 'o': {
                    mode_t o3 = result & 7;
                    if (wu) perm |= o3 << 6;
                    if (wg) perm |= o3 << 3;
                    if (wo) perm |= o3;
                    break;
                }
                default:
                    fprintf(stderr, "mkdir: invalid mode: '%s'\n", str);
                    return -1;
                }
                p++;
            }

            switch (op) {
            case '+':
                result |= perm;
                break;
            case '-':
                result &= ~perm;
                break;
            case '=': {
                mode_t who_mask = 0;
                if (wu) who_mask |= S_IRWXU | S_ISUID;
                if (wg) who_mask |= S_IRWXG | S_ISGID;
                if (wo) who_mask |= S_IRWXO | S_ISVTX;
                result = (result & ~who_mask) | perm;
                break;
            }
            }
        }

        if (*p == ',') {
            p++;
        } else if (*p != '\0') {
            fprintf(stderr, "mkdir: invalid mode: '%s'\n", str);
            return -1;
        }
    }

    *out = result;
    return 0;
}

/*
 * Try to create path with the given mode (bypassing umask).
 * If it already exists as a directory, treat as success when with_p is true.
 * Returns true on success, false on error.
 */
static bool try_mkdir(const char *path, mode_t m, bool existing_ok)
{
    mode_t saved = umask(0);
    int r = mkdir(path, m);
    umask(saved);

    if (r == 0) return true;

    if (errno == EEXIST && existing_ok) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return true;
        fprintf(stderr, "mkdir: cannot create directory '%s': File exists\n", path);
    } else {
        fprintf(stderr, "mkdir: cannot create directory '%s': %s\n",
                path, strerror(errno));
    }
    had_error = 1;
    return false;
}

static void make_dir_p(const char *orig)
{
    mode_t filemask = umask(0);
    umask(filemask);
    /* Intermediate dirs: user gets write+execute, rest follows umask complement */
    mode_t inter_mode = (S_IWUSR | S_IXUSR | ~filemask) & 0777;

    char *path = strdup(orig);
    if (!path) { perror("strdup"); had_error = 1; return; }

    char *p = path;
    while (*p == '/') p++; /* skip leading slashes */

    for (;;) {
        char *slash = strchr(p, '/');
        bool is_last;

        if (slash) {
            *slash = '\0';
            is_last = false;
        } else {
            is_last = true;
        }

        /* Skip empty components from consecutive slashes */
        if (*p != '\0') {
            int r;

            if (is_last && opt_m) {
                /* Final dir with -m: bypass umask, create with at least opt_mode,
                 * then chmod to exact opt_mode so directory is never more
                 * restrictive than the requested mode at any point. */
                mode_t saved = umask(0);
                r = mkdir(path, opt_mode | 0300);
                umask(saved);
                if (r == 0) {
                    if (chmod(path, opt_mode) != 0) {
                        fprintf(stderr,
                                "mkdir: cannot set permissions on '%s': %s\n",
                                path, strerror(errno));
                        had_error = 1;
                        free(path);
                        return;
                    }
                }
            } else if (is_last) {
                /* Final dir without -m: let kernel apply umask normally,
                 * matching the DESCRIPTION's mkdir(S_IRWXU|S_IRWXG|S_IRWXO). */
                r = mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO);
            } else {
                /* Intermediate dir: bypass umask, set traversal permissions. */
                mode_t saved = umask(0);
                r = mkdir(path, inter_mode | 0300);
                umask(saved);
                if (r == 0) {
                    if (chmod(path, inter_mode) != 0) {
                        fprintf(stderr,
                                "mkdir: cannot set permissions on '%s': %s\n",
                                path, strerror(errno));
                        had_error = 1;
                        free(path);
                        return;
                    }
                }
            }

            if (r != 0) {
                if (errno == EEXIST) {
                    struct stat st;
                    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
                        fprintf(stderr,
                                "mkdir: cannot create directory '%s': File exists\n",
                                path);
                        had_error = 1;
                        free(path);
                        return;
                    }
                    /* Existing directory: OK under -p */
                } else {
                    fprintf(stderr, "mkdir: cannot create directory '%s': %s\n",
                            path, strerror(errno));
                    had_error = 1;
                    free(path);
                    return;
                }
            }
        }

        if (is_last) break;

        *slash = '/';
        p = slash + 1;
        while (*p == '/') p++; /* skip consecutive slashes */
    }

    free(path);
}

static void make_dir(const char *dir)
{
    if (opt_p) {
        make_dir_p(dir);
        return;
    }

    if (opt_m) {
        if (!try_mkdir(dir, opt_mode | 0300, false)) return;
        if (chmod(dir, opt_mode) != 0) {
            fprintf(stderr, "mkdir: cannot set permissions on '%s': %s\n",
                    dir, strerror(errno));
            had_error = 1;
        }
    } else {
        /* Default: mkdir with 0777; kernel applies umask */
        if (mkdir(dir, S_IRWXU | S_IRWXG | S_IRWXO) != 0) {
            fprintf(stderr, "mkdir: cannot create directory '%s': %s\n",
                    dir, strerror(errno));
            had_error = 1;
        }
    }
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    int opt;
    while ((opt = getopt(argc, argv, "m:p")) != -1) {
        switch (opt) {
        case 'm':
            if (parse_mode(optarg, 0777, &opt_mode) != 0) return 1;
            opt_m = true;
            break;
        case 'p':
            opt_p = true;
            break;
        default:
            fprintf(stderr, "usage: mkdir [-p] [-m mode] dir...\n");
            return 1;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc == 0) {
        fprintf(stderr, "usage: mkdir [-p] [-m mode] dir...\n");
        return 1;
    }

    for (int i = 0; i < argc; i++)
        make_dir(argv[i]);

    return had_error ? 1 : 0;
}
