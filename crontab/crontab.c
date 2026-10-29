/*
 * crontab.c - crontab: schedule periodic background work (POSIX.1-2024,
 * XCU crontab)
 *
 * Synopsis:
 *   crontab [file]
 *   crontab [-e|-l|-r]          [UP: -e]
 *
 * A user's crontab entry is the file named by the login name in the spool
 * directory (cron.c), written whole under a temporary name and renamed;
 * crond runs the commands.  Installed set-user-ID root, crontab reads the
 * file, and runs the editor, as the real user.
 *
 * Choices where the standard leaves room:
 *  - A file (or the standard input) is checked line by line before it is
 *    installed: a line that is not six fields with patterns of the right
 *    ranges is reported with its number, and nothing is installed (status
 *    1).  Day of the week 7 is not accepted ([0,6]).
 *  - -e: the entry is copied to a temporary file (in TMPDIR, else /tmp),
 *    "$EDITOR file" is run by sh (vi when EDITOR is unset or empty); if
 *    the editor fails, or the file is not valid, nothing is installed and
 *    the copy is kept, its name in the diagnostic.  An unchanged file is
 *    not installed again.
 *  - -l and -r without an entry: a diagnostic and status 1.
 *  - cron.allow and cron.deny (cron.c) apply to every use of crontab.
 *  - Usage errors: status 2.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "cron.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static uid_t ruid;
static bool setuid_root;
static char user[256];

static void privileged(bool on)
{
    if (setuid_root && seteuid(on ? 0 : ruid) != 0) {
        fprintf(stderr, "crontab: %s\n", strerror(errno));
        exit(1);
    }
}

static void usage(void)
{
    fprintf(stderr, "usage: crontab [file]\n       crontab [-e|-l|-r]\n");
    exit(2);
}

/* read a stream whole; NULL on a read error */
static char *slurp(FILE *f, size_t *len)
{
    size_t cap = 4096, n = 0;
    char *b = malloc(cap + 1);
    if (!b)
        return NULL;
    size_t k;
    while ((k = fread(b + n, 1, cap - n, f)) > 0) {
        n += k;
        if (n == cap) {
            char *nb = realloc(b, (cap *= 2) + 1);
            if (!nb) {
                free(b);
                return NULL;
            }
            b = nb;
        }
    }
    if (ferror(f)) {
        free(b);
        return NULL;
    }
    b[n] = '\0';
    *len = n;
    return b;
}

/* check every line; report the bad ones as "name: line N: ..." */
static bool check(const char *text, size_t len, const char *name)
{
    bool ok = true;
    size_t line = 0;
    const char *p = text, *end = text + len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t n = nl ? (size_t)(nl - p) : (size_t)(end - p);
        line++;
        char *l = strndup(p, n);
        if (!l) {
            fprintf(stderr, "crontab: out of memory\n");
            exit(1);
        }
        if (memchr(p, '\0', n)) {
            fprintf(stderr, "crontab: %s: line %zu: a NUL byte\n", name, line);
            ok = false;
        } else {
            struct entry e;
            char err[256];
            int r = cron_parse(l, &e, err, sizeof err);
            if (r < 0) {
                fprintf(stderr, "crontab: %s: line %zu: %s\n", name, line, err);
                ok = false;
            }
            free(e.command);
        }
        free(l);
        p += n + 1;
    }
    return ok;
}

static void path_of(char *buf, size_t size)
{
    snprintf(buf, size, "%s/%s", cron_spool(), user);
}

static int install(const char *text, size_t len)
{
    char path[4096], tmp[4200];
    path_of(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s/.new.%s.%ld", cron_spool(), user, (long)getpid());
    privileged(true);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0600);
    if (fd < 0) {
        fprintf(stderr, "crontab: %s: %s\n", tmp, strerror(errno));
        return 1;
    }
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, text + off, len - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "crontab: %s: %s\n", tmp, strerror(errno));
            close(fd);
            unlink(tmp);
            return 1;
        }
        off += (size_t)w;
    }
    if (len > 0 && text[len - 1] != '\n' && write(fd, "\n", 1) != 1) {
        close(fd);
        unlink(tmp);
        return 1;
    }
    if (fsync(fd) != 0 || close(fd) != 0 || rename(tmp, path) != 0) {
        fprintf(stderr, "crontab: %s: %s\n", path, strerror(errno));
        unlink(tmp);
        return 1;
    }
    privileged(false);
    return 0;
}

static char *read_entry(size_t *len, bool *exists)
{
    char path[4096];
    path_of(path, sizeof path);
    privileged(true);
    FILE *f = fopen(path, "r");
    privileged(false);
    *exists = f != NULL;
    if (!f) {
        if (errno != ENOENT) {
            fprintf(stderr, "crontab: %s: %s\n", path, strerror(errno));
            exit(1);
        }
        *len = 0;
        char *e = malloc(1);
        if (e)
            *e = '\0';
        return e;
    }
    char *t = slurp(f, len);
    fclose(f);
    if (!t) {
        fprintf(stderr, "crontab: %s: cannot read it\n", path);
        exit(1);
    }
    return t;
}

static int edit(void)
{
    size_t len;
    bool exists;
    char *old = read_entry(&len, &exists);
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir)
        dir = "/tmp";
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s/crontab.XXXXXX", dir);
    int fd = mkstemp(tmp);
    if (fd < 0) {
        fprintf(stderr, "crontab: %s: %s\n", tmp, strerror(errno));
        return 1;
    }
    if (len && write(fd, old, len) != (ssize_t)len) {
        fprintf(stderr, "crontab: %s: %s\n", tmp, strerror(errno));
        close(fd);
        unlink(tmp);
        return 1;
    }
    close(fd);
    const char *editor = getenv("EDITOR");
    if (!editor || !*editor)
        editor = "vi";
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "crontab: fork: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        /* the editor runs as the user only */
        if (setuid_root && (setuid(ruid) != 0 || geteuid() == 0))
            _exit(126);
        char cmd[4200];
        snprintf(cmd, sizeof cmd, "%s \"$1\"", editor);
        execl("/bin/sh", "sh", "-c", cmd, "sh", tmp, (char *)NULL);
        _exit(127);
    }
    int ws;
    while (waitpid(pid, &ws, 0) < 0)
        if (errno != EINTR) {
            fprintf(stderr, "crontab: wait: %s\n", strerror(errno));
            return 1;
        }
    if (!WIFEXITED(ws) || WEXITSTATUS(ws) != 0) {
        fprintf(stderr, "crontab: the editor failed; the entry is unchanged (the copy: %s)\n", tmp);
        return 1;
    }
    FILE *f = fopen(tmp, "r");
    size_t nlen;
    char *text = f ? slurp(f, &nlen) : NULL;
    if (f)
        fclose(f);
    if (!text) {
        fprintf(stderr, "crontab: %s: cannot read it\n", tmp);
        return 1;
    }
    if (exists && nlen == len && memcmp(text, old, len) == 0) {
        fprintf(stderr, "crontab: no changes\n");
        unlink(tmp);
        return 0;
    }
    if (!check(text, nlen, tmp)) {
        fprintf(stderr, "crontab: the entry is unchanged (the copy: %s)\n", tmp);
        return 1;
    }
    int r = install(text, nlen);
    if (r == 0)
        unlink(tmp);
    return r;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    ruid = getuid();
    setuid_root = geteuid() == 0 && ruid != 0;
    privileged(false);
    bool oe = false, ol = false, orr = false;
    int c;
    while ((c = getopt(argc, argv, "elr")) != -1) {
        switch (c) {
        case 'e': oe = true; break;
        case 'l': ol = true; break;
        case 'r': orr = true; break;
        default: usage();
        }
    }
    if (oe + ol + orr > 1 || ((oe || ol || orr) && optind < argc) || argc - optind > 1)
        usage();
    struct passwd *pw = getpwuid(ruid);
    if (!pw || strlen(pw->pw_name) >= sizeof user || strchr(pw->pw_name, '/')) {
        fprintf(stderr, "crontab: the invoking user has no usable user database entry\n");
        return 1;
    }
    strcpy(user, pw->pw_name);
    if (!cron_allowed(user, ruid == 0)) {
        fprintf(stderr, "crontab: you may not use crontab (cron.allow, cron.deny)\n");
        return 1;
    }
    if (ol) {
        size_t len;
        bool exists;
        char *t = read_entry(&len, &exists);
        if (!exists) {
            fprintf(stderr, "crontab: no crontab entry for %s\n", user);
            return 1;
        }
        if (fwrite(t, 1, len, stdout) != len || fflush(stdout) != 0) {
            fprintf(stderr, "crontab: write error: %s\n", strerror(errno));
            return 1;
        }
        return 0;
    }
    if (orr) {
        char path[4096];
        path_of(path, sizeof path);
        privileged(true);
        if (unlink(path) != 0) {
            fprintf(stderr, "crontab: %s\n", errno == ENOENT ? "no crontab entry to remove" : strerror(errno));
            return 1;
        }
        privileged(false);
        return 0;
    }
    if (oe)
        return edit();
    FILE *in = stdin;
    const char *name = "standard input";
    if (optind < argc) {
        name = argv[optind];
        if (!(in = fopen(name, "r"))) {
            fprintf(stderr, "crontab: %s: %s\n", name, strerror(errno));
            return 1;
        }
    }
    size_t len;
    char *text = slurp(in, &len);
    if (!text) {
        fprintf(stderr, "crontab: %s: %s\n", name, strerror(errno));
        return 1;
    }
    if (!check(text, len, name))
        return 1;
    return install(text, len);
}
