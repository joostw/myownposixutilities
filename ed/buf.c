/*
 * buf.c - the edit buffer: lines, marks, undo, and reading and writing
 * files and commands.
 */
#include "ed.h"

Line **lines;
long nlines, cur;
int modified;
Line *marks[26];
char *filename;
static long cap;

/* the state before the most recent change, for u */
static Line **u_lines;
static long u_n, u_cur;
static int u_valid;
/* the state before the command now running */
static Line **p_lines;
static long p_n, p_cur;
static int p_depth, p_touched;

Line *line_new(const char *s, size_t len)
{
    Line *l = xmalloc(sizeof *l);
    l->s = xmalloc(len + 1);
    memcpy(l->s, s, len);
    l->s[len] = 0;
    l->len = len;
    l->gmark = 0;
    return l;
}

static void ensure(long n)
{
    if (n + 2 > cap) {
        cap = (n + 2) * 2;
        lines = xrealloc(lines, (size_t)cap * sizeof(Line *));
    }
}

void buf_insert(long after, Line **v, long n)
{
    ensure(nlines + n);
    memmove(lines + after + 1 + n, lines + after + 1, (size_t)(nlines - after) * sizeof(Line *));
    memcpy(lines + after + 1, v, (size_t)n * sizeof(Line *));
    nlines += n;
    changed();
}

void buf_delete(long from, long to)
{
    memmove(lines + from, lines + to + 1, (size_t)(nlines - to) * sizeof(Line *));
    nlines -= to - from + 1;
    changed();
}

void buf_replace(long i, Line *l)
{
    lines[i] = l;
    changed();
}

long line_index(Line *l)
{
    for (long i = 1; i <= nlines; i++)
        if (lines[i] == l)
            return i;
    return 0;
}

void changed(void)
{
    modified = 1;
    p_touched = 1;
}

void buf_clear_all(void)
{
    nlines = 0;
    cur = 0;
    ensure(0);
    memset(marks, 0, sizeof marks);
    free(u_lines);
    u_lines = NULL;
    u_valid = 0;
}

/* ---------- undo ---------- */

static Line **copy_lines(long *n)
{
    Line **c = xmalloc((size_t)(nlines + 2) * sizeof(Line *));
    memcpy(c, lines, (size_t)(nlines + 1) * sizeof(Line *));
    *n = nlines;
    return c;
}

void undo_begin(void)
{
    if (p_depth++ > 0)
        return;
    ensure(nlines);
    p_lines = copy_lines(&p_n);
    p_cur = cur;
    p_touched = 0;
}

void undo_end_global(void)
{
    /* a global command that changed nothing leaves nothing to undo */
    if (p_depth == 1 && !p_touched) {
        free(u_lines);
        u_lines = NULL;
        u_valid = 0;
    }
    undo_end();
}

void undo_end(void)
{
    if (--p_depth > 0)
        return;
    if (p_touched) {
        free(u_lines);
        u_lines = p_lines;
        u_n = p_n;
        u_cur = p_cur;
        u_valid = 1;
    } else
        free(p_lines);
    p_lines = NULL;
}

void undo_forget(void)
{
    free(u_lines);
    u_lines = NULL;
    u_valid = 0;
}

int undo(void)
{
    Line **t;
    long tn, tc;
    if (!u_valid)
        return fail("Nothing to undo");
    t = copy_lines(&tn);
    tc = cur;
    ensure(u_n);
    memcpy(lines, u_lines, (size_t)(u_n + 1) * sizeof(Line *));
    nlines = u_n;
    cur = u_cur;
    free(u_lines);
    u_lines = t;
    u_n = tn;
    u_cur = tc;
    modified = 1;
    return 0;
}

/* ---------- files ---------- */

static FILE *open_cmd(const char *cmd, const char *mode, pid_t *pid)
{
    int pfd[2];
    int reading = mode[0] == 'r';
    fflush(stdout);
    if (pipe(pfd) < 0)
        return NULL;
    *pid = fork();
    if (*pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return NULL;
    }
    if (*pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        if (reading) {
            dup2(pfd[1], 1);
        } else {
            dup2(pfd[0], 0);
        }
        close(pfd[0]);
        close(pfd[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    if (reading) {
        close(pfd[1]);
        return fdopen(pfd[0], "r");
    }
    close(pfd[0]);
    return fdopen(pfd[1], "w");
}

static void close_cmd(FILE *fp, pid_t pid)
{
    fclose(fp);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
        ;
}

long read_file(const char *name, long after, int is_cmd)
{
    FILE *fp;
    pid_t pid = 0;
    Buf b = { 0 };
    char tmp[65536];
    size_t n;
    long count = 0, nl = 0;
    Line **v = NULL;
    long nv = 0, capv = 0;
    if (is_cmd)
        fp = open_cmd(name, "r", &pid);
    else {
        struct stat st;
        if (stat(name, &st) == 0 && S_ISDIR(st.st_mode)) {
            fprintf(stderr, "%s: Is a directory\n", name);
            return fail("Cannot open input file");
        }
        fp = fopen(name, "r");
    }
    if (!fp) {
        if (!is_cmd)
            fprintf(stderr, "%s: %s\n", name, strerror(errno));
        return fail(is_cmd ? "Cannot create shell process" : "Cannot open input file");
    }
    while ((n = fread(tmp, 1, sizeof tmp, fp)) > 0) {
        buf_add(&b, tmp, n);
        count += (long)n;
    }
    if (ferror(fp) && !is_cmd) {
        fprintf(stderr, "%s: %s\n", name, strerror(errno));
        fclose(fp);
        free(b.s);
        return fail("Cannot read input file");
    }
    if (is_cmd)
        close_cmd(fp, pid);
    else
        fclose(fp);
    for (size_t i = 0, st = 0; i < b.len; i++) {
        if (b.s[i] == '\n' || i + 1 == b.len) {
            size_t end = b.s[i] == '\n' ? i : i + 1;
            if (nv + 1 > capv) {
                capv = capv ? capv * 2 : 1024;
                v = xrealloc(v, (size_t)capv * sizeof(Line *));
            }
            v[nv++] = line_new(b.s + st, end - st);
            st = i + 1;
            nl++;
        }
    }
    if (nv)
        buf_insert(after, v, nv);
    cur = after + nv;
    free(v);
    free(b.s);
    return count;
}

long write_file(const char *name, const char *mode, long from, long to, int is_cmd)
{
    FILE *fp;
    pid_t pid = 0;
    long count = 0;
    if (is_cmd)
        fp = open_cmd(name, "w", &pid);
    else
        fp = fopen(name, mode);
    if (!fp) {
        if (!is_cmd)
            fprintf(stderr, "%s: %s\n", name, strerror(errno));
        return fail(is_cmd ? "Cannot create shell process" : "Cannot open output file");
    }
    if (is_cmd)
        signal(SIGPIPE, SIG_IGN);
    for (long i = from; i <= to && i >= 1; i++) {
        Line *l = lines[i];
        if (fwrite(l->s, 1, l->len, fp) != l->len || putc('\n', fp) == EOF)
            break;
        count += (long)l->len + 1;
    }
    if (is_cmd) {
        close_cmd(fp, pid);
        signal(SIGPIPE, SIG_DFL);
        return count;
    }
    if (ferror(fp) | (fclose(fp) != 0)) {
        fprintf(stderr, "%s: %s\n", name, strerror(errno));
        return fail("Cannot write output file");
    }
    return count;
}
