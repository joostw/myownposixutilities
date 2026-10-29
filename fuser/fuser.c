/*
 * fuser.c - fuser: list process IDs of all processes that have one or
 * more files open (POSIX.1-2024, XCU fuser) [XSI]
 *
 * Synopsis: fuser [-cfu] file...
 *
 * The processes are those of /proc (Linux); a process uses a file through
 * its working directory (/proc/PID/cwd, use 'c'), its root directory (root,
 * 'r'), its program (exe, 'e'), an open file descriptor (fd/N, no use
 * character), or a mapping (maps, 'm').  A file is matched by its device
 * and i-node; with -c, or for a block device that holds a mounted file
 * system (without -f), every file of that file system is the file.
 *
 * Choices where the standard leaves room:
 *  - The use characters are written in the order r, c, e, m, and an open
 *    file descriptor has none, as in psmisc fuser; a program, which is
 *    also mapped, is 'e' only.
 *  - -c with an operand that is not a mount point: its file system, as for
 *    a mount point.  A block device is mounted when /proc/self/mountinfo
 *    lists its device number.
 *  - The pathname written is the operand as it is.
 *  - Processes whose /proc entries cannot be read (another user's, without
 *    privileges) are not listed.  Listing no process is not an error.
 *  - An operand that does not exist: a diagnostic on its line, and status
 *    1 at the end.  Usage errors have status 2.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <locale.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static bool opt_c, opt_f, opt_u;

/* the device numbers of /proc/PID/maps (major:minor in hexadecimal) and of
   st_dev, compared as major and minor */
static unsigned long dev_major(unsigned long long d)
{
    return (unsigned long)(((d >> 8) & 0xfff) | ((d >> 32) & 0xfffff000ULL));
}

static unsigned long dev_minor(unsigned long long d)
{
    return (unsigned long)((d & 0xff) | ((d >> 12) & 0xffffff00ULL));
}

/* what is looked for */
struct target {
    bool fs;                    /* any file of the file system dev */
    unsigned long long dev, ino;
    bool also_file;             /* a mounted block device: the file too */
    unsigned long long fdev, fino;
};

static bool matches(const struct target *t, unsigned long long dev, unsigned long long ino)
{
    if (t->fs && dev == t->dev)
        return true;
    if (!t->fs && dev == t->dev && ino == t->ino)
        return true;
    return t->also_file && dev == t->fdev && ino == t->fino;
}

static bool stat_matches(const struct target *t, const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && matches(t, (unsigned long long)st.st_dev, (unsigned long long)st.st_ino);
}

/* the uses of the file by process pid: "rcem" letters, '.' for an open
   file; empty when none */
static void uses(const struct target *t, long pid, char *use)
{
    char path[64];
    size_t n = 0;
    snprintf(path, sizeof path, "/proc/%ld/root", pid);
    if (stat_matches(t, path))
        use[n++] = 'r';
    snprintf(path, sizeof path, "/proc/%ld/cwd", pid);
    if (stat_matches(t, path))
        use[n++] = 'c';
    snprintf(path, sizeof path, "/proc/%ld/exe", pid);
    if (stat_matches(t, path))
        use[n++] = 'e';
    /* mappings: "address perms offset dev inode path" */
    snprintf(path, sizeof path, "/proc/%ld/maps", pid);
    FILE *f = n > 0 && use[n - 1] == 'e' ? NULL : fopen(path, "r");  /* the program is mapped */
    if (f) {
        char line[4096];
        while (fgets(line, sizeof line, f)) {
            unsigned long ma, mi;
            unsigned long long ino;
            if (sscanf(line, "%*s %*s %*s %lx:%lx %llu", &ma, &mi, &ino) != 3 || ino == 0)
                continue;
            bool m = t->fs ? ma == dev_major(t->dev) && mi == dev_minor(t->dev)
                           : ma == dev_major(t->dev) && mi == dev_minor(t->dev) && ino == t->ino;
            if (!m && t->also_file)
                m = ma == dev_major(t->fdev) && mi == dev_minor(t->fdev) && ino == t->fino;
            if (m) {
                use[n++] = 'm';
                break;
            }
        }
        fclose(f);
    }
    snprintf(path, sizeof path, "/proc/%ld/fd", pid);
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.')
                continue;
            char fp[300];
            snprintf(fp, sizeof fp, "/proc/%ld/fd/%s", pid, e->d_name);
            if (stat_matches(t, fp)) {
                use[n++] = '.';
                break;
            }
        }
        closedir(d);
    }
    use[n] = '\0';
}

/* is device number rdev that of a mounted file system? */
static bool mounted(unsigned long long rdev)
{
    FILE *f = fopen("/proc/self/mountinfo", "r");
    if (!f)
        return false;
    char line[4096];
    bool found = false;
    while (!found && fgets(line, sizeof line, f)) {
        unsigned long ma, mi;
        if (sscanf(line, "%*s %*s %lu:%lu", &ma, &mi) == 2 && ma == dev_major(rdev) &&
            mi == dev_minor(rdev))
            found = true;
    }
    fclose(f);
    return found;
}

static int cmp_long(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    int c;
    while ((c = getopt(argc, argv, "cfu")) != -1) {
        switch (c) {
        case 'c': opt_c = true; break;
        case 'f': opt_f = true; break;
        case 'u': opt_u = true; break;
        default:
            fprintf(stderr, "usage: fuser [-cfu] file...\n");
            return 2;
        }
    }
    if (optind == argc) {
        fprintf(stderr, "usage: fuser [-cfu] file...\n");
        return 2;
    }
    /* the processes */
    DIR *d = opendir("/proc");
    if (!d) {
        fprintf(stderr, "fuser: /proc: %s\n", strerror(errno));
        return 1;
    }
    long *pids = NULL;
    size_t npids = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (e->d_name[0] >= '0' && e->d_name[0] <= '9') {
            long *np = realloc(pids, (npids + 1) * sizeof *pids);
            if (!np) {
                fprintf(stderr, "fuser: out of memory\n");
                return 1;
            }
            pids = np;
            pids[npids++] = strtol(e->d_name, NULL, 10);
        }
    closedir(d);
    qsort(pids, npids, sizeof *pids, cmp_long);
    long self = (long)getpid();

    int status = 0;
    setvbuf(stderr, NULL, _IONBF, 0);
    for (int i = optind; i < argc; i++) {
        const char *file = argv[i];
        struct stat st;
        fprintf(stderr, "%s:", file);
        if (stat(file, &st) != 0) {
            fprintf(stderr, " %s\n", strerror(errno));
            status = 1;
            continue;
        }
        struct target t = { false, (unsigned long long)st.st_dev, (unsigned long long)st.st_ino,
                            false, 0, 0 };
        if (opt_c)
            t.fs = true;
        else if (S_ISBLK(st.st_mode) && !opt_f && mounted((unsigned long long)st.st_rdev)) {
            t.fs = true;
            t.also_file = true;
            t.fdev = (unsigned long long)st.st_dev;
            t.fino = (unsigned long long)st.st_ino;
            t.dev = (unsigned long long)st.st_rdev;
        }
        for (size_t k = 0; k < npids; k++) {
            if (pids[k] == self)
                continue;
            char use[8];
            uses(&t, pids[k], use);
            if (!use[0])
                continue;
            /* the dot marks an open file: no character for it */
            char *dot = strchr(use, '.');
            if (dot)
                *dot = '\0';
            printf(" %ld", pids[k]);
            fflush(stdout);
            if (opt_u) {
                /* the real user ID: the first of Uid: in status */
                char path[64];
                long ruid = -1;
                snprintf(path, sizeof path, "/proc/%ld/status", pids[k]);
                FILE *sf = fopen(path, "r");
                if (sf) {
                    char line[256];
                    while (fgets(line, sizeof line, sf))
                        if (strncmp(line, "Uid:", 4) == 0) {
                            ruid = strtol(line + 4, NULL, 10);
                            break;
                        }
                    fclose(sf);
                }
                struct passwd *pw = ruid >= 0 ? getpwuid((uid_t)ruid) : NULL;
                if (pw)
                    fprintf(stderr, "%s(%s)", use, pw->pw_name);
                else
                    fprintf(stderr, "%s(%ld)", use, ruid);
            } else
                fprintf(stderr, "%s", use);
        }
        fputc('\n', stderr);
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "fuser: write error: %s\n", strerror(errno));
        return 1;
    }
    return status;
}
