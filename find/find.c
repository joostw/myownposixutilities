/*
 * find.c - find: find files (POSIX.1-2024, XCU find)
 *
 * Synopsis: find [-H|-L] path... [operand_expression...]
 *
 * The expression is parsed into a tree once.  The hierarchy is walked with
 * fstatat() and openat() relative to open directories, so pathname length
 * is no limit and the working directory never changes; at most 32
 * directory descriptors are kept open, a closed level being reopened as
 * ".." of the level below (and checked by its device and inode), except
 * the level above one reached through a symbolic link (-L), which stays
 * open.  Each directory is read completely before its entries are
 * visited.  Commands of -exec and -ok run in find's working directory.
 *
 * Choices where the standard leaves room:
 *  - Without path operands, "." is searched.
 *  - -print0, like -print, keeps the expression from being given an
 *    implicit -print (as the standard's own examples assume).
 *  - -exec ... ; replaces "{}" also inside a longer argument; -exec ... {} +
 *    only an argument that is just "{}" (the last one before the +).
 *  - -newer without -H or -L follows a symbolic link (or, if it dangles,
 *    uses the link itself).
 *  - -perm of a symbolic link without -L or -H is evaluated on the link's
 *    own mode; -perm's symbolic mode is applied like chmod's, the file
 *    mode creation mask included for clauses without a who.
 *  - The prompt of -ok is "< utility argument... >? ".
 *  - -atime, -ctime, -mtime count whole seconds; a time in the future
 *    gives a negative or zero number of days (the remainder discarded).
 *  - With -H or -L, a link that cannot be followed because its target
 *    does not exist is acted on as the link itself; one that cannot be
 *    followed for another reason (a loop of links) is reported and left
 *    out.
 *  - A directory loop (-L) and an unreadable directory are reported; the
 *    exit status is then 1.  The status is also 1 if a command of -exec
 *    ... + fails, or if output cannot be written.
 *  - Errors in the expression exit with status 2.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <grp.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wctype.h>

extern char **environ;

static char follow = 'P';       /* H, L or P */
static bool depth_mode, xdev, mount;
static int status;
static time_t now;

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "find: out of memory\n");
        exit(1);
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    if (!(p = realloc(p, n ? n : 1))) {
        fprintf(stderr, "find: out of memory\n");
        exit(1);
    }
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

static void syntax(const char *fmt, const char *arg)
{
    fprintf(stderr, "find: ");
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    exit(2);
}

/* ---- the expression ---- */

enum kind {
    K_AND, K_OR, K_NOT,
    K_NAME, K_INAME, K_PATH, K_NOUSER, K_NOGROUP, K_MOUNT, K_XDEV, K_PRUNE, K_PERM, K_TYPE,
    K_LINKS, K_USER, K_GROUP, K_SIZE, K_ATIME, K_CTIME, K_MTIME, K_EXEC, K_EXECPLUS, K_OK,
    K_PRINT, K_PRINT0, K_NEWER, K_DEPTH, K_TRUE
};

struct exec {
    char **argv;                /* the template, NULL-terminated */
    int argc;
    /* -exec ... {} +: the pathnames collected */
    char **set;
    size_t nset, capset, setbytes;
    int fixed;                  /* the arguments before {} */
};

struct node {
    enum kind kind;
    struct node *l, *r;
    const char *s;              /* a pattern */
    char cmp;                   /* + - or = for numbers */
    long long n;
    bool bytes;                 /* -size ...c */
    mode_t mode;
    bool perm_all;              /* -perm -mode */
    uid_t uid;
    gid_t gid;
    struct timespec mtime;      /* -newer */
    char type;
    struct exec *ex;
};

static char **av;
static int ac, ap;
static bool has_action;
static struct exec **plus_execs;
static size_t nplus;

static struct node *new_node(enum kind k)
{
    struct node *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->kind = k;
    return n;
}

static const char *next_arg(const char *primary)
{
    if (ap >= ac)
        syntax("%s: an argument is missing", primary);
    return av[ap++];
}

static void number_arg(struct node *n, const char *primary, const char *s, bool allow_c)
{
    const char *p = s;
    n->cmp = '=';
    if (*p == '+' || *p == '-')
        n->cmp = *p++;
    if (!isdigit((unsigned char)*p))
        syntax("%s: not a number", s);
    char *end;
    errno = 0;
    n->n = strtoll(p, &end, 10);
    if (errno)
        syntax("%s: out of range", s);
    if (allow_c && *end == 'c') {
        n->bytes = true;
        end++;
    }
    if (*end) {
        fprintf(stderr, "find: %s %s: ", primary, s);
        syntax("%snot a valid number", "");
    }
}

static bool numcmp(const struct node *n, long long v)
{
    switch (n->cmp) {
    case '+': return v > n->n;
    case '-': return v < n->n;
    default: return v == n->n;
    }
}

/* -perm's mode: like chmod's symbolic mode, applied to a template of 0 */
static mode_t perm_mode(const char *s)
{
    if (isdigit((unsigned char)*s)) {
        mode_t m = 0;
        for (const char *p = s; *p; p++) {
            if (*p < '0' || *p > '7')
                syntax("-perm %s: not a valid mode", s);
            m = m * 8 + (mode_t)(*p - '0');
            if (m > 07777)
                syntax("-perm %s: not a valid mode", s);
        }
        return m;
    }
    mode_t cmask = umask(0);
    umask(cmask);
    mode_t m = 0;
    const char *p = s;
    for (;;) {
        int who = 0;
        for (;; p++) {
            if (*p == 'u') who |= 1;
            else if (*p == 'g') who |= 2;
            else if (*p == 'o') who |= 4;
            else if (*p == 'a') who |= 7;
            else break;
        }
        if (*p != '+' && *p != '-' && *p != '=')
            syntax("-perm %s: not a valid mode", s);
        while (*p == '+' || *p == '-' || *p == '=') {
            char op = *p++;
            mode_t perm = 0;
            if (*p == 'u' || *p == 'g' || *p == 'o') {
                mode_t c = *p == 'u' ? (m >> 6) & 7 : *p == 'g' ? (m >> 3) & 7 : m & 7;
                perm = c << 6 | c << 3 | c;
                p++;
            } else {
                for (;; p++) {
                    if (*p == 'r') perm |= 0444;
                    else if (*p == 'w') perm |= 0222;
                    else if (*p == 'x') perm |= 0111;
                    else if (*p == 'X') ;   /* the template has no x bits and is no directory */
                    else if (*p == 's') perm |= S_ISUID | S_ISGID;
                    else if (*p == 't') perm |= S_ISVTX;
                    else break;
                }
            }
            mode_t mask = 0;
            if (who) {
                if (who & 1) mask |= S_IRWXU | S_ISUID;
                if (who & 2) mask |= S_IRWXG | S_ISGID;
                if (who & 4) mask |= S_IRWXO | S_ISVTX;
            } else
                mask = (07777 & ~cmask) | S_ISUID | S_ISGID | S_ISVTX;
            perm &= mask;
            if (op == '+')
                m |= perm;
            else if (op == '-')
                m &= ~perm;
            else {
                if (who) {
                    mode_t clear = 0;
                    if (who & 1) clear |= S_IRWXU | S_ISUID;
                    if (who & 2) clear |= S_IRWXG | S_ISGID;
                    if (who & 4) clear |= S_IRWXO | S_ISVTX;
                    m &= ~clear;
                } else
                    m = 0;
                m |= perm;
            }
        }
        if (!*p)
            break;
        if (*p != ',')
            syntax("-perm %s: not a valid mode", s);
        p++;
    }
    return m;
}

static struct exec *parse_exec(const char *primary, bool allow_plus, bool *plus)
{
    struct exec *e = xmalloc(sizeof *e);
    memset(e, 0, sizeof *e);
    int start = ap;
    *plus = false;
    while (ap < ac) {
        if (strcmp(av[ap], ";") == 0)
            break;
        if (allow_plus && strcmp(av[ap], "+") == 0 && ap > start && strcmp(av[ap - 1], "{}") == 0) {
            *plus = true;
            break;
        }
        ap++;
    }
    if (ap >= ac)
        syntax("%s: the terminating ; or + is missing", primary);
    e->argc = ap - start;
    if (e->argc == 0)
        syntax("%s: the utility is missing", primary);
    e->argv = xmalloc((size_t)(e->argc + 1) * sizeof *e->argv);
    for (int i = 0; i < e->argc; i++)
        e->argv[i] = av[start + i];
    e->argv[e->argc] = NULL;
    if (*plus)
        e->fixed = e->argc - 1;
    ap++;                       /* the ; or + */
    return e;
}

static struct node *parse_or(void);

static struct node *parse_primary(void)
{
    if (ap >= ac)
        syntax("%sthe expression is incomplete", "");
    const char *a = av[ap++];
    struct node *n;
    if (strcmp(a, "(") == 0) {
        n = parse_or();
        if (ap >= ac || strcmp(av[ap], ")") != 0)
            syntax("%sa ) is missing", "");
        ap++;
        return n;
    }
    if (strcmp(a, "!") == 0) {
        n = new_node(K_NOT);
        n->l = parse_primary();
        return n;
    }
    if (strcmp(a, "-name") == 0 || strcmp(a, "-iname") == 0 || strcmp(a, "-path") == 0) {
        n = new_node(a[1] == 'n' ? K_NAME : a[1] == 'i' ? K_INAME : K_PATH);
        n->s = next_arg(a);
        return n;
    }
    if (strcmp(a, "-nouser") == 0) return new_node(K_NOUSER);
    if (strcmp(a, "-nogroup") == 0) return new_node(K_NOGROUP);
    if (strcmp(a, "-mount") == 0) { mount = xdev = true; return new_node(K_MOUNT); }
    if (strcmp(a, "-xdev") == 0) { xdev = true; return new_node(K_XDEV); }
    if (strcmp(a, "-prune") == 0) return new_node(K_PRUNE);
    if (strcmp(a, "-depth") == 0) { depth_mode = true; return new_node(K_DEPTH); }
    if (strcmp(a, "-print") == 0) { has_action = true; return new_node(K_PRINT); }
    if (strcmp(a, "-print0") == 0) { has_action = true; return new_node(K_PRINT0); }
    if (strcmp(a, "-perm") == 0) {
        n = new_node(K_PERM);
        const char *m = next_arg(a);
        if (*m == '-') {
            n->perm_all = true;
            m++;
        }
        n->mode = perm_mode(m);
        return n;
    }
    if (strcmp(a, "-type") == 0) {
        n = new_node(K_TYPE);
        const char *t = next_arg(a);
        if (strlen(t) != 1 || !strchr("bcdlpfs", t[0]))
            syntax("-type %s: not a type (b c d l p f s)", t);
        n->type = t[0];
        return n;
    }
    if (strcmp(a, "-links") == 0) {
        n = new_node(K_LINKS);
        number_arg(n, a, next_arg(a), false);
        return n;
    }
    if (strcmp(a, "-size") == 0) {
        n = new_node(K_SIZE);
        number_arg(n, a, next_arg(a), true);
        return n;
    }
    if (strcmp(a, "-atime") == 0 || strcmp(a, "-ctime") == 0 || strcmp(a, "-mtime") == 0) {
        n = new_node(a[1] == 'a' ? K_ATIME : a[1] == 'c' ? K_CTIME : K_MTIME);
        number_arg(n, a, next_arg(a), false);
        return n;
    }
    if (strcmp(a, "-user") == 0) {
        n = new_node(K_USER);
        const char *u = next_arg(a);
        struct passwd *pw = getpwnam(u);
        if (pw)
            n->uid = pw->pw_uid;
        else if (*u && strspn(u, "0123456789") == strlen(u))
            n->uid = (uid_t)strtoul(u, NULL, 10);
        else
            syntax("-user %s: no such user", u);
        return n;
    }
    if (strcmp(a, "-group") == 0) {
        n = new_node(K_GROUP);
        const char *g = next_arg(a);
        struct group *gr = getgrnam(g);
        if (gr)
            n->gid = gr->gr_gid;
        else if (*g && strspn(g, "0123456789") == strlen(g))
            n->gid = (gid_t)strtoul(g, NULL, 10);
        else
            syntax("-group %s: no such group", g);
        return n;
    }
    if (strcmp(a, "-newer") == 0) {
        n = new_node(K_NEWER);
        const char *f = next_arg(a);
        struct stat st;
        if (stat(f, &st) != 0 && lstat(f, &st) != 0) {
            fprintf(stderr, "find: %s: %s\n", f, strerror(errno));
            exit(2);
        }
        n->mtime = st.st_mtim;
        return n;
    }
    if (strcmp(a, "-exec") == 0 || strcmp(a, "-ok") == 0) {
        bool plus;
        bool ok = a[1] == 'o';
        n = new_node(ok ? K_OK : K_EXEC);
        n->ex = parse_exec(a, !ok, &plus);
        if (plus) {
            n->kind = K_EXECPLUS;
            plus_execs = xrealloc(plus_execs, (nplus + 1) * sizeof *plus_execs);
            plus_execs[nplus++] = n->ex;
        }
        has_action = true;
        return n;
    }
    if (strcmp(a, ")") == 0)
        syntax("%sa ( is missing, or the expression before ) is", "");
    syntax("%s: not a primary or operator", a);
    return NULL;
}

static bool starts_primary(void)
{
    return ap < ac && strcmp(av[ap], "-o") != 0 && strcmp(av[ap], ")") != 0;
}

static struct node *parse_and(void)
{
    struct node *n = parse_primary();
    while (starts_primary()) {
        if (strcmp(av[ap], "-a") == 0) {
            ap++;
        }
        struct node *a = new_node(K_AND);
        a->l = n;
        a->r = parse_primary();
        n = a;
    }
    return n;
}

static struct node *parse_or(void)
{
    struct node *n = parse_and();
    while (ap < ac && strcmp(av[ap], "-o") == 0) {
        ap++;
        struct node *o = new_node(K_OR);
        o->l = n;
        o->r = parse_and();
        n = o;
    }
    return n;
}

/* ---- running commands ---- */

static long arg_max;

static int run(char **argv)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "find: fork: %s\n", strerror(errno));
        status = 1;
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        int e = errno;
        fprintf(stderr, "find: %s: %s\n", argv[0], strerror(e));
        _exit(e == ENOENT ? 127 : 126);
    }
    int ws;
    while (waitpid(pid, &ws, 0) < 0)
        if (errno != EINTR) {
            status = 1;
            return -1;
        }
    return WIFEXITED(ws) ? WEXITSTATUS(ws) : 128 + (WIFSIGNALED(ws) ? WTERMSIG(ws) : 0);
}

/* replace each "{}" in s by path */
static char *subst(const char *s, const char *path)
{
    size_t n = 0, pl = strlen(path);
    for (const char *p = s; (p = strstr(p, "{}")); p += 2)
        n++;
    if (n == 0)
        return (char *)s;
    char *r = xmalloc(strlen(s) + n * pl + 1), *q = r;
    for (const char *p = s; *p;) {
        if (p[0] == '{' && p[1] == '}') {
            memcpy(q, path, pl);
            q += pl;
            p += 2;
        } else
            *q++ = *p++;
    }
    *q = '\0';
    return r;
}

static bool exec_one(struct exec *e, const char *path)
{
    char **argv = xmalloc((size_t)(e->argc + 1) * sizeof *argv);
    for (int i = 0; i < e->argc; i++)
        argv[i] = subst(e->argv[i], path);
    argv[e->argc] = NULL;
    int r = run(argv);
    for (int i = 0; i < e->argc; i++)
        if (argv[i] != e->argv[i])
            free(argv[i]);
    free(argv);
    return r == 0;
}

static void exec_flush(struct exec *e)
{
    if (e->nset == 0)
        return;
    char **argv = xmalloc((size_t)(e->fixed + e->nset + 1) * sizeof *argv);
    for (int i = 0; i < e->fixed; i++)
        argv[i] = e->argv[i];
    for (size_t i = 0; i < e->nset; i++)
        argv[e->fixed + (int)i] = e->set[i];
    argv[e->fixed + (int)e->nset] = NULL;
    if (run(argv) != 0)
        status = 1;
    for (size_t i = 0; i < e->nset; i++)
        free(e->set[i]);
    free(argv);
    e->nset = 0;
    e->setbytes = 0;
}

static void exec_add(struct exec *e, const char *path)
{
    size_t need = strlen(path) + 1 + sizeof(char *);
    if (e->nset > 0 && (long)(e->setbytes + need) > arg_max)
        exec_flush(e);
    if (e->nset == e->capset) {
        e->capset = e->capset ? e->capset * 2 : 64;
        e->set = xrealloc(e->set, e->capset * sizeof *e->set);
    }
    e->set[e->nset++] = xstrdup(path);
    e->setbytes += need;
}

static bool yes(void)
{
    static regex_t re;
    static int ready;
    if (!ready) {
        const char *y = nl_langinfo(YESEXPR);
        ready = (y && *y && regcomp(&re, y, REG_EXTENDED | REG_NOSUB) == 0) ? 1 : -1;
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, stdin);
    bool r = false;
    if (n > 0) {
        if (line[n - 1] == '\n')
            line[n - 1] = '\0';
        r = ready == 1 ? regexec(&re, line, 0, NULL, 0) == 0 : (line[0] == 'y' || line[0] == 'Y');
    }
    free(line);
    return r;
}

/* ---- evaluating ---- */

static char *path;              /* the current pathname */
static size_t pathlen, pathcap;
static bool prune;

static const char *base_name(void)
{
    /* the last component, without trailing slashes ("/" stays "/") */
    static char *buf;
    static size_t cap;
    size_t n = pathlen;
    while (n > 1 && path[n - 1] == '/')
        n--;
    size_t i = n;
    while (i > 0 && path[i - 1] != '/')
        i--;
    if (n - i + 1 > cap) {
        cap = n - i + 1;
        buf = xrealloc(buf, cap);
    }
    memcpy(buf, path + i, n - i);
    buf[n - i] = '\0';
    return buf;
}

static bool match_icase(const char *pat, const char *s)
{
#ifdef FNM_CASEFOLD
    return fnmatch(pat, s, FNM_CASEFOLD) == 0;
#else
    /* fold both to lower case, single-byte characters only */
    char *p = xstrdup(pat), *t = xstrdup(s);
    for (char *q = p; *q; q++)
        *q = (char)tolower((unsigned char)*q);
    for (char *q = t; *q; q++)
        *q = (char)tolower((unsigned char)*q);
    bool r = fnmatch(p, t, 0) == 0;
    free(p);
    free(t);
    return r;
#endif
}

static long long days(time_t t)
{
    return (long long)(now - t) / 86400;
}

static bool out_path(char end)
{
    if (fwrite(path, 1, pathlen, stdout) != pathlen || putchar(end) == EOF) {
        status = 1;
        return false;
    }
    return true;
}

static bool eval(const struct node *n, const struct stat *st)
{
    switch (n->kind) {
    case K_AND: return eval(n->l, st) && eval(n->r, st);
    case K_OR: return eval(n->l, st) || eval(n->r, st);
    case K_NOT: return !eval(n->l, st);
    case K_NAME: return fnmatch(n->s, base_name(), 0) == 0;
    case K_INAME: return match_icase(n->s, base_name());
    case K_PATH: return fnmatch(n->s, path, 0) == 0;
    case K_NOUSER: return getpwuid(st->st_uid) == NULL;
    case K_NOGROUP: return getgrgid(st->st_gid) == NULL;
    case K_MOUNT: case K_XDEV: case K_DEPTH: case K_TRUE: return true;
    case K_PRUNE:
        prune = true;
        return true;
    case K_PERM:
        if (n->perm_all)
            return (st->st_mode & n->mode & 07777) == n->mode;
        return (st->st_mode & 07777) == n->mode;
    case K_TYPE:
        switch (n->type) {
        case 'b': return S_ISBLK(st->st_mode);
        case 'c': return S_ISCHR(st->st_mode);
        case 'd': return S_ISDIR(st->st_mode);
        case 'l': return S_ISLNK(st->st_mode);
        case 'p': return S_ISFIFO(st->st_mode);
        case 'f': return S_ISREG(st->st_mode);
        default: return S_ISSOCK(st->st_mode);
        }
    case K_LINKS: return numcmp(n, (long long)st->st_nlink);
    case K_USER: return st->st_uid == n->uid;
    case K_GROUP: return st->st_gid == n->gid;
    case K_SIZE:
        if (n->bytes)
            return numcmp(n, (long long)st->st_size);
        return numcmp(n, ((long long)st->st_size + 511) / 512);
    case K_ATIME: return numcmp(n, days(st->st_atim.tv_sec));
    case K_CTIME: return numcmp(n, days(st->st_ctim.tv_sec));
    case K_MTIME: return numcmp(n, days(st->st_mtim.tv_sec));
    case K_NEWER:
        return st->st_mtim.tv_sec > n->mtime.tv_sec ||
               (st->st_mtim.tv_sec == n->mtime.tv_sec && st->st_mtim.tv_nsec > n->mtime.tv_nsec);
    case K_PRINT: out_path('\n'); return true;
    case K_PRINT0: out_path('\0'); return true;
    case K_EXEC: return exec_one(n->ex, path);
    case K_EXECPLUS: exec_add(n->ex, path); return true;
    case K_OK: {
        fflush(stdout);
        fputs("< ", stderr);
        for (int i = 0; i < n->ex->argc; i++) {
            char *a = subst(n->ex->argv[i], path);
            fprintf(stderr, "%s ", a);
            if (a != n->ex->argv[i])
                free(a);
        }
        fputs(">? ", stderr);
        if (!yes())
            return false;
        return exec_one(n->ex, path);
    }
    }
    return false;
}

/* ---- walking ---- */

static struct node *expr;

struct level {
    int fd;
    dev_t dev;
    ino_t ino;
    bool pinned;
};
static struct level *lv;
static size_t depth, lvcap, nopen, maxopen = 32;

static void push_level(int fd, const struct stat *st)
{
    if (depth == lvcap) {
        lvcap = lvcap ? lvcap * 2 : 16;
        lv = xrealloc(lv, lvcap * sizeof *lv);
    }
    lv[depth++] = (struct level){ fd, st->st_dev, st->st_ino, false };
    if (++nopen > maxopen)
        for (size_t i = 0; i + 1 < depth; i++)
            if (lv[i].fd >= 0 && !lv[i].pinned) {
                close(lv[i].fd);
                lv[i].fd = -1;
                nopen--;
                break;
            }
}

static bool pop_level(void)
{
    struct level *top = &lv[depth - 1];
    bool ok = true;
    if (depth >= 2 && lv[depth - 2].fd < 0) {
        struct level *b = &lv[depth - 2];
        struct stat st;
        int fd = openat(top->fd, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd >= 0 && fstat(fd, &st) == 0 && st.st_dev == b->dev && st.st_ino == b->ino) {
            b->fd = fd;
            nopen++;
        } else {
            if (fd >= 0)
                close(fd);
            ok = false;
        }
    }
    close(top->fd);
    nopen--;
    depth--;
    return ok;
}

static size_t path_push(const char *name)
{
    size_t old = pathlen, n = strlen(name);
    bool slash = pathlen > 0 && path[pathlen - 1] != '/';
    if (pathlen + n + 2 > pathcap) {
        pathcap = (pathlen + n + 2) * 2;
        path = xrealloc(path, pathcap);
    }
    if (slash)
        path[pathlen++] = '/';
    memcpy(path + pathlen, name, n + 1);
    pathlen += n;
    return old;
}

static void path_pop(size_t old)
{
    pathlen = old;
    path[pathlen] = '\0';
}

static void err(int e)
{
    fprintf(stderr, "find: %s: %s\n", path, strerror(e));
    status = 1;
}

static char **read_names(int fd, size_t *count, int *error)
{
    int fd2 = dup(fd);
    DIR *d = fd2 >= 0 ? fdopendir(fd2) : NULL;
    char **names = NULL;
    size_t n = 0, cap = 0;
    *error = 0;
    *count = 0;
    if (!d) {
        *error = errno;
        if (fd2 >= 0)
            close(fd2);
        return NULL;
    }
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(d);
        if (!e) {
            *error = errno;
            break;
        }
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            names = xrealloc(names, cap * sizeof *names);
        }
        names[n++] = xstrdup(e->d_name);
    }
    closedir(d);
    *count = n;
    return names;
}

static dev_t root_dev;

/* visit the entry name of the directory dirfd (the current pathname) */
static void visit(int dirfd, const char *name, bool operand)
{
    struct stat st;
    bool followed = false;
    bool want = follow == 'L' || (follow == 'H' && operand);
    if (want && fstatat(dirfd, name, &st, 0) == 0)
        followed = true;
    else if (want && errno != ENOENT && errno != ENOTDIR) {
        err(errno);             /* a loop of links, say: not a missing file */
        return;
    } else if (fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        err(errno);
        return;
    }
    if (operand)
        root_dev = st.st_dev;
    if (mount && st.st_dev != root_dev)
        return;
    bool isdir = S_ISDIR(st.st_mode);
    prune = false;
    if (!depth_mode)
        eval(expr, &st);
    bool descend = isdir && (depth_mode || !prune) && !(xdev && st.st_dev != root_dev);
    if (descend) {
        for (size_t i = 0; i < depth; i++)
            if (lv[i].dev == st.st_dev && lv[i].ino == st.st_ino) {
                fprintf(stderr, "find: %s: a directory loop (not entered)\n", path);
                status = 1;
                descend = false;
                break;
            }
    }
    if (descend) {
        int fd = openat(dirfd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | (followed ? 0 : O_NOFOLLOW));
        struct stat fst;
        if (fd < 0) {
            err(errno);
        } else if (fstat(fd, &fst) != 0 || fst.st_dev != st.st_dev || fst.st_ino != st.st_ino) {
            fprintf(stderr, "find: %s: changed while being searched\n", path);
            status = 1;
            close(fd);
        } else {
            size_t n;
            int rerr;
            char **names = read_names(fd, &n, &rerr);
            /* the level above one reached through a link cannot be found as ".." */
            if (followed && S_ISLNK(st.st_mode) == 0 && depth > 0) {
                struct stat lst;
                if (fstatat(dirfd, name, &lst, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(lst.st_mode))
                    lv[depth - 1].pinned = true;
            }
            push_level(fd, &fst);
            if (rerr)
                err(rerr);
            for (size_t i = 0; i < n; i++) {
                int me = lv[depth - 1].fd;
                size_t old = path_push(names[i]);
                if (me >= 0)
                    visit(me, names[i], false);
                path_pop(old);
                free(names[i]);
            }
            free(names);
            if (!pop_level()) {
                fprintf(stderr, "find: %s: the directory above it has moved\n", path);
                status = 1;
            }
        }
    }
    if (depth_mode)
        eval(expr, &st);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    now = time(NULL);
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (a[0] != '-' || a[1] == '\0' || strspn(a + 1, "HL") != strlen(a + 1))
            break;
        for (const char *p = a + 1; *p; p++)
            follow = *p;
    }
    int first_path = i;
    while (i < argc && argv[i][0] != '-' && strcmp(argv[i], "!") != 0 && strcmp(argv[i], "(") != 0)
        i++;
    int npaths = i - first_path;
    av = argv;
    ac = argc;
    ap = i;
    if (ap < ac) {
        expr = parse_or();
        if (ap < ac)
            syntax("%s: unexpected", av[ap]);
    }
    if (!has_action) {
        struct node *p = new_node(K_PRINT);
        if (expr) {
            struct node *a = new_node(K_AND);
            a->l = expr;
            a->r = p;
            expr = a;
        } else
            expr = p;
    }
    arg_max = sysconf(_SC_ARG_MAX);
    if (arg_max <= 0)
        arg_max = 4096 * 32;
    for (char **e = environ; *e; e++)
        arg_max -= (long)(strlen(*e) + 1 + sizeof(char *));
    arg_max -= 4096;
    if (arg_max > 1 << 20)
        arg_max = 1 << 20;      /* sets of a reasonable size */
    if (arg_max < 4096)
        arg_max = 4096;
    long om = sysconf(_SC_OPEN_MAX);
    if (om > 0 && (om - 10) / 2 < (long)maxopen)
        maxopen = om > 14 ? (size_t)(om - 10) / 2 : 2;

    static char *dot[] = { "." };
    char **paths = npaths ? argv + first_path : dot;
    if (!npaths)
        npaths = 1;
    for (int k = 0; k < npaths; k++) {
        pathlen = 0;
        path_push(paths[k]);
        /* the operand as given, trailing slashes and all */
        size_t l = strlen(paths[k]);
        if (l + 1 > pathcap) {
            pathcap = l + 1;
            path = xrealloc(path, pathcap);
        }
        memcpy(path, paths[k], l + 1);
        pathlen = l;
        visit(AT_FDCWD, paths[k], true);
    }
    for (size_t k = 0; k < nplus; k++)
        exec_flush(plus_execs[k]);
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "find: write error: %s\n", strerror(errno));
        status = 1;
    }
    return status;
}
