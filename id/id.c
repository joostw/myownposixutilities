/*
 * id.c - id: return user identity (POSIX.1-2024, XCU id)
 *
 * Synopsis:
 *   id [user]
 *   id -G [-n] [user]
 *   id -g [-nr] [user]
 *   id -u [-nr] [user]
 *
 * The identity of the process (getuid(), geteuid(), getgid(), getegid(),
 * getgroups()) or of a user from the user database; the user's groups are
 * the group of its entry and the groups of the group database that list
 * it as a member.
 *
 * Choices where the standard leaves room:
 *  - groups= is written whenever the system supports supplementary groups:
 *    the effective group first, then the other groups in the order
 *    getgroups() (or the group database) gives them, each once.
 *  - -G writes the effective group, then the real one if it differs, then
 *    the other groups, each once.
 *  - A user operand that is not a login name but a decimal number is taken
 *    as a user ID if the user database has an entry for it.
 *  - -n with a name that cannot be found writes the number.
 *  - -n or -r without -G, -g or -u, -r with -G, and more than one of -G,
 *    -g and -u are usage errors (status 2).
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
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

static void usage(void)
{
    fprintf(stderr, "usage: id [user]\n"
                    "       id -G [-n] [user]\n"
                    "       id -g [-nr] [user]\n"
                    "       id -u [-nr] [user]\n");
    exit(2);
}

static const char *uname_of(uid_t u)
{
    struct passwd *pw = getpwuid(u);
    return pw ? pw->pw_name : NULL;
}

static const char *gname_of(gid_t g)
{
    struct group *gr = getgrgid(g);
    return gr ? gr->gr_name : NULL;
}

static gid_t *groups;
static size_t ngroups, capgroups;

static void add_group(gid_t g)
{
    for (size_t i = 0; i < ngroups; i++)
        if (groups[i] == g)
            return;
    if (ngroups == capgroups) {
        capgroups = capgroups ? capgroups * 2 : 32;
        groups = realloc(groups, capgroups * sizeof *groups);
        if (!groups) {
            fprintf(stderr, "id: out of memory\n");
            exit(1);
        }
    }
    groups[ngroups++] = g;
}

static void put_id(unsigned long n, const char *name, bool names)
{
    if (names && name)
        fputs(name, stdout);
    else
        printf("%lu", n);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bool oG = false, og = false, on = false, orr = false, ou = false;
    int c;
    while ((c = getopt(argc, argv, "Ggnru")) != -1) {
        switch (c) {
        case 'G': oG = true; break;
        case 'g': og = true; break;
        case 'n': on = true; break;
        case 'r': orr = true; break;
        case 'u': ou = true; break;
        default: usage();
        }
    }
    if (oG + og + ou > 1 || ((on || orr) && !(oG || og || ou)) || (orr && oG) || argc - optind > 1)
        usage();

    uid_t ruid, euid;
    gid_t rgid, egid;
    if (optind < argc) {
        const char *user = argv[optind];
        struct passwd *pw = getpwnam(user);
        if (!pw && *user && strspn(user, "0123456789") == strlen(user)) {
            errno = 0;
            unsigned long v = strtoul(user, NULL, 10);
            if (!errno && v == (uid_t)v)
                pw = getpwuid((uid_t)v);
        }
        if (!pw) {
            fprintf(stderr, "id: %s: no such user\n", user);
            return 1;
        }
        ruid = euid = pw->pw_uid;
        rgid = egid = pw->pw_gid;
        char *name = strdup(pw->pw_name);
        if (!name) {
            fprintf(stderr, "id: out of memory\n");
            return 1;
        }
        add_group(egid);
        struct group *gr;
        setgrent();
        while ((gr = getgrent()) != NULL)
            for (char **m = gr->gr_mem; m && *m; m++)
                if (strcmp(*m, name) == 0) {
                    add_group(gr->gr_gid);
                    break;
                }
        endgrent();
        free(name);
    } else {
        ruid = getuid();
        euid = geteuid();
        rgid = getgid();
        egid = getegid();
        add_group(egid);
        int n = getgroups(0, NULL);
        if (n > 0) {
            gid_t *g = malloc((size_t)n * sizeof *g);
            if (!g) {
                fprintf(stderr, "id: out of memory\n");
                return 1;
            }
            n = getgroups(n, g);
            for (int i = 0; i < n; i++)
                add_group(g[i]);
            free(g);
        }
    }

    if (ou)
        put_id((unsigned long)(orr ? ruid : euid), uname_of(orr ? ruid : euid), on);
    else if (og)
        put_id((unsigned long)(orr ? rgid : egid), gname_of(orr ? rgid : egid), on);
    else if (oG) {
        /* effective, real, then the others: groups[0] is the effective */
        put_id((unsigned long)egid, gname_of(egid), on);
        if (rgid != egid) {
            putchar(' ');
            put_id((unsigned long)rgid, gname_of(rgid), on);
        }
        for (size_t i = 1; i < ngroups; i++)
            if (groups[i] != rgid) {
                putchar(' ');
                put_id((unsigned long)groups[i], gname_of(groups[i]), on);
            }
    } else {
        const char *s;
        printf("uid=%lu", (unsigned long)ruid);
        if ((s = uname_of(ruid)))
            printf("(%s)", s);
        printf(" gid=%lu", (unsigned long)rgid);
        if ((s = gname_of(rgid)))
            printf("(%s)", s);
        if (euid != ruid) {
            printf(" euid=%lu", (unsigned long)euid);
            if ((s = uname_of(euid)))
                printf("(%s)", s);
        }
        if (egid != rgid) {
            printf(" egid=%lu", (unsigned long)egid);
            if ((s = gname_of(egid)))
                printf("(%s)", s);
        }
        if (sysconf(_SC_NGROUPS_MAX) > 0)
            for (size_t i = 0; i < ngroups; i++) {
                printf("%s%lu", i ? "," : " groups=", (unsigned long)groups[i]);
                if ((s = gname_of(groups[i])))
                    printf("(%s)", s);
            }
    }
    putchar('\n');
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "id: write error: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
