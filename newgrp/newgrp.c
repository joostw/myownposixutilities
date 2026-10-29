/*
 * newgrp.c - newgrp: change to a new group (POSIX.1-2024, XCU newgrp)
 *
 * Synopsis: newgrp [-l] [group]
 *
 * The real and effective group IDs are set to the group (setgid(); with
 * the privileges to do so: newgrp is installed set-user-ID root, or run by
 * root), the supplementary groups are changed as the standard describes,
 * the privileges are given up (setuid(getuid())), and the user's login
 * shell is executed.  With no operand, the group of the user's entry in
 * the user database, and the supplementary groups that list the user in
 * the group database.  The supplementary groups are set with setgroups(),
 * which POSIX does not specify.
 *
 * Choices where the standard leaves room:
 *  - Group passwords are not used (POSIX has no interface for the shadow
 *    group file): a user who is not root may change only to a group whose
 *    member it is (in the group database, or the group of its user entry).
 *  - A group that is not in the group database, a numeric one included, is
 *    refused for a user who is not root; root may change to any number.
 *  - When the group cannot be assigned (it is unknown, the user is not a
 *    member, or the system refuses), a diagnostic is written and the shell
 *    is executed all the same, with the groups unchanged, as the standard
 *    requires.
 *  - The shell is the login shell of the user database entry (sh if it is
 *    empty).  With -l (or a first operand "-", as historically), the
 *    shell is a login shell (its argv[0] starts with '-'), the environment
 *    is that of a new login (HOME, SHELL, USER, LOGNAME, PATH from getconf
 *    PATH, and TERM kept), and the working directory is HOME.
 *  - Usage errors have status 2, a shell that cannot be executed 127.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* not in POSIX; declared as BSD and glibc have it */
extern int setgroups(size_t, const gid_t *);

static gid_t *list;
static size_t nlist, caplist;

static void add_gid(gid_t g)
{
    for (size_t i = 0; i < nlist; i++)
        if (list[i] == g)
            return;
    if (nlist == caplist) {
        caplist = caplist ? caplist * 2 : 32;
        if (!(list = realloc(list, caplist * sizeof *list))) {
            fprintf(stderr, "newgrp: out of memory\n");
            exit(1);
        }
    }
    list[nlist++] = g;
}

static bool in_list(gid_t g)
{
    for (size_t i = 0; i < nlist; i++)
        if (list[i] == g)
            return true;
    return false;
}

static void del_gid(gid_t g)
{
    for (size_t i = 0; i < nlist; i++)
        if (list[i] == g) {
            memmove(list + i, list + i + 1, (nlist - i - 1) * sizeof *list);
            nlist--;
            return;
        }
}

static bool is_member(const struct group *gr, const char *user)
{
    for (char **m = gr->gr_mem; m && *m; m++)
        if (strcmp(*m, user) == 0)
            return true;
    return false;
}

static void usage(void)
{
    fprintf(stderr, "usage: newgrp [-l] [group]\n");
    exit(2);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool login = false;
    int c;
    if (argc > 1 && strcmp(argv[1], "-") == 0) {
        login = true;
        argv[1] = argv[0];
        argv++;
        argc--;
    }
    while ((c = getopt(argc, argv, "l")) != -1) {
        if (c != 'l')
            usage();
        login = true;
    }
    if (argc - optind > 1)
        usage();
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (!pw) {
        fprintf(stderr, "newgrp: no user database entry for user ID %lu\n", (unsigned long)uid);
        return 1;
    }
    char *user = strdup(pw->pw_name), *home = strdup(pw->pw_dir), *shell = strdup(pw->pw_shell);
    gid_t user_gid = pw->pw_gid;
    if (!user || !home || !shell) {
        fprintf(stderr, "newgrp: out of memory\n");
        return 1;
    }
    bool privileged = geteuid() == 0;
    bool root = uid == 0;

    /* the group, and whether it may be assigned */
    bool ok = true;
    gid_t gid = user_gid;
    if (optind < argc) {
        const char *name = argv[optind];
        struct group *gr = getgrnam(name);
        bool numeric = *name && strspn(name, "0123456789") == strlen(name);
        if (!gr && numeric) {
            errno = 0;
            unsigned long v = strtoul(name, NULL, 10);
            if (errno || v != (gid_t)v) {
                fprintf(stderr, "newgrp: %s: not a group\n", name);
                ok = false;
            } else {
                gid = (gid_t)v;
                gr = getgrgid(gid);
            }
        } else if (gr)
            gid = gr->gr_gid;
        else {
            fprintf(stderr, "newgrp: %s: no such group\n", name);
            ok = false;
        }
        if (ok && !root) {
            if (!gr) {
                fprintf(stderr, "newgrp: %s: no such group\n", name);
                ok = false;
            } else if (gid != user_gid && !is_member(gr, user)) {
                fprintf(stderr, "newgrp: %s: permission denied (not a member)\n", name);
                ok = false;
            }
        }
        if (ok) {
            /* the supplementary groups, as the standard describes */
            int n = getgroups(0, NULL);
            if (n > 0) {
                gid_t *g = malloc((size_t)n * sizeof *g);
                if (!g) {
                    fprintf(stderr, "newgrp: out of memory\n");
                    return 1;
                }
                n = getgroups(n, g);
                for (int i = 0; i < n; i++)
                    add_gid(g[i]);
                free(g);
            }
            long max = sysconf(_SC_NGROUPS_MAX);
            gid_t old = getegid();
            if (in_list(old)) {
                if (!in_list(gid) && (max < 0 || (long)nlist < max))
                    add_gid(gid);
            } else {
                del_gid(gid);
                if (max < 0 || (long)nlist < max)
                    add_gid(old);
            }
        }
    } else {
        /* back to the user's own groups */
        add_gid(user_gid);
        struct group *gr;
        setgrent();
        while ((gr = getgrent()) != NULL)
            if (is_member(gr, user))
                add_gid(gr->gr_gid);
        endgrent();
    }
    if (ok) {
        if (!privileged)
            fprintf(stderr, "newgrp: cannot change the group: not privileged (newgrp must be "
                            "set-user-ID root)\n");
        else if (setgroups(nlist, list) != 0 || setgid(gid) != 0)
            fprintf(stderr, "newgrp: cannot change the group: %s\n", strerror(errno));
    }
    /* give up the privileges, if any */
    if (setuid(uid) != 0 || (uid != 0 && geteuid() == 0)) {
        fprintf(stderr, "newgrp: cannot give up the privileges: %s\n", strerror(errno));
        return 1;
    }

    /* the shell */
    if (!*shell) {
        free(shell);
        shell = strdup("/bin/sh");
        if (!shell)
            return 1;
    }
    const char *base = strrchr(shell, '/');
    base = base ? base + 1 : shell;
    char *arg0 = malloc(strlen(base) + 2);
    if (!arg0)
        return 1;
    sprintf(arg0, "%s%s", login ? "-" : "", base);
    if (login) {
        const char *term = getenv("TERM");
        char *t = term ? strdup(term) : NULL;
        static char *empty[] = { NULL };
        extern char **environ;
        environ = empty;
        size_t len = confstr(_CS_PATH, NULL, 0);
        char *path = len ? malloc(len) : NULL;
        if (path)
            confstr(_CS_PATH, path, len);
        setenv("HOME", home, 1);
        setenv("SHELL", shell, 1);
        setenv("USER", user, 1);
        setenv("LOGNAME", user, 1);
        setenv("PATH", path ? path : "/usr/bin:/bin", 1);
        if (t)
            setenv("TERM", t, 1);
        if (chdir(home) != 0)
            fprintf(stderr, "newgrp: %s: %s\n", home, strerror(errno));
    }
    char *sargv[] = { arg0, NULL };
    execv(shell, sargv);
    fprintf(stderr, "newgrp: %s: %s\n", shell, strerror(errno));
    return 127;
}
