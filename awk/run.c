/*
 * run.c - the interpreter: values, fields, records, I/O, builtins.
 */
#include "awk.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* state                                                              */
/* ------------------------------------------------------------------ */

enum { PH_INIT, PH_BEGIN, PH_MAIN, PH_END };
static int phase = PH_INIT;
static int exit_code = 0;

enum { J_NEXT = 1, J_NEXTFILE, J_EXIT };
static jmp_buf top_jb;

enum { X_NORMAL, X_BREAK, X_CONTINUE, X_RETURN };
static Val retval;

static Val *cur_locals;
static Buf obuf_pool[16];
static int obuf_depth;
typedef struct Frame { Val *locals; int n; } Frame;
static Frame *fstack;
static int fdepth, fcap;

static Str *default_fmt;
static Val uninit_val;

static double rand_seed = 0;

/* ------------------------------------------------------------------ */
/* value conversion                                                   */
/* ------------------------------------------------------------------ */

static Str *convfmt(void)
{
    Val *c = gvars[V_CONVFMT];
    return (c->t == T_STR || c->t == T_STRNUM) ? c->s : default_fmt;
}

static Str *ofmt(void)
{
    Val *c = gvars[V_OFMT];
    return (c->t == T_STR || c->t == T_STRNUM) ? c->s : default_fmt;
}

Str *val_tostr(Val *v)
{
    switch (v->t) {
    case T_STR: case T_STRNUM:
        return str_ref(v->s);
    case T_NUM:
        return num2str(v->n, convfmt());
    case T_ARRAY:
        fatal("attempt to use array in a scalar context");
    default:
        return str_ref(str_empty);
    }
}

static Str *val_tostr_out(Val *v)
{
    if (v->t == T_NUM)
        return num2str(v->n, ofmt());
    return val_tostr(v);
}

static double tonum(Val *v)
{
    switch (v->t) {
    case T_NUM: case T_STRNUM:
        return v->n;
    case T_STR:
        return str2num(v->s->s, v->s->len);
    case T_ARRAY:
        fatal("attempt to use array in a scalar context");
    default:
        return 0;
    }
}

static int tobool(Val *v)
{
    switch (v->t) {
    case T_NUM: case T_STRNUM:
        return v->n != 0;
    case T_STR:
        return v->s->len != 0;
    default:
        return 0;
    }
}

static Str *gstr(int slot)
{
    return val_tostr(gvars[slot]);
}

static void set_gnum(int slot, double d)
{
    val_free(gvars[slot]);
    *gvars[slot] = val_num(d);
}

/* ------------------------------------------------------------------ */
/* frames                                                             */
/* ------------------------------------------------------------------ */

static void free_locals(Val *loc, int n)
{
    int i;
    for (i = 0; i < n; i++)
        val_free(&loc[i]);
    free(loc);
}

static void push_frame(Val *loc, int n)
{
    if (fdepth == fcap) {
        fcap = fcap ? fcap * 2 : 64;
        fstack = xrealloc(fstack, (size_t)fcap * sizeof *fstack);
    }
    fstack[fdepth].locals = loc;
    fstack[fdepth].n = n;
    fdepth++;
}

static void pop_frame(void)
{
    fdepth--;
    free_locals(fstack[fdepth].locals, fstack[fdepth].n);
    cur_locals = fdepth ? fstack[fdepth - 1].locals : NULL;
}

static void unwind(void)
{
    while (fdepth > 0)
        pop_frame();
    cur_locals = NULL;
    obuf_depth = 0;
}

/* ------------------------------------------------------------------ */
/* variables and arrays                                               */
/* ------------------------------------------------------------------ */

static const char *var_name(Node *n)
{
    return n->str ? n->str->s : "?";
}

static Val *var_cell(Node *n)
{
    return n->type == N_GVAR ? gvars[n->idx] : &cur_locals[n->idx];
}

static Val *deref(Val *c)
{
    while (c->t == T_REF)
        c = c->u.ref;
    return c;
}

static Array *cell_array(Val *c, const char *name)
{
    if (c->t == T_ARRAY)
        return c->u.a;
    if (c->t == T_UNINIT) {
        c->t = T_ARRAY;
        c->own = 1;
        c->u.a = array_new();
        return c->u.a;
    }
    if (c->t == T_REF) {
        Val *t = deref(c);
        Array *a = cell_array(t, name);
        c->t = T_ARRAY;
        c->own = 0;
        c->u.a = a;
        return a;
    }
    fatal("can't use scalar %s as array", name);
}

static Array *node_array(Node *n)
{
    if (n->type != N_GVAR && n->type != N_LVAR)
        fatal("array name expected");
    if (n->type == N_GVAR && n->idx < V_NSPECIAL && n->idx != V_ENVIRON && n->idx != V_ARGV)
        fatal("can't use special variable %s as array", var_name(n));
    return cell_array(var_cell(n), var_name(n));
}

/* ------------------------------------------------------------------ */
/* fields and records                                                 */
/* ------------------------------------------------------------------ */

static Val *fld;
static int fldcap;
static int nf;
static int fields_valid = 1;
static int rec_dirty;
static Str *rec_fs;

static void fld_reserve(int n)
{
    if (n + 1 >= fldcap) {
        int nc = fldcap ? fldcap : 64, i;
        while (nc <= n + 1)
            nc *= 2;
        fld = xrealloc(fld, (size_t)nc * sizeof *fld);
        for (i = fldcap; i < nc; i++)
            fld[i] = val_uninit();
        fldcap = nc;
    }
}

typedef void (*addfn)(void *ctx, const char *s, size_t n);

/* split s per awk field-splitting rules with separator fs (string) or re */
static void split_string(const char *s, size_t len, Str *fs, Regex *re, int paragraph,
                         addfn add, void *ctx)
{
    size_t i, p;
    if (len == 0)
        return;
    if (!re && fs->len == 1 && fs->s[0] == ' ') {
        i = 0;
        for (;;) {
            while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n'))
                i++;
            if (i >= len)
                break;
            p = i;
            while (i < len && s[i] != ' ' && s[i] != '\t' && s[i] != '\n')
                i++;
            add(ctx, s + p, i - p);
        }
        return;
    }
    if (!re && fs->len == 0) {
        /* unspecified by POSIX: split into characters */
        for (i = 0; i < len; ) {
            size_t cl = mb_charlen(s + i, len - i);
            add(ctx, s + i, cl);
            i += cl;
        }
        return;
    }
    if (!re && fs->len == 1) {
        int c = (unsigned char)fs->s[0];
        p = 0;
        for (i = 0; i < len; i++) {
            if ((unsigned char)s[i] == c || (paragraph && s[i] == '\n')) {
                add(ctx, s + p, i - p);
                p = i + 1;
            }
        }
        add(ctx, s + p, len - p);
        return;
    }
    if (!re)
        re = re_dynamic_fs(fs, paragraph);
    p = 0;
    for (;;) {
        size_t q = p, so, eo, ms = 0, me = 0;
        int found = 0;
        while (q <= len && re_exec(re, s + q, q > 0, &so, &eo)) {
            ms = q + so;
            me = q + eo;
            if (me > ms) {
                found = 1;
                break;
            }
            if (ms >= len)
                break;
            q = ms + mb_charlen(s + ms, len - ms);
        }
        if (!found) {
            add(ctx, s + p, len - p);
            break;
        }
        add(ctx, s + p, ms - p);
        p = me;
    }
}

static void add_field(void *ctx, const char *s, size_t n)
{
    (void)ctx;
    fld_reserve(nf + 1);
    nf++;
    if (n == 0)
        fld[nf] = val_uninit();
    else
        fld[nf] = val_strnum(str_new(s, n));
}

static int paragraph_mode(void)
{
    Val *rs = gvars[V_RS];
    return (rs->t == T_STR || rs->t == T_STRNUM) && rs->s->len == 0;
}

static void ensure_split(void)
{
    int i;
    Str *s;
    if (fields_valid)
        return;
    fields_valid = 1;
    for (i = 1; i <= nf; i++)
        val_free(&fld[i]);
    nf = 0;
    s = val_tostr(&fld[0]);
    split_string(s->s, s->len, rec_fs, NULL, paragraph_mode(), add_field, NULL);
    str_unref(s);
    set_gnum(V_NF, nf);
}

/* consumes s */
static void set_record(Str *s)
{
    fld_reserve(0);
    val_free(&fld[0]);
    fld[0] = val_strnum(s);
    fields_valid = 0;
    rec_dirty = 0;
    str_unref(rec_fs);
    rec_fs = gstr(V_FS);
}

static void rebuild_record(void)
{
    Buf b;
    Str *ofs = gstr(V_OFS);
    int i;
    buf_init(&b);
    for (i = 1; i <= nf; i++) {
        Str *s;
        if (i > 1)
            buf_add(&b, ofs->s, ofs->len);
        s = val_tostr(&fld[i]);
        buf_add(&b, s->s, s->len);
        str_unref(s);
    }
    str_unref(ofs);
    val_free(&fld[0]);
    fld[0] = val_strnum(buf_str(&b));
    buf_free(&b);
    rec_dirty = 0;
}

static Val *get_field(int i)
{
    if (i == 0) {
        if (rec_dirty)
            rebuild_record();
        return &fld[0];
    }
    ensure_split();
    if (i > nf)
        return &uninit_val;
    return &fld[i];
}

static Str *record_str(void)
{
    return val_tostr(get_field(0));
}

static void set_nf(double d)
{
    int n, i;
    ensure_split();
    if (d < 0 || d > 10000000)
        fatal("NF set to invalid value %g", d);
    n = (int)d;
    fld_reserve(n);
    if (n > nf) {
        for (i = nf + 1; i <= n; i++)
            fld[i] = val_uninit();
    } else {
        for (i = n + 1; i <= nf; i++)
            val_free(&fld[i]);
    }
    nf = n;
    set_gnum(V_NF, n);
    rec_dirty = 1;
}

/* consumes v */
static void set_field(int i, Val v)
{
    if (i == 0) {
        Str *s = val_tostr(&v);
        val_free(&v);
        set_record(s);
        return;
    }
    ensure_split();
    if (i > nf) {
        int j;
        fld_reserve(i);
        for (j = nf + 1; j <= i; j++)
            fld[j] = val_uninit();
        nf = i;
        set_gnum(V_NF, nf);
    }
    val_free(&fld[i]);
    if (v.t == T_UNINIT)
        v = val_str(str_ref(str_empty));
    fld[i] = v;
    rec_dirty = 1;
}

/* ------------------------------------------------------------------ */
/* input readers                                                      */
/* ------------------------------------------------------------------ */

typedef struct Reader {
    int fd;
    FILE *pfp;          /* popen stream, if any */
    char *buf;
    size_t start, end, cap;
    int eof, err;
} Reader;

static Reader *reader_new(int fd, FILE *pfp)
{
    Reader *r = xcalloc(1, sizeof *r);
    r->fd = fd;
    r->pfp = pfp;
    r->cap = 8192;
    r->buf = xmalloc(r->cap);
    r->buf[0] = 0;
    return r;
}

static void reader_free(Reader *r)
{
    free(r->buf);
    free(r);
}

static void reader_fill(Reader *r)
{
    ssize_t n;
    if (r->eof)
        return;
    if (r->start > 0 && (r->start == r->end || r->start >= r->cap / 2)) {
        memmove(r->buf, r->buf + r->start, r->end - r->start);
        r->end -= r->start;
        r->start = 0;
    }
    if (r->end + 1 >= r->cap) {
        r->cap *= 2;
        r->buf = xrealloc(r->buf, r->cap);
    }
    for (;;) {
        n = read(r->fd, r->buf + r->end, r->cap - r->end - 1);
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }
    if (n <= 0) {
        r->eof = 1;
        if (n < 0)
            r->err = errno ? errno : EIO;
    } else
        r->end += (size_t)n;
    r->buf[r->end] = 0;
}

/* is the line beginning at absolute index i blank (only <blank>s)?
 * 1: yes, *after = index after its newline; 0: no; -1: need more data */
static int blank_line_at(Reader *r, size_t i, size_t *after)
{
    while (i < r->end && (r->buf[i] == ' ' || r->buf[i] == '\t'))
        i++;
    if (i >= r->end)
        return -1;
    if (r->buf[i] == '\n') {
        *after = i + 1;
        return 1;
    }
    return 0;
}

static int read_record(Reader *r, Str **out)
{
    Str *rs = gstr(V_RS);
    int ret;

    if (rs->len == 1) {
        int c = (unsigned char)rs->s[0];
        size_t off = 0;
        str_unref(rs);
        for (;;) {
            char *p = memchr(r->buf + r->start + off, c, r->end - r->start - off);
            if (p) {
                size_t len = (size_t)(p - (r->buf + r->start));
                *out = str_new(r->buf + r->start, len);
                r->start += len + 1;
                return 1;
            }
            off = r->end - r->start;
            if (r->eof) {
                if (r->end > r->start) {
                    *out = str_new(r->buf + r->start, r->end - r->start);
                    r->start = r->end;
                    return 1;
                }
                return r->err ? -1 : 0;
            }
            reader_fill(r);
        }
    }

    if (rs->len == 0) {
        size_t off, after;
        str_unref(rs);
        /* skip leading blank lines */
        for (;;) {
            int b = blank_line_at(r, r->start, &after);
            if (b == 1) {
                r->start = after;
                continue;
            }
            if (b == -1) {
                if (r->eof) {
                    r->start = r->end;
                    return r->err ? -1 : 0;
                }
                reader_fill(r);
                continue;
            }
            break;
        }
        off = 0;
        for (;;) {
            char *p = memchr(r->buf + r->start + off, '\n', r->end - r->start - off);
            size_t i;
            int b;
            if (!p) {
                off = r->end - r->start;
                if (r->eof) {
                    *out = str_new(r->buf + r->start, r->end - r->start);
                    r->start = r->end;
                    return 1;
                }
                reader_fill(r);
                continue;
            }
            i = (size_t)(p - r->buf);
            b = blank_line_at(r, i + 1, &after);
            if (b == 1) {
                *out = str_new(r->buf + r->start, i - r->start);
                r->start = i + 1;
                return 1;
            }
            if (b == -1) {
                if (r->eof) {
                    *out = str_new(r->buf + r->start, i - r->start);
                    r->start = r->end;
                    return 1;
                }
                off = i - r->start;
                reader_fill(r);
                continue;
            }
            off = i + 1 - r->start;
        }
    }

    /* multi-character RS (unspecified by POSIX): treated as an ERE */
    {
        Regex *re = re_dynamic(rs);
        str_unref(rs);
        for (;;) {
            size_t p = r->start, so, eo, ms = 0, me = 0;
            int found = 0;
            while (p <= r->end && re_exec(re, r->buf + p, p > r->start, &so, &eo)) {
                ms = p + so;
                me = p + eo;
                if (me > ms) {
                    found = 1;
                    break;
                }
                if (ms >= r->end)
                    break;
                p = ms + 1;
            }
            if (found) {
                if (me == r->end && !r->eof) {
                    reader_fill(r);
                    continue;
                }
                *out = str_new(r->buf + r->start, ms - r->start);
                r->start = me;
                return 1;
            }
            if (r->eof) {
                if (r->end > r->start) {
                    *out = str_new(r->buf + r->start, r->end - r->start);
                    r->start = r->end;
                    return 1;
                }
                ret = r->err ? -1 : 0;
                return ret;
            }
            reader_fill(r);
        }
    }
}

/* ------------------------------------------------------------------ */
/* streams (files and pipes opened by redirections and getline)       */
/* ------------------------------------------------------------------ */

enum { ST_OUT_FILE, ST_OUT_PIPE, ST_IN_FILE, ST_IN_PIPE };
typedef struct Stream {
    Str *name;
    int kind;
    int isstd;
    FILE *fp;
    Reader *rd;
    struct Stream *next;
} Stream;
static Stream *streams;
static Reader *stdin_rd;

static Reader *get_stdin_reader(void)
{
    if (!stdin_rd)
        stdin_rd = reader_new(0, NULL);
    return stdin_rd;
}

static void flush_all(void)
{
    Stream *s;
    fflush(stdout);
    for (s = streams; s; s = s->next)
        if ((s->kind == ST_OUT_FILE || s->kind == ST_OUT_PIPE) && s->fp)
            fflush(s->fp);
}

static int streq(Str *a, Str *b)
{
    return a->len == b->len && memcmp(a->s, b->s, a->len) == 0;
}

static FILE *get_output(int op, Str *name)
{
    Stream *s;
    FILE *fp;
    int isstd = 0;
    for (s = streams; s; s = s->next)
        if ((s->kind == ST_OUT_FILE || s->kind == ST_OUT_PIPE) && streq(s->name, name))
            return s->fp;
    if (op != '|' && (strcmp(name->s, "/dev/stdout") == 0 || strcmp(name->s, "-") == 0)) {
        fp = stdout;
        isstd = 1;
    } else if (op != '|' && strcmp(name->s, "/dev/stderr") == 0) {
        fp = stderr;
        isstd = 1;
    } else if (op == '|') {
        flush_all();
        fp = popen(name->s, "w");
        if (!fp)
            fatal("can't open pipe %s", name->s);
    } else {
        fp = fopen(name->s, op == 'a' ? "a" : "w");
        if (!fp)
            fatal("can't redirect to %s: %s", name->s, strerror(errno));
    }
    s = xcalloc(1, sizeof *s);
    s->name = str_ref(name);
    s->kind = op == '|' ? ST_OUT_PIPE : ST_OUT_FILE;
    s->isstd = isstd;
    s->fp = fp;
    s->next = streams;
    streams = s;
    return fp;
}

static Reader *get_input(int kind, Str *name)
{
    Stream *s;
    Reader *rd;
    FILE *pfp = NULL;
    int isstd = 0;
    for (s = streams; s; s = s->next)
        if (s->kind == kind && streq(s->name, name))
            return s->rd;
    if (kind == ST_IN_FILE) {
        if (strcmp(name->s, "-") == 0 || strcmp(name->s, "/dev/stdin") == 0) {
            rd = get_stdin_reader();
            isstd = 1;
        } else {
            int fd = open(name->s, O_RDONLY);
            if (fd < 0)
                return NULL;
            rd = reader_new(fd, NULL);
        }
    } else {
        flush_all();
        pfp = popen(name->s, "r");
        if (!pfp)
            return NULL;
        rd = reader_new(fileno(pfp), pfp);
    }
    s = xcalloc(1, sizeof *s);
    s->name = str_ref(name);
    s->kind = kind;
    s->isstd = isstd;
    s->rd = rd;
    s->next = streams;
    streams = s;
    return rd;
}

static int wait_status(int st)
{
    if (st == -1)
        return -1;
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    if (WIFSIGNALED(st))
        return 256 + WTERMSIG(st);
    return st;
}

static int close_one(Stream *s)
{
    int r = 0;
    switch (s->kind) {
    case ST_OUT_FILE:
        if (s->isstd)
            r = fflush(s->fp) == 0 ? 0 : -1;
        else
            r = fclose(s->fp) == 0 ? 0 : -1;
        break;
    case ST_OUT_PIPE:
        r = wait_status(pclose(s->fp));
        break;
    case ST_IN_FILE:
        if (!s->isstd) {
            r = close(s->rd->fd) == 0 ? 0 : -1;
            reader_free(s->rd);
        }
        break;
    case ST_IN_PIPE:
        r = wait_status(pclose(s->rd->pfp));
        reader_free(s->rd);
        break;
    }
    str_unref(s->name);
    free(s);
    return r;
}

static int close_stream(Str *name)
{
    Stream **pp = &streams, *s;
    int r = -1, found = 0;
    while ((s = *pp)) {
        if (streq(s->name, name)) {
            *pp = s->next;
            r = close_one(s);
            found = 1;
        } else
            pp = &s->next;
    }
    return found ? r : -1;
}

static void close_all(void)
{
    while (streams) {
        Stream *s = streams;
        streams = s->next;
        close_one(s);
    }
}

/* ------------------------------------------------------------------ */
/* main input                                                         */
/* ------------------------------------------------------------------ */

static Reader *main_rd;
static int main_is_stdin;
static int argind = 1;
static int files_processed;
static int main_done;

static void incr_var(int slot)
{
    double d = tonum(gvars[slot]) + 1;
    set_gnum(slot, d);
}

static void set_filename(Str *s)
{
    val_free(gvars[V_FILENAME]);
    *gvars[V_FILENAME] = val_strnum(s);
}

static int open_next_main(void)
{
    Array *argv = cell_array(gvars[V_ARGV], "ARGV");
    for (;;) {
        double argc = tonum(gvars[V_ARGC]);
        Str *key, *s;
        Val *e;
        if (argind >= argc) {
            if (files_processed == 0) {
                files_processed = 1;
                main_rd = get_stdin_reader();
                main_is_stdin = 1;
                set_filename(str_cstr("-"));
                set_gnum(V_FNR, 0);
                return 1;
            }
            return 0;
        }
        key = num2str(argind, default_fmt);
        e = array_get(argv, key, 0);
        str_unref(key);
        argind++;
        if (!e)
            continue;
        s = val_tostr(e);
        if (s->len == 0) {
            str_unref(s);
            continue;
        }
        if (is_assignment_operand(s->s)) {
            cmdline_assign(s->s);
            str_unref(s);
            continue;
        }
        files_processed++;
        if (strcmp(s->s, "-") == 0) {
            main_rd = get_stdin_reader();
            main_is_stdin = 1;
        } else {
            int fd = open(s->s, O_RDONLY);
            struct stat stb;
            if (fd < 0)
                fatal("can't open file %s: %s", s->s, strerror(errno));
            if (fstat(fd, &stb) == 0 && S_ISDIR(stb.st_mode))
                fatal("can't read file %s: is a directory", s->s);
            main_rd = reader_new(fd, NULL);
            main_is_stdin = 0;
        }
        set_filename(s);
        set_gnum(V_FNR, 0);
        return 1;
    }
}

static void close_main(void)
{
    if (main_rd && !main_is_stdin) {
        close(main_rd->fd);
        reader_free(main_rd);
    }
    main_rd = NULL;
}

static int main_getline(Str **out)
{
    for (;;) {
        int r;
        if (!main_rd) {
            if (main_done || !open_next_main()) {
                main_done = 1;
                return 0;
            }
        }
        r = read_record(main_rd, out);
        if (r > 0) {
            incr_var(V_NR);
            incr_var(V_FNR);
            return 1;
        }
        if (r < 0) {
            Str *fn = gstr(V_FILENAME);
            fatal("read error on %s: %s", fn->s, strerror(main_rd->err));
        }
        close_main();
    }
}

void error_context(void)
{
    if (phase == PH_MAIN && gvars && ngvars > V_NR) {
        Str *fn = gstr(V_FILENAME);
        fprintf(stderr, "%s:  input record number %.0f, file %s\n", progname,
                tonum(gvars[V_FNR]), fn->s);
        str_unref(fn);
    }
}

/* ------------------------------------------------------------------ */
/* command line assignments                                           */
/* ------------------------------------------------------------------ */

int is_assignment_operand(const char *s)
{
    const char *p = s;
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_'))
        return 0;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '_' ||
           (*p >= '0' && *p <= '9'))
        p++;
    return *p == '=';
}

Str *process_cmdline_escapes(const char *s)
{
    Buf b;
    size_t i = 0, n = strlen(s);
    Str *r;
    buf_init(&b);
    while (i < n) {
        if (s[i] == '\\') {
            int ch;
            size_t ni;
            if (i + 1 >= n) {           /* trailing backslash is literal */
                buf_addc(&b, '\\');
                i++;
                continue;
            }
            ni = (size_t)parse_escape(s, i + 1, n, &ch);
            if (ch < 0) {
                buf_addc(&b, '\\');
                buf_addc(&b, s[i + 1]);
                i += 2;
            } else {
                buf_addc(&b, ch);
                i = ni;
            }
            continue;
        }
        buf_addc(&b, s[i]);
        i++;
    }
    r = buf_str(&b);
    buf_free(&b);
    return r;
}

void cmdline_assign(const char *arg)
{
    const char *eq = strchr(arg, '=');
    size_t nl = (size_t)(eq - arg);
    char *name = xmalloc(nl + 1);
    int idx;
    Val v;
    memcpy(name, arg, nl);
    name[nl] = 0;
    if (is_reserved_word(name))
        fatal("cannot assign to reserved word %s", name);
    {
        int i;
        for (i = 0; i < nfuncs; i++)
            if (strcmp(funcs[i]->name, name) == 0)
                fatal("cannot assign to function name %s", name);
    }
    idx = global_lookup(name, 1);
    v = val_strnum(process_cmdline_escapes(eq + 1));
    if (idx == V_NF) {
        set_nf(tonum(&v));
        val_free(&v);
    } else {
        Val *c = gvars[idx];
        if (c->t == T_ARRAY)
            fatal("cannot assign to array %s", name);
        if (rec_dirty && (idx == V_OFS || idx == V_CONVFMT))
            rebuild_record();
        val_free(c);
        *c = v;
    }
    free(name);
}

/* ------------------------------------------------------------------ */
/* lvalues                                                            */
/* ------------------------------------------------------------------ */

static Val eval(Node *n);
static int exec(Node *s);

static double eval_num(Node *n)
{
    Val v;
    double d;
    if (n->type == N_NUM)
        return n->num;
    v = eval(n);
    d = tonum(&v);
    val_free(&v);
    return d;
}

static Str *eval_str(Node *n)
{
    Val v;
    Str *s;
    if (n->type == N_STR)
        return str_ref(n->str);
    v = eval(n);
    s = val_tostr(&v);
    val_free(&v);
    return s;
}

static int eval_bool(Node *n)
{
    Val v = eval(n);
    int b = tobool(&v);
    val_free(&v);
    return b;
}

static Str *subscript(Node *list)
{
    Buf b;
    Str *sep, *r;
    if (!list->next)
        return eval_str(list);
    buf_init(&b);
    sep = gstr(V_SUBSEP);
    for (; list; list = list->next) {
        Str *s = eval_str(list);
        buf_add(&b, s->s, s->len);
        str_unref(s);
        if (list->next)
            buf_add(&b, sep->s, sep->len);
    }
    str_unref(sep);
    r = buf_str(&b);
    buf_free(&b);
    return r;
}

static int field_index(Node *n)
{
    double d = eval_num(n);
    if (d < 0 || d > 10000000 || d != d)
        fatal("trying to access out of range field %g", d);
    return (int)d;
}

typedef struct LRef {
    Val *cell;
    int field;      /* >= 0 for field references */
    int is_nf;
} LRef;

static void lref(Node *lv, LRef *r)
{
    r->cell = NULL;
    r->field = -1;
    r->is_nf = 0;
    switch (lv->type) {
    case N_GVAR: case N_LVAR:
        r->cell = var_cell(lv);
        if (r->cell->t == T_ARRAY)
            fatal("can't assign to %s; it's an array name", var_name(lv));
        if (lv->type == N_GVAR && lv->idx == V_NF)
            r->is_nf = 1;
        break;
    case N_INDEX: {
        Str *key = subscript(lv->b);
        Array *a = node_array(lv->a);
        r->cell = array_get(a, key, 1);
        str_unref(key);
        break;
    }
    case N_FIELD:
        r->field = field_index(lv->a);
        break;
    default:
        fatal("assignment to non-lvalue");
    }
}

static Val lref_get(LRef *r)
{
    if (r->field >= 0)
        return val_copy(get_field(r->field));
    if (r->is_nf)
        ensure_split();
    if (r->cell->t == T_REF)
        return val_uninit();
    return val_copy(r->cell);
}

/* consumes v */
static void lref_set(LRef *r, Val v)
{
    if (r->field >= 0) {
        set_field(r->field, v);
        return;
    }
    if (r->is_nf) {
        double d = tonum(&v);
        val_free(&v);
        set_nf(d);
        return;
    }
    if (rec_dirty && (r->cell == gvars[V_OFS] || r->cell == gvars[V_CONVFMT]))
        rebuild_record();   /* $0 reflects the OFS in effect at field assignment */
    val_free(r->cell);
    *r->cell = v;
}

static void assign(Node *lv, Val v)
{
    LRef r;
    lref(lv, &r);
    lref_set(&r, v);
}

/* ------------------------------------------------------------------ */
/* comparisons and regex helpers                                      */
/* ------------------------------------------------------------------ */

static int str_compare(Str *a, Str *b)
{
    if (collate_c) {
        size_t n = a->len < b->len ? a->len : b->len;
        int c = memcmp(a->s, b->s, n);
        if (c)
            return c;
        return a->len < b->len ? -1 : a->len > b->len;
    }
    return strcoll(a->s, b->s);
}

static int compare_vals(int op, Val *a, Val *b)
{
    if (a->t != T_STR && b->t != T_STR) {
        double x = a->t == T_UNINIT ? 0 : a->n;
        double y = b->t == T_UNINIT ? 0 : b->n;
        switch (op) {
        case TK_LT: return x < y;
        case TK_LE: return x <= y;
        case TK_GT: return x > y;
        case TK_GE: return x >= y;
        case TK_EQ: return x == y;
        default: return x != y;
        }
    } else {
        Str *s = val_tostr(a), *t = val_tostr(b);
        int c, r;
        if (op == TK_EQ || op == TK_NE)
            c = streq(s, t) ? 0 : 1;
        else
            c = str_compare(s, t);
        str_unref(s);
        str_unref(t);
        switch (op) {
        case TK_LT: r = c < 0; break;
        case TK_LE: r = c <= 0; break;
        case TK_GT: r = c > 0; break;
        case TK_GE: r = c >= 0; break;
        case TK_EQ: r = c == 0; break;
        default: r = c != 0; break;
        }
        return r;
    }
}

/* regex for an ERE argument: an ERE token or the string value of an expression */
static Regex *regex_of(Node *n)
{
    Str *p;
    Regex *re;
    if (n->type == N_ERE)
        return n->re;
    if (n->type == N_GROUP && n->a->type == N_ERE)
        return n->a->re;
    p = eval_str(n);
    re = re_dynamic(p);
    str_unref(p);
    return re;
}

static int match_str(Regex *re, Str *s)
{
    size_t so, eo;
    return re_exec(re, s->s, 0, &so, &eo);
}

/* ------------------------------------------------------------------ */
/* output                                                             */
/* ------------------------------------------------------------------ */


static Buf *obuf_get(Buf *local)
{
    if (obuf_depth < 16) {
        Buf *b = &obuf_pool[obuf_depth++];
        b->len = 0;
        if (b->p) b->p[0] = 0;
        return b;
    }
    buf_init(local);
    obuf_depth++;
    return local;
}

static void obuf_put(Buf *b, Buf *local)
{
    obuf_depth--;
    if (b == local)
        buf_free(local);
}

static void write_out(FILE *fp, const char *s, size_t n)
{
    if (n && fwrite(s, 1, n, fp) != n && fp != stdout && fp != stderr)
        fatal("write error: %s", strerror(errno));
}

static void do_print(Node *s)
{
    Buf local, *b = obuf_get(&local);
    FILE *fp = stdout;
    Node *a;
    if (s->type == N_PRINT) {
        if (!s->a) {
            Str *r = record_str();
            buf_add(b, r->s, r->len);
            str_unref(r);
        } else {
            Str *ofs = NULL;
            for (a = s->a; a; a = a->next) {
                Val v = eval(a);
                Str *str = val_tostr_out(&v);
                val_free(&v);
                if (a != s->a) {
                    if (!ofs)
                        ofs = gstr(V_OFS);
                    buf_add(b, ofs->s, ofs->len);
                }
                buf_add(b, str->s, str->len);
                str_unref(str);
            }
            if (ofs)
                str_unref(ofs);
        }
        {
            Str *ors = gstr(V_ORS);
            buf_add(b, ors->s, ors->len);
            str_unref(ors);
        }
    } else {
        Val stackargs[16], *args = stackargs;
        int n = s->nargs, i;
        Str *fmt;
        if (n > 16)
            args = xmalloc((size_t)n * sizeof *args);
        for (a = s->a, i = 0; a; a = a->next, i++)
            args[i] = eval(a);
        fmt = val_tostr(&args[0]);
        format_awk(b, fmt->s, fmt->len, args + 1, n - 1);
        str_unref(fmt);
        for (i = 0; i < n; i++)
            val_free(&args[i]);
        if (args != stackargs)
            free(args);
    }
    if (s->op) {
        Str *dest = eval_str(s->b);
        fp = get_output(s->op, dest);
        str_unref(dest);
    }
    write_out(fp, b->p, b->len);
    obuf_put(b, &local);
}

/* ------------------------------------------------------------------ */
/* builtins                                                           */
/* ------------------------------------------------------------------ */

static void nargs_check(Node *n, int min, int max)
{
    if (n->nargs < min || n->nargs > max)
        fatal("function %s called with %d argument%s", n->str ? n->str->s : "?",
              n->nargs, n->nargs == 1 ? "" : "s");
}

static Node *argn(Node *n, int i)
{
    Node *a = n->a;
    while (a && i--)
        a = a->next;
    return a;
}

/* expand repl for sub/gsub: & is the match, \& a literal &, \\ a backslash */
static void add_repl(Buf *o, Str *repl, const char *m, size_t mlen)
{
    size_t i;
    for (i = 0; i < repl->len; i++) {
        char c = repl->s[i];
        if (c == '\\' && i + 1 < repl->len && (repl->s[i+1] == '&' || repl->s[i+1] == '\\')) {
            buf_addc(o, repl->s[++i]);
        } else if (c == '&')
            buf_add(o, m, mlen);
        else
            buf_addc(o, c);
    }
}

static Str *substitute(Regex *re, Str *s, Str *repl, int global, int *count)
{
    Buf o;
    size_t p = 0, len = s->len, so, eo;
    long last_end = -1;
    *count = 0;
    buf_init(&o);
    while (p <= len) {
        size_t ms, me;
        if (!re_exec(re, s->s + p, p > 0, &so, &eo))
            break;
        ms = p + so;
        me = p + eo;
        if (ms == me) {
            if ((long)ms == last_end) {
                /* empty match right after previous match: skip a character */
                size_t cl;
                if (ms >= len)
                    break;
                cl = mb_charlen(s->s + ms, len - ms);
                buf_add(&o, s->s + p, ms + cl - p);
                p = ms + cl;
                continue;
            }
            buf_add(&o, s->s + p, ms - p);
            add_repl(&o, repl, s->s + ms, 0);
            (*count)++;
            if (ms >= len) {
                p = len + 1;
                break;
            }
            {
                size_t cl = mb_charlen(s->s + ms, len - ms);
                buf_add(&o, s->s + ms, cl);
                p = ms + cl;
            }
        } else {
            buf_add(&o, s->s + p, ms - p);
            add_repl(&o, repl, s->s + ms, me - ms);
            (*count)++;
            p = me;
            last_end = (long)me;
        }
        if (!global)
            break;
    }
    if (p <= len)
        buf_add(&o, s->s + p, len - p);
    {
        Str *r = buf_str(&o);
        buf_free(&o);
        return r;
    }
}

static Val do_subgsub(Node *n, int global)
{
    Node *ren = argn(n, 0), *repln = argn(n, 1), *tgt = argn(n, 2);
    Str *pat = NULL, *repl, *cur, *res;
    Regex *re;
    LRef r;
    int count, use_lref = 0;
    nargs_check(n, 2, 3);
    if (ren->type != N_ERE)
        pat = eval_str(ren);
    repl = eval_str(repln);
    if (tgt && (tgt->type == N_GVAR || tgt->type == N_LVAR || tgt->type == N_INDEX ||
                tgt->type == N_FIELD)) {
        Val v;
        lref(tgt, &r);
        v = lref_get(&r);
        cur = val_tostr(&v);
        val_free(&v);
        use_lref = 1;
    } else if (tgt) {
        cur = eval_str(tgt);
    } else {
        cur = record_str();
    }
    re = pat ? re_dynamic(pat) : ren->re;
    res = substitute(re, cur, repl, global, &count);
    if (count > 0) {
        if (use_lref)
            lref_set(&r, val_str(res));
        else if (!tgt)
            set_field(0, val_str(res));
        else
            str_unref(res);
    } else
        str_unref(res);
    str_unref(cur);
    str_unref(repl);
    if (pat)
        str_unref(pat);
    return val_num(count);
}

typedef struct SplitCtx { Array *a; int n; } SplitCtx;

static void add_split(void *ctx, const char *s, size_t n)
{
    SplitCtx *c = ctx;
    char kb[32];
    Str *key, *v;
    Val *cell;
    c->n++;
    snprintf(kb, sizeof kb, "%d", c->n);
    key = str_cstr(kb);
    cell = array_get(c->a, key, 1);
    str_unref(key);
    v = str_new(s, n);
    val_free(cell);
    *cell = val_strnum(v);
}

static Val do_split(Node *n)
{
    Node *sn = argn(n, 0), *an = argn(n, 1), *fn = argn(n, 2);
    Str *s, *fs = NULL;
    Regex *re = NULL;
    SplitCtx ctx;
    nargs_check(n, 2, 3);
    s = eval_str(sn);
    if (fn) {
        if (fn->type == N_ERE)
            re = fn->re;
        else
            fs = eval_str(fn);
    } else
        fs = gstr(V_FS);
    ctx.a = node_array(an);
    ctx.n = 0;
    array_clear(ctx.a);
    split_string(s->s, s->len, fs, re, 0, add_split, &ctx);
    str_unref(s);
    if (fs)
        str_unref(fs);
    return val_num(ctx.n);
}

static Str *case_map(Str *s, int upper)
{
    Str *r;
    size_t i;
    if (!mb_mode) {
        r = str_new(s->s, s->len);
        for (i = 0; i < r->len; i++)
            r->s[i] = (char)(upper ? toupper((unsigned char)r->s[i]) : tolower((unsigned char)r->s[i]));
        return r;
    } else {
        Buf b;
        buf_init(&b);
        for (i = 0; i < s->len; ) {
            mbstate_t st;
            wchar_t wc;
            size_t k;
            memset(&st, 0, sizeof st);
            k = mbrtowc(&wc, s->s + i, s->len - i, &st);
            if (k == (size_t)-1 || k == (size_t)-2 || k == 0) {
                buf_addc(&b, s->s[i]);
                i++;
                continue;
            }
            {
                char mb[MB_LEN_MAX];
                wint_t m = upper ? towupper((wint_t)wc) : towlower((wint_t)wc);
                size_t ol;
                memset(&st, 0, sizeof st);
                ol = wcrtomb(mb, (wchar_t)m, &st);
                if (ol == (size_t)-1)
                    buf_add(&b, s->s + i, k);
                else
                    buf_add(&b, mb, ol);
            }
            i += k;
        }
        r = buf_str(&b);
        buf_free(&b);
        return r;
    }
}

static Val do_builtin(Node *n)
{
    Node *a0 = argn(n, 0), *a1 = argn(n, 1), *a2 = argn(n, 2);
    switch (n->idx) {
    case B_LENGTH: {
        Str *s;
        size_t l;
        nargs_check(n, 0, 1);
        if (!a0) {
            s = record_str();
        } else {
            if ((a0->type == N_GVAR && a0->idx != V_NF) || a0->type == N_LVAR) {
                Val *c = deref(var_cell(a0));
                if (c->t == T_ARRAY)
                    return val_num((double)c->u.a->count);
            }
            s = eval_str(a0);
        }
        l = mb_strlen(s->s, s->len);
        str_unref(s);
        return val_num((double)l);
    }
    case B_SUBSTR: {
        Str *s, *r;
        double m, len, start, end;
        size_t L, bs, be;
        nargs_check(n, 2, 3);
        s = eval_str(a0);
        m = eval_num(a1);
        L = mb_strlen(s->s, s->len);
        if (m != m) {
            str_unref(s);
            return val_str(str_ref(str_empty));
        }
        /* historical practice (unspecified by POSIX for m < 1): truncate,
         * then clamp the start to 1 without adjusting the length */
        start = m < -1e15 ? -1e15 : m > 1e15 ? 1e15 : trunc(m);
        if (start < 1)
            start = 1;
        if (a2) {
            len = eval_num(a2);
            if (len != len) {
                str_unref(s);
                return val_str(str_ref(str_empty));
            }
            len = len < 0 ? 0 : len > 1e15 ? 1e15 : trunc(len);
            end = start + len;
        } else
            end = (double)L + 1;
        if (end > (double)L + 1)
            end = (double)L + 1;
        if (end <= start) {
            str_unref(s);
            return val_str(str_ref(str_empty));
        }
        bs = mb_offset(s->s, s->len, (size_t)start - 1);
        be = bs + mb_offset(s->s + bs, s->len - bs, (size_t)(end - start));
        r = str_new(s->s + bs, be - bs);
        str_unref(s);
        return val_str(r);
    }
    case B_INDEX: {
        Str *s, *t;
        size_t i, cpos = 1;
        double res = 0;
        nargs_check(n, 2, 2);
        s = eval_str(a0);
        t = eval_str(a1);
        if (t->len == 0)
            res = s->len ? 1 : 0;
        else {
            for (i = 0; i + t->len <= s->len; ) {
                if (memcmp(s->s + i, t->s, t->len) == 0) {
                    res = (double)cpos;
                    break;
                }
                i += mb_charlen(s->s + i, s->len - i);
                cpos++;
            }
        }
        str_unref(s);
        str_unref(t);
        return val_num(res);
    }
    case B_MATCH: {
        Str *s;
        Regex *re;
        size_t so, eo;
        double rstart = 0, rlength = -1;
        nargs_check(n, 2, 2);
        s = eval_str(a0);
        re = regex_of(a1);
        if (re_exec(re, s->s, 0, &so, &eo)) {
            rstart = (double)mb_strlen(s->s, so) + 1;
            rlength = (double)mb_strlen(s->s + so, eo - so);
        }
        str_unref(s);
        set_gnum(V_RSTART, rstart);
        set_gnum(V_RLENGTH, rlength);
        return val_num(rstart);
    }
    case B_SPLIT:
        return do_split(n);
    case B_SUB:
        return do_subgsub(n, 0);
    case B_GSUB:
        return do_subgsub(n, 1);
    case B_SPRINTF: {
        Val stackargs[16], *args = stackargs;
        int na = n->nargs, i;
        Node *p;
        Str *fmt, *r;
        Buf b;
        if (na < 1)
            fatal("sprintf: no format");
        if (na > 16)
            args = xmalloc((size_t)na * sizeof *args);
        for (p = n->a, i = 0; p; p = p->next, i++)
            args[i] = eval(p);
        fmt = val_tostr(&args[0]);
        buf_init(&b);
        format_awk(&b, fmt->s, fmt->len, args + 1, na - 1);
        r = buf_str(&b);
        buf_free(&b);
        str_unref(fmt);
        for (i = 0; i < na; i++)
            val_free(&args[i]);
        if (args != stackargs)
            free(args);
        return val_str(r);
    }
    case B_TOLOWER: case B_TOUPPER: {
        Str *s, *r;
        nargs_check(n, 1, 1);
        s = eval_str(a0);
        r = case_map(s, n->idx == B_TOUPPER);
        str_unref(s);
        return val_str(r);
    }
    case B_ATAN2: {
        double y, x;
        nargs_check(n, 2, 2);
        y = eval_num(a0);
        x = eval_num(a1);
        return val_num(atan2(y, x));
    }
    case B_COS: nargs_check(n, 1, 1); return val_num(cos(eval_num(a0)));
    case B_SIN: nargs_check(n, 1, 1); return val_num(sin(eval_num(a0)));
    case B_EXP: nargs_check(n, 1, 1); return val_num(exp(eval_num(a0)));
    case B_LOG: nargs_check(n, 1, 1); return val_num(log(eval_num(a0)));
    case B_SQRT: nargs_check(n, 1, 1); return val_num(sqrt(eval_num(a0)));
    case B_INT: nargs_check(n, 1, 1); return val_num(trunc(eval_num(a0)));
    case B_RAND:
        nargs_check(n, 0, 0);
        return val_num(drand48());
    case B_SRAND: {
        double prev = rand_seed;
        nargs_check(n, 0, 1);
        rand_seed = a0 ? eval_num(a0) : (double)time(NULL);
        srand48((long)rand_seed);
        return val_num(prev);
    }
    case B_CLOSE: {
        Str *s;
        int r;
        nargs_check(n, 1, 1);
        s = eval_str(a0);
        r = close_stream(s);
        str_unref(s);
        return val_num(r);
    }
    case B_FFLUSH: {
        int r = 0;
        nargs_check(n, 0, 1);
        if (!a0) {
            flush_all();
        } else {
            Str *s = eval_str(a0);
            if (s->len == 0)
                flush_all();
            else {
                Stream *st;
                r = -1;
                for (st = streams; st; st = st->next)
                    if ((st->kind == ST_OUT_FILE || st->kind == ST_OUT_PIPE) && streq(st->name, s))
                        r = fflush(st->fp) == 0 ? 0 : -1;
            }
            str_unref(s);
        }
        return val_num(r);
    }
    case B_SYSTEM: {
        Str *s;
        int st;
        nargs_check(n, 1, 1);
        s = eval_str(a0);
        flush_all();
        st = system(s->s);
        str_unref(s);
        return val_num(wait_status(st));
    }
    }
    fatal("unknown builtin");
}

/* ------------------------------------------------------------------ */
/* getline                                                            */
/* ------------------------------------------------------------------ */

static Val do_getline(Node *n)
{
    Str *s = NULL;
    int r;
    if (n->op == 0) {
        r = main_getline(&s);
    } else {
        Str *name = eval_str(n->b);
        Reader *rd = get_input(n->op == 1 ? ST_IN_FILE : ST_IN_PIPE, name);
        str_unref(name);
        if (!rd)
            return val_num(-1);
        r = read_record(rd, &s);
    }
    if (r > 0) {
        if (n->a)
            assign(n->a, val_strnum(s));
        else
            set_record(s);
    }
    return val_num(r);
}

/* ------------------------------------------------------------------ */
/* function calls                                                     */
/* ------------------------------------------------------------------ */

static Val call_func(Node *n)
{
    Func *f = funcs[n->idx];
    int np = f->nparams, i;
    Val *loc;
    Node *a;
    Val result;
    int r;
    if (n->nargs > np)
        fatal("function %s called with %d args, accepts only %d", f->name, n->nargs, np);
    loc = xcalloc((size_t)(np ? np : 1), sizeof *loc);
    for (a = n->a, i = 0; a; a = a->next, i++) {
        if ((a->type == N_GVAR && a->idx != V_NF) || a->type == N_LVAR) {
            Val *t = deref(var_cell(a));
            if (t->t == T_ARRAY) {
                loc[i].t = T_ARRAY;
                loc[i].own = 0;
                loc[i].u.a = t->u.a;
                continue;
            }
            if (t->t == T_UNINIT) {
                loc[i].t = T_REF;
                loc[i].u.ref = t;
                continue;
            }
            loc[i] = val_copy(t);
            continue;
        }
        loc[i] = eval(a);
    }
    if (fdepth > 1000000)
        fatal("function call nesting too deep");
    push_frame(loc, np);
    cur_locals = loc;
    r = exec(f->body);
    result = val_uninit();
    if (r == X_RETURN) {
        result = retval;
        retval = val_uninit();
    }
    pop_frame();
    return result;
}

/* ------------------------------------------------------------------ */
/* expression evaluation                                              */
/* ------------------------------------------------------------------ */

static double arith(int op, double x, double y)
{
    switch (op) {
    case '+': return x + y;
    case '-': return x - y;
    case '*': return x * y;
    case '/':
        if (y == 0)
            fatal("division by zero");
        return x / y;
    case '%':
        if (y == 0)
            fatal("division by zero in %%");
        return fmod(x, y);
    case '^': return pow(x, y);
    }
    return 0;
}

static int assign_arith_op(int tk)
{
    switch (tk) {
    case TK_ADD_ASSIGN: return '+';
    case TK_SUB_ASSIGN: return '-';
    case TK_MUL_ASSIGN: return '*';
    case TK_DIV_ASSIGN: return '/';
    case TK_MOD_ASSIGN: return '%';
    case TK_POW_ASSIGN: return '^';
    }
    return 0;
}

static Val eval(Node *n)
{
    switch (n->type) {
    case N_NUM:
        return val_num(n->num);
    case N_STR:
        return val_str(str_ref(n->str));
    case N_ERE: {
        Str *r = record_str();
        int m = match_str(n->re, r);
        str_unref(r);
        return val_num(m);
    }
    case N_GVAR: case N_LVAR: {
        Val *c;
        if (n->type == N_GVAR) {
            if (n->idx == V_NF)
                ensure_split();
            c = gvars[n->idx];
        } else
            c = &cur_locals[n->idx];
        if (c->t == T_REF) {
            if (deref(c)->t == T_ARRAY)
                fatal("can't use array %s in scalar context", var_name(n));
            return val_uninit();
        }
        if (c->t == T_ARRAY)
            fatal("can't use array %s in scalar context", var_name(n));
        return val_copy(c);
    }
    case N_INDEX: {
        Str *key = subscript(n->b);
        Array *a = node_array(n->a);
        Val *c = array_get(a, key, 1);
        str_unref(key);
        return val_copy(c);
    }
    case N_FIELD:
        return val_copy(get_field(field_index(n->a)));
    case N_ASSIGN: {
        if (n->op == TK_ASSIGN) {
            Val v = eval(n->b);
            if (v.t == T_ARRAY)
                fatal("can't assign array");
            assign(n->a, val_copy(&v));
            return v;
        } else {
            double rhs = eval_num(n->b), res;
            LRef r;
            Val old;
            lref(n->a, &r);
            old = lref_get(&r);
            res = arith(assign_arith_op(n->op), tonum(&old), rhs);
            val_free(&old);
            lref_set(&r, val_num(res));
            return val_num(res);
        }
    }
    case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC: {
        LRef r;
        Val old;
        double d;
        lref(n->a, &r);
        old = lref_get(&r);
        d = tonum(&old);
        val_free(&old);
        lref_set(&r, val_num((n->type == N_PREINC || n->type == N_POSTINC) ? d + 1 : d - 1));
        if (n->type == N_PREINC) return val_num(d + 1);
        if (n->type == N_PREDEC) return val_num(d - 1);
        return val_num(d);
    }
    case N_COND:
        return eval_bool(n->a) ? eval(n->b) : eval(n->c);
    case N_OR:
        return val_num(eval_bool(n->a) || eval_bool(n->b));
    case N_AND:
        return val_num(eval_bool(n->a) && eval_bool(n->b));
    case N_NOT:
        return val_num(!eval_bool(n->a));
    case N_IN: {
        Node *list = n->a;
        Str *key;
        Array *a;
        int r;
        if (list->type == N_GROUP && !list->next)
            list = list->a;
        key = subscript(list);
        a = node_array(n->b);
        r = array_get(a, key, 0) != NULL;
        str_unref(key);
        return val_num(r);
    }
    case N_MATCH: {
        Str *s = eval_str(n->a);
        Regex *re = regex_of(n->b);
        int m = match_str(re, s);
        str_unref(s);
        return val_num(n->op ? !m : m);
    }
    case N_CMP: {
        Val a = eval(n->a), b = eval(n->b);
        int r = compare_vals(n->op, &a, &b);
        val_free(&a);
        val_free(&b);
        return val_num(r);
    }
    case N_CONCAT: {
        Str *a = eval_str(n->a), *b = eval_str(n->b), *r;
        if (a->len == 0) {
            str_unref(a);
            return val_str(b);
        }
        if (b->len == 0) {
            str_unref(b);
            return val_str(a);
        }
        r = str_new(NULL, a->len + b->len);
        memcpy(r->s, a->s, a->len);
        memcpy(r->s + a->len, b->s, b->len);
        str_unref(a);
        str_unref(b);
        return val_str(r);
    }
    case N_ARITH: {
        double x = eval_num(n->a);
        double y = eval_num(n->b);
        return val_num(arith(n->op, x, y));
    }
    case N_NEG:
        return val_num(-eval_num(n->a));
    case N_PLUS:
        return val_num(eval_num(n->a));
    case N_CALL:
        return call_func(n);
    case N_BUILTIN:
        return do_builtin(n);
    case N_GETLINE:
        return do_getline(n);
    case N_GROUP:
        return eval(n->a);
    case N_GROUPLIST:
        fatal("invalid use of parenthesized expression list");
    }
    fatal("internal error: bad expression node %d", n->type);
}

/* ------------------------------------------------------------------ */
/* statements                                                         */
/* ------------------------------------------------------------------ */

static int exec(Node *s)
{
    int r;
    if (!s)
        return X_NORMAL;
    switch (s->type) {
    case N_BLOCK: {
        Node *p;
        for (p = s->a; p; p = p->next)
            if ((r = exec(p)) != X_NORMAL)
                return r;
        return X_NORMAL;
    }
    case N_EXPRSTMT: {
        Val v = eval(s->a);
        val_free(&v);
        return X_NORMAL;
    }
    case N_PRINT: case N_PRINTF:
        do_print(s);
        return X_NORMAL;
    case N_IF:
        if (eval_bool(s->a))
            return exec(s->b);
        return exec(s->c);
    case N_WHILE:
        while (eval_bool(s->a)) {
            r = exec(s->b);
            if (r == X_BREAK) break;
            if (r == X_RETURN) return r;
        }
        return X_NORMAL;
    case N_DO:
        do {
            r = exec(s->a);
            if (r == X_BREAK) break;
            if (r == X_RETURN) return r;
        } while (eval_bool(s->b));
        return X_NORMAL;
    case N_FOR:
        if (s->a)
            exec(s->a);
        for (;;) {
            if (s->b && !eval_bool(s->b))
                break;
            r = exec(s->d);
            if (r == X_BREAK) break;
            if (r == X_RETURN) return r;
            if (s->c)
                exec(s->c);
        }
        return X_NORMAL;
    case N_FORIN: {
        Array *a = node_array(s->b);
        size_t nk, i;
        Str **keys = array_keys(a, &nk);
        int ret = X_NORMAL;
        for (i = 0; i < nk; i++) {
            if (!array_get(a, keys[i], 0))
                continue;
            assign(s->a, val_str(str_ref(keys[i])));
            r = exec(s->c);
            if (r == X_BREAK) break;
            if (r == X_RETURN) { ret = r; break; }
        }
        for (i = 0; i < nk; i++)
            str_unref(keys[i]);
        free(keys);
        return ret;
    }
    case N_DELETE: {
        if (s->b) {
            Str *key = subscript(s->b);
            Array *a = node_array(s->a);
            array_delete(a, key);
            str_unref(key);
        } else
            array_clear(node_array(s->a));
        return X_NORMAL;
    }
    case N_NEXT:
        if (phase != PH_MAIN)
            fatal("next used in BEGIN or END action");
        longjmp(top_jb, J_NEXT);
    case N_NEXTFILE:
        if (phase != PH_MAIN)
            fatal("nextfile used in BEGIN or END action");
        longjmp(top_jb, J_NEXTFILE);
    case N_EXIT:
        if (s->a)
            exit_code = (int)eval_num(s->a);
        longjmp(top_jb, J_EXIT);
    case N_RETURN: {
        Val v = s->a ? eval(s->a) : val_uninit();
        val_free(&retval);
        retval = v;
        return X_RETURN;
    }
    case N_BREAK:
        return X_BREAK;
    case N_CONTINUE:
        return X_CONTINUE;
    }
    fatal("internal error: bad statement node %d", s->type);
}

/* ------------------------------------------------------------------ */
/* program execution                                                  */
/* ------------------------------------------------------------------ */

static void init_specials(void)
{
    static const char *names[V_NSPECIAL] = {
        "NF", "NR", "FNR", "FS", "OFS", "ORS", "RS", "FILENAME", "SUBSEP",
        "RSTART", "RLENGTH", "CONVFMT", "OFMT", "ENVIRON", "ARGC", "ARGV"
    };
    int i;
    for (i = 0; i < V_NSPECIAL; i++)
        if (global_lookup(names[i], 1) != i)
            fatal("internal error: special variable slots");
}

/* called before parsing */
void run_init(int argc, char **argv, char **envp)
{
    int i;
    Array *a;
    default_fmt = str_static("%.6g");
    init_specials();
    *gvars[V_FS] = val_str(str_static(" "));
    *gvars[V_OFS] = val_str(str_static(" "));
    *gvars[V_ORS] = val_str(str_static("\n"));
    *gvars[V_RS] = val_str(str_static("\n"));
    *gvars[V_SUBSEP] = val_str(str_static("\034"));
    *gvars[V_CONVFMT] = val_str(str_static("%.6g"));
    *gvars[V_OFMT] = val_str(str_static("%.6g"));
    *gvars[V_NR] = val_num(0);
    *gvars[V_FNR] = val_num(0);
    *gvars[V_NF] = val_num(0);
    *gvars[V_RSTART] = val_num(0);
    *gvars[V_RLENGTH] = val_num(-1);

    a = cell_array(gvars[V_ENVIRON], "ENVIRON");
    for (i = 0; envp && envp[i]; i++) {
        char *eq = strchr(envp[i], '=');
        Str *k, *v;
        Val *c;
        if (!eq)
            continue;
        k = str_new(envp[i], (size_t)(eq - envp[i]));
        v = str_cstr(eq + 1);
        c = array_get(a, k, 1);
        val_free(c);
        *c = val_strnum(v);
        str_unref(k);
    }
    a = cell_array(gvars[V_ARGV], "ARGV");
    for (i = 0; i < argc; i++) {
        char kb[32];
        snprintf(kb, sizeof kb, "%d", i);
        array_set_cstr(a, kb, val_strnum(str_cstr(argv[i])));
    }
    *gvars[V_ARGC] = val_num(argc);

    fld_reserve(1);
    fld[0] = val_uninit();
    rec_fs = str_ref(str_empty);
    fields_valid = 1;
    nf = 0;
    retval = val_uninit();
    uninit_val = val_uninit();
    srand48(0);
}

static int rule_matches(Rule *r)
{
    if (!r->pat1)
        return 1;
    if (!r->pat2)
        return eval_bool(r->pat1);
    if (r->inrange) {
        if (eval_bool(r->pat2))
            r->inrange = 0;
        return 1;
    }
    if (eval_bool(r->pat1)) {
        r->inrange = !eval_bool(r->pat2);
        return 1;
    }
    return 0;
}

static void print_record(void)
{
    Str *r = record_str(), *ors = gstr(V_ORS);
    write_out(stdout, r->s, r->len);
    write_out(stdout, ors->s, ors->len);
    str_unref(r);
    str_unref(ors);
}

static int idx_g;   /* loop indices kept static across longjmp */

static int run_begin(void)
{
    int code = setjmp(top_jb);
    if (code) {
        unwind();
        return code == J_EXIT;
    }
    phase = PH_BEGIN;
    for (idx_g = 0; idx_g < nbegin; idx_g++)
        exec(begin_actions[idx_g]);
    return 0;
}

static int run_main(void)
{
    int code = setjmp(top_jb);
    if (code) {
        unwind();
        if (code == J_EXIT)
            return 1;
        if (code == J_NEXTFILE)
            close_main();
    }
    phase = PH_MAIN;
    for (;;) {
        Str *s;
        if (!main_getline(&s))
            return 0;
        set_record(s);
        for (idx_g = 0; idx_g < nrules; idx_g++) {
            Rule *r = &rules[idx_g];
            if (rule_matches(r)) {
                if (r->action)
                    exec(r->action);
                else
                    print_record();
            }
        }
    }
}

static void run_end(void)
{
    int code = setjmp(top_jb);
    if (code) {
        unwind();
        return;
    }
    phase = PH_END;
    for (idx_g = 0; idx_g < nend; idx_g++)
        exec(end_actions[idx_g]);
}

int run_program(void)
{
    const char *lc = setlocale(LC_COLLATE, NULL);
    collate_c = !lc || strcmp(lc, "C") == 0 || strcmp(lc, "POSIX") == 0;

    if (nbegin == 0 && nrules == 0 && nend == 0)
        return 0;
    if (!run_begin()) {
        if (nrules > 0 || nend > 0)
            run_main();
    }
    if (nend > 0)
        run_end();
    phase = PH_INIT;
    close_main();
    close_all();
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "%s: write error on standard output\n", progname);
        return 2;
    }
    return exit_code;
}
