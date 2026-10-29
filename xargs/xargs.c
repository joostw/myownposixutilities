/*
 * xargs.c - xargs: construct argument lists and invoke utility
 * (POSIX.1-2024, XCU xargs)
 *
 * Synopsis:
 *   xargs [-prtx] [-E eofstr|-0] [-I replstr|-L number|-n number]
 *         [-s size] [utility [argument...]]
 *
 * The options are read by hand, not with getopt(): scanning stops at the
 * first operand, so the utility's own options are never taken for xargs's.
 * The command line length counts the utility, its arguments and a NUL for
 * each; it is kept below the -s size, and below {ARG_MAX} - 2048 less the
 * size of the environment.
 *
 * Choices where the standard leaves room:
 *  - Without -E, there is no logical end-of-file string (_ is an ordinary
 *    argument).
 *  - The default size is the smaller of 128 KiB and what {ARG_MAX} allows;
 *    a larger -s is reduced to what {ARG_MAX} allows.
 *  - -I, -L and -n: the last one given is used.  With -I, empty lines are
 *    skipped, and a constructed argument may be of any length (within the
 *    size).
 *  - -L follows the text of POSIX.1-2024: each input line is one argument
 *    (its quotes and backslashes are processed, its blanks are kept), a
 *    line ending in an unescaped blank continues on the next non-empty
 *    line (the blanks and the newline removed), and empty lines are
 *    skipped.  (Historical implementations split such lines at blanks.)
 *  - The utility's standard input is /dev/null (xargs's own is the list of
 *    arguments).
 *  - Exit status: 124 if an invocation exits with 255 (xargs stops), 125
 *    if one is killed by a signal (it stops too), 123 if one exits with
 *    another nonzero status, 126 and 127 as the standard says, 1 for
 *    errors of xargs itself (an argument too long with -x; a quote without
 *    its end, after the arguments before it have been used).
 *  - -0: bytes after the last null byte are ignored, with a warning.
 *  - -t writes the utility and its arguments separated by single spaces;
 *    -p adds "?..." and reads the answer from /dev/tty.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <regex.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static const char *progname = "xargs";

/* ---- executing the utility (as execvp(), with the statuses of XCU 2.9.1.1) ---- */

/* exec file with argv; on ENOEXEC, as a script of sh */
static void try_exec(const char *file, char **argv)
{
    execv(file, argv);
    if (errno != ENOEXEC)
        return;
    int n = 0;
    while (argv[n])
        n++;
    char **sargv = malloc((size_t)(n + 2) * sizeof *sargv);
    if (!sargv)
        return;
    sargv[0] = "sh";
    sargv[1] = (char *)file;
    for (int i = 1; i <= n; i++)
        sargv[i + 1] = argv[i];
    /* sh from the standard PATH */
    size_t len = confstr(_CS_PATH, NULL, 0);
    char *path = len ? malloc(len) : NULL;
    if (path) {
        confstr(_CS_PATH, path, len);
        for (char *dir = strtok(path, ":"); dir; dir = strtok(NULL, ":")) {
            char sh[4096];
            snprintf(sh, sizeof sh, "%s/sh", dir);
            execv(sh, sargv);
        }
        free(path);
    }
    execv("/bin/sh", sargv);
    free(sargv);
    errno = ENOEXEC;
}

/* execute argv[0], searched for in PATH unless it has a '/'; on failure,
   a diagnostic and the status: 127 if it is not found, 126 if it is found
   but cannot be executed */
static int exec_utility(char **argv)
{
    const char *utility = argv[0];
    int saved = ENOENT;
    if (strchr(utility, '/')) {
        try_exec(utility, argv);
        saved = errno;
    } else {
        const char *path = getenv("PATH");
        char *defpath = NULL;
        if (!path) {
            size_t len = confstr(_CS_PATH, NULL, 0);
            if (len && (defpath = malloc(len)))
                confstr(_CS_PATH, defpath, len);
            path = defpath ? defpath : "/usr/bin:/bin";
        }
        size_t ul = strlen(utility);
        for (const char *p = path;;) {
            const char *colon = strchr(p, ':');
            size_t dl = colon ? (size_t)(colon - p) : strlen(p);
            char *file = malloc(dl + ul + 2);
            if (!file) {
                saved = ENOMEM;
                break;
            }
            if (dl == 0)
                memcpy(file, utility, ul + 1);  /* an empty entry: the current directory */
            else {
                memcpy(file, p, dl);
                file[dl] = '/';
                memcpy(file + dl + 1, utility, ul + 1);
            }
            try_exec(file, argv);
            int e = errno;
            /* a file that is there but cannot be run decides the status; a
               directory that cannot be searched does not */
            struct stat st;
            if (e != ENOENT && e != ENOTDIR && !(e == EACCES && stat(file, &st) != 0))
                saved = e;
            free(file);
            if (!colon)
                break;
            p = colon + 1;
        }
        free(defpath);
    }
    if (saved == ENOENT || saved == ENOTDIR) {
        fprintf(stderr, "%s: %s: not found\n", progname, utility);
        return 127;
    }
    fprintf(stderr, "%s: %s: %s\n", progname, utility, strerror(saved));
    return 126;
}

static bool opt_p, opt_r, opt_t, opt_x, opt_0;
static const char *eofstr;      /* NULL: none */
static const char *replstr;     /* -I */
static long lines_per;          /* -L */
static long max_args;           /* -n */
static long size_limit;
static int status;

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "xargs: out of memory\n");
        exit(1);
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    if (!(p = realloc(p, n ? n : 1))) {
        fprintf(stderr, "xargs: out of memory\n");
        exit(1);
    }
    return p;
}

static void usage(void)
{
    fprintf(stderr, "usage: xargs [-prtx] [-E eofstr|-0] [-I replstr|-L number|-n number]\n"
                    "             [-s size] [utility [argument...]]\n");
    exit(1);
}

/* ---- reading the arguments ---- */

static int peekc = -2;

static int getch(void)
{
    if (peekc != -2) {
        int c = peekc;
        peekc = -2;
        return c;
    }
    return getchar();
}

static void ungetch(int c)
{
    peekc = c;
}

struct buf {
    char *s;
    size_t len, cap;
};

static void bput(struct buf *b, int c)
{
    if (b->len + 1 >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 128;
        b->s = xrealloc(b->s, b->cap);
    }
    b->s[b->len++] = (char)c;
    b->s[b->len] = '\0';
}

static bool at_eof;             /* the end of the input, or the logical end */
static bool quote_error;        /* the input ended in a quote: stop, status 1 */

/* the next argument; NULL at the end.  lines: -I and -L, where a line is
   one argument */
static char *next_arg(bool lines)
{
    static struct buf b;
    if (at_eof)
        return NULL;
    b.len = 0;
    if (b.s)
        b.s[0] = '\0';
    if (opt_0) {
        int c;
        while ((c = getch()) != EOF && c != '\0')
            bput(&b, c);
        if (c == EOF) {
            at_eof = true;
            if (b.len > 0)
                fprintf(stderr, "xargs: the input does not end with a null byte; the last %zu bytes are ignored\n", b.len);
            return NULL;
        }
        bput(&b, 0);
        b.len--;
        return b.s;
    }
    for (;;) {
        int c;
        bool any = false;       /* something (perhaps empty quotes) was read */
        /* skip delimiters */
        if (!lines) {
            while ((c = getch()) == ' ' || c == '\t' || c == '\n')
                ;
        } else {
            /* -I: blanks at the start of a line are skipped; -L too; empty lines */
            while ((c = getch()) == ' ' || c == '\t' || c == '\n')
                ;
        }
        if (c == EOF) {
            at_eof = true;
            return NULL;
        }
        ungetch(c);
        bool trailing_blank = false;
        size_t blank_start = 0;
        for (;;) {
            c = getch();
            if (c == EOF)
                break;
            if (c == '\n') {
                if (lines && lines_per && trailing_blank) {
                    /* continuation: drop the blanks; go on at the next non-empty line */
                    b.len = blank_start;
                    b.s[b.len] = '\0';
                    trailing_blank = false;
                    int d;
                    while ((d = getch()) == '\n')
                        ;
                    if (d == EOF)
                        break;
                    ungetch(d);
                    continue;
                }
                break;
            }
            if (!lines && (c == ' ' || c == '\t'))
                break;
            if (c == ' ' || c == '\t') {
                if (!trailing_blank) {
                    trailing_blank = true;
                    blank_start = b.len;
                }
                bput(&b, c);
                any = true;
                continue;
            }
            trailing_blank = false;
            any = true;
            if (c == '\\') {
                int d = getch();
                if (d == EOF)
                    break;
                bput(&b, d);
                continue;
            }
            if (c == '"' || c == '\'') {
                int q = c;
                for (;;) {
                    int d = getch();
                    if (d == EOF || d == '\n') {
                        fprintf(stderr, "xargs: a %s quote without its end\n", q == '"' ? "double" : "single");
                        quote_error = at_eof = true;
                        return NULL;    /* what came before it is still used */
                    }
                    if (d == q)
                        break;
                    bput(&b, d);
                }
                continue;
            }
            bput(&b, c);
        }
        if (c == EOF)
            at_eof = true;
        if (!b.s)
            bput(&b, 0), b.len = 0;
        if (eofstr && strcmp(b.s, eofstr) == 0) {
            at_eof = true;
            return NULL;
        }
        if (lines && b.len == 0 && !any) {
            if (at_eof)
                return NULL;
            continue;
        }
        return b.s;
    }
}

/* ---- running ---- */

static FILE *tty;

static bool yes(void)
{
    static regex_t re;
    static int ready;
    if (!ready) {
        const char *y = nl_langinfo(YESEXPR);
        ready = (y && *y && regcomp(&re, y, REG_EXTENDED | REG_NOSUB) == 0) ? 1 : -1;
    }
    if (!tty && !(tty = fopen("/dev/tty", "r"))) {
        fprintf(stderr, "xargs: /dev/tty: %s\n", strerror(errno));
        exit(1);
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, tty);
    bool r = false;
    if (n > 0) {
        if (line[n - 1] == '\n')
            line[n - 1] = '\0';
        r = ready == 1 ? regexec(&re, line, 0, NULL, 0) == 0 : (line[0] == 'y' || line[0] == 'Y');
    }
    free(line);
    return r;
}

static void run(char **argv)
{
    if (opt_t || opt_p) {
        for (int i = 0; argv[i]; i++)
            fprintf(stderr, "%s%s", i ? " " : "", argv[i]);
        if (opt_p) {
            fputs(" ?...", stderr);
            fflush(stderr);
            if (!yes())
                return;
        } else
            fputc('\n', stderr);
    }
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "xargs: fork: %s\n", strerror(errno));
        exit(1);
    }
    if (pid == 0) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd >= 0 && fd != 0) {
            dup2(fd, 0);
            close(fd);
        }
        _exit(exec_utility(argv));
    }
    int ws;
    while (waitpid(pid, &ws, 0) < 0)
        if (errno != EINTR) {
            fprintf(stderr, "xargs: wait: %s\n", strerror(errno));
            exit(1);
        }
    if (WIFSIGNALED(ws)) {
        fprintf(stderr, "xargs: %s: killed by signal %d\n", argv[0], WTERMSIG(ws));
        exit(125);
    }
    int r = WEXITSTATUS(ws);
    if (r == 255) {
        fprintf(stderr, "xargs: %s: exited with status 255; stopping\n", argv[0]);
        exit(124);
    }
    if (r == 126 || r == 127)
        exit(r);
    if (r != 0)
        status = 123;
}

static long number(const char *s, const char *opt)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (!isdigit((unsigned char)*s) || *end || errno || v <= 0) {
        fprintf(stderr, "xargs: -%s %s: not a positive number\n", opt, s);
        exit(1);
    }
    return v;
}

/* replace each replstr in s by a; malloc'ed */
static char *subst(const char *s, const char *a)
{
    size_t rl = strlen(replstr), al = strlen(a), n = 0;
    if (rl == 0)
        return strcpy(xmalloc(strlen(s) + 1), s);
    for (const char *p = s; (p = strstr(p, replstr)); p += rl)
        n++;
    char *r = xmalloc(strlen(s) + n * al + 1), *q = r;
    for (const char *p = s; *p;) {
        if (strncmp(p, replstr, rl) == 0) {
            memcpy(q, a, al);
            q += al;
            p += rl;
        } else
            *q++ = *p++;
    }
    *q = '\0';
    return r;
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    long size_opt = 0;
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (a[0] != '-' || a[1] == '\0')
            break;
        for (const char *p = a + 1; *p; p++) {
            const char *arg = NULL;
            if (strchr("EILns", *p)) {
                if (p[1])
                    arg = p + 1;
                else if (i + 1 < argc)
                    arg = argv[++i];
                else {
                    fprintf(stderr, "xargs: -%c needs an argument\n", *p);
                    usage();
                }
            }
            switch (*p) {
            case 'p': opt_p = true; break;
            case 'r': opt_r = true; break;
            case 't': opt_t = true; break;
            case 'x': opt_x = true; break;
            case '0': opt_0 = true; break;
            case 'E': eofstr = *arg ? arg : NULL; break;
            case 'I': replstr = arg; lines_per = 0; max_args = 0; break;
            case 'L': lines_per = number(arg, "L"); replstr = NULL; max_args = 0; break;
            case 'n': max_args = number(arg, "n"); replstr = NULL; lines_per = 0; break;
            case 's': size_opt = number(arg, "s"); break;
            default:
                fprintf(stderr, "xargs: -%c: unknown option\n", *p);
                usage();
            }
            if (arg)
                break;
        }
    }
    if (opt_0)
        eofstr = NULL;
    if (replstr)
        opt_x = true;

    /* the size allowed by ARG_MAX */
    long argmax = sysconf(_SC_ARG_MAX);
    if (argmax <= 0)
        argmax = 4096;
    long envsize = 0;
    for (char **e = environ; *e; e++)
        envsize += (long)(strlen(*e) + 1 + sizeof(char *));
    long allowed = argmax - 2048 - envsize;
    if (allowed < 256)
        allowed = 256;
    size_limit = size_opt ? size_opt : 131072;
    if (size_limit > allowed)
        size_limit = allowed;

    char **init = argv + i;
    int ninit = argc - i;
    static char *echo_argv[] = { "echo", NULL };
    if (ninit == 0) {
        init = echo_argv;
        ninit = 1;
    }
    long base = 0;
    for (int k = 0; k < ninit; k++)
        base += (long)strlen(init[k]) + 1;

    if (replstr) {
        bool ran = false;
        char *a;
        while ((a = next_arg(true))) {
            char **cmd = xmalloc((size_t)(ninit + 1) * sizeof *cmd);
            long len = 0;
            for (int k = 0; k < ninit; k++) {
                cmd[k] = subst(init[k], a);
                len += (long)strlen(cmd[k]) + 1;
            }
            cmd[ninit] = NULL;
            if (len > size_limit) {
                fprintf(stderr, "xargs: the command line is too long\n");
                exit(1);
            }
            run(cmd);
            ran = true;
            for (int k = 0; k < ninit; k++)
                free(cmd[k]);
            free(cmd);
        }
        if (!ran && !opt_r) {
            /* no input: the utility once, with the arguments as they are */
            char **cmd = xmalloc((size_t)(ninit + 1) * sizeof *cmd);
            for (int k = 0; k < ninit; k++)
                cmd[k] = init[k];
            cmd[ninit] = NULL;
            run(cmd);
            free(cmd);
        }
        return quote_error ? 1 : status;
    }

    if (base > size_limit) {
        fprintf(stderr, "xargs: the utility and its arguments are longer than the size\n");
        exit(1);
    }
    char **cmd = NULL;
    size_t cap = 0;
    bool ran = false;
    char *pending = NULL;       /* an argument that did not fit the last line */
    for (;;) {
        size_t n = (size_t)ninit;
        long len = base;
        if (cap < n + 1) {
            cap = n + 64;
            cmd = xrealloc(cmd, cap * sizeof *cmd);
        }
        for (int k = 0; k < ninit; k++)
            cmd[k] = init[k];
        long count = 0;
        long limit = lines_per ? lines_per : max_args;
        for (;;) {
            if (limit && count >= limit)
                break;
            char *a;
            if (pending) {
                a = pending;
                pending = NULL;
            } else {
                char *t = next_arg(lines_per > 0);
                if (!t)
                    break;
                a = strcpy(xmalloc(strlen(t) + 1), t);
            }
            long al = (long)strlen(a) + 1;
            if (len + al > size_limit) {
                if (count == 0 || opt_x) {
                    fprintf(stderr, "xargs: the command line is too long\n");
                    exit(1);
                }
                pending = a;
                break;
            }
            if (n + 2 > cap) {
                cap *= 2;
                cmd = xrealloc(cmd, cap * sizeof *cmd);
            }
            cmd[n++] = a;
            len += al;
            count++;
        }
        if (count == 0 && !pending) {
            if (!ran && !opt_r) {
                cmd[n] = NULL;
                run(cmd);
            }
            break;
        }
        if (opt_x && limit && count < limit && pending) {
            fprintf(stderr, "xargs: the command line is too long\n");
            exit(1);
        }
        cmd[n] = NULL;
        run(cmd);
        ran = true;
        for (size_t k = (size_t)ninit; k < n; k++)
            free(cmd[k]);
        if (at_eof && !pending)
            break;
    }
    free(cmd);
    return quote_error ? 1 : status;
}
