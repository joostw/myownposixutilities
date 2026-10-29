/*
 * excmd.c - ex command line parsing (XCU ex "Command Line Parsing in ex"),
 * addressing, the command table, input sources and line output.
 */
#include "ex.h"
#include "cmd.h"

int mode = MODE_EX;
int cur;
int cur_off;
int want_col;
int silent, interactive, batch_stdin;
volatile sig_atomic_t intr, got_winch, got_cont, got_hup;
int in_global, in_exec_buffer;
int search_dir;
int last_print_flags;
char *curpath, *altpath;
int path_changed_by_file;
int ex_quit_pending;
Source *cur_source;
int exit_status;
int command_lines_printed;
long buffer_generation;

/* ---------- command table ---------- */

static const CmdDef cmdtab[] = {
    { "abbreviate", "ab", c_abbrev, 0, ARG_MAP },
    { "append", "a", c_append, 1, AZERO | AEMPTY | BANG | NOAP | ARG_TEXT },
    { "args", "ar", c_args, 0, 0 },
    { "change", "c", c_change, 2, BANG | NOAP | ARG_TEXT },
    { "chdir", "chd", c_cd, 0, BANG },
    { "cd", "cd", c_cd, 0, BANG },
    { "copy", "co", c_copy, 2, 0 },
    { "t", "t", c_copy, 2, 0 },
    { "delete", "d", c_delete, 2, 0 },
    { "edit", "e", c_edit, 0, BANG | ARG_EDIT },
    { "ex", "ex", c_edit, 0, BANG | ARG_EDIT },
    { "file", "f", c_file, 0, 0 },
    { "global", "g", c_global, 2, ADEFALL | BANG | ARG_DELIM },
    { "insert", "i", c_insert, 1, AZERO | AEMPTY | BANG | NOAP | ARG_TEXT },
    { "join", "j", c_join, 2, BANG },
    { "list", "l", c_print, 2, 0 },
    { "map", "map", c_map, 0, BANG | ARG_MAP },
    { "mark", "ma", c_mark, 1, 0 },
    { "k", "k", c_mark, 1, 0 },
    { "move", "m", c_move, 2, 0 },
    { "next", "n", c_next, 0, BANG | ARG_EDIT },
    { "number", "nu", c_print, 2, 0 },
    { "#", "#", c_print, 2, 0 },
    { "open", "o", c_open, 1, AEMPTY | ARG_DELIM },
    { "preserve", "pre", c_preserve, 0, 0 },
    { "print", "p", c_print, 2, 0 },
    { "put", "pu", c_put, 1, AZERO | AEMPTY },
    { "quit", "q", c_quit, 0, BANG },
    { "read", "r", c_read, 1, AZERO | AEMPTY | ARG_READ },
    { "recover", "rec", c_recover, 0, BANG },
    { "rewind", "rew", c_rewind, 0, BANG },
    { "set", "se", c_set, 0, 0 },
    { "shell", "sh", c_shell, 0, 0 },
    { "source", "so", c_source, 0, 0 },
    { "substitute", "s", c_subst, 2, ARG_DELIM },
    { "&", "&", c_subst_amp, 2, 0 },
    { "~", "~", c_subst_tilde, 2, 0 },
    { "suspend", "su", c_suspend, 0, BANG },
    { "stop", "st", c_suspend, 0, BANG },
    { "tag", "ta", c_tag, 0, BANG },
    { "unabbreviate", "una", c_unabbrev, 0, ARG_MAP },
    { "undo", "u", c_undo, 0, 0 },
    { "unmap", "unm", c_unmap, 0, BANG | ARG_MAP },
    { "v", "v", c_global, 2, ADEFALL | ARG_DELIM },
    { "version", "ve", c_version, 0, 0 },
    { "visual", "vi", c_visual, 1, AEMPTY | ARG_VISUAL },
    { "write", "w", c_write, 2, ADEFALL | AEMPTY2 | BANG | ARG_WRITE | NOAP },
    { "wq", "wq", c_write, 2, ADEFALL | AEMPTY2 | BANG | ARG_WRITE | NOAP },
    { "xit", "x", c_xit, 2, ADEFALL | AEMPTY2 | BANG | NOAP },
    { "yank", "y", c_yank, 2, 0 },
    { "z", "z", c_z, 1, 0 },
    { "!", "!", c_bang, 2, AEMPTY2 | AOPT | ARG_BANG },
    { "<", "<", c_shift, 2, 0 },
    { ">", ">", c_shift, 2, 0 },
    { "\004", "\004", c_ctrld, 0, 0 },
    { "=", "=", c_equal, 1, AZERO | AEMPTY | A1DEFLAST },
    { "@", "@", c_exec, 2, 0 },
    { "*", "*", c_exec, 2, 0 },
    { NULL, NULL, NULL, 0, 0 }
};

static const CmdDef *lookup(const char *w, size_t n)
{
    const CmdDef *best = NULL;
    size_t bestmin = 0;
    for (const CmdDef *d = cmdtab; d->name; d++) {
        size_t ml = strlen(d->abbr);
        if (n >= ml && n <= strlen(d->name) && strncmp(d->name, w, n) == 0) {
            if (!best || ml > bestmin) {
                best = d;
                bestmin = ml;
            }
        }
    }
    return best;
}

/* ---------- small parsing helpers ---------- */

const char *skipbl(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

int parse_count(const char **pp, int *count)
{
    const char *p = skipbl(*pp);
    long v = 0;
    if (!isdigit((unsigned char)*p))
        return 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        if (v > 100000000)
            v = 100000000;
        p++;
    }
    if (v == 0)
        ex_err("Count must be positive");
    *count = (int)v;
    *pp = p;
    return 1;
}

/* flags: + - # p l, optionally blank separated */
int parse_flags(const char **pp, int *flags, int *off)
{
    const char *p = *pp;
    int any = 0;
    for (;;) {
        p = skipbl(p);
        if (*p == '#')
            *flags |= PF_NUM;
        else if (*p == 'p')
            *flags |= PF_PRINT;
        else if (*p == 'l')
            *flags |= PF_LIST;
        else if (*p == '+')
            (*off)++;
        else if (*p == '-')
            (*off)--;
        else
            break;
        any = 1;
        p++;
    }
    *pp = p;
    return any;
}

void end_args(const char *p)
{
    p = skipbl(p);
    if (*p)
        ex_err("Extra characters at end of command: %s", p);
}

/* apply a count to a command's addresses */
void apply_count(Cmd *c, int count)
{
    int last = c->a2;
    c->a1 = last;
    c->a2 = last + count - 1;
    if (c->a2 > nlines)
        c->a2 = nlines;
    if (c->a1 < 1 && nlines > 0)
        ex_err("Illegal address");
}

/* ---------- current column helpers ---------- */

int first_nonblank(int line)
{
    const char *s;
    int len, p = 0, last = 0;
    if (line < 1 || line > nlines)
        return 0;
    s = LT(line);
    len = LL(line);
    while (p < len && is_blankc((unsigned char)s[p]))
        p++;
    if (p < len)
        return p;
    if (len == 0)
        return 0;
    /* only blanks: the last character */
    p = 0;
    while (p < len) {
        last = p;
        p += ch_len(s, len, p);
    }
    return last;
}

void set_nonblank(void)
{
    if (cur < 1 || cur > nlines) {
        cur_off = 0;
        want_col = 0;
        return;
    }
    cur_off = first_nonblank(cur);
    want_col = col_of(LT(cur), LL(cur), cur_off);
}

/* ---------- line output ---------- */

static void out_line_text(int line, int flags)
{
    Str b;
    const char *s = LT(line);
    int len = LL(line), list, num, col = 0, width;
    list = (flags & PF_LIST) || OV(OP_LIST);
    num = (flags & PF_NUM) || OV(OP_NUMBER);
    width = cols > 1 ? cols - 1 : 79;
    str_init(&b);
    if (num) {
        str_fmt(&b, "%6d  ", line);
        col = 8;
    }
    for (int p = 0; p < len;) {
        int n = ch_len(s, len, p);
        if (!list && s[p] == '\t' && !vi_active) {
            str_addc(&b, '\t');
            col += (int)OV(OP_TABSTOP) - col % (int)(OV(OP_TABSTOP) ? OV(OP_TABSTOP) : 8);
        } else {
            Str piece;
            int w;
            str_init(&piece);
            w = char_disp(&piece, s, len, p, col, list);
            if (list && col + w > width && col > 0) {
                str_adds(&b, "\\\n");
                out_write(b.s, b.len);
                str_clear(&b);
                col = 0;
            }
            str_addn(&b, piece.s, piece.len);
            str_free(&piece);
            col += w;
        }
        p += n;
    }
    if (list)
        str_addc(&b, '$');
    str_addc(&b, '\n');
    out_write(b.s, b.len);
    str_free(&b);
}

int last_line_printed;

void print_lines(int a, int b, int flags)
{
    for (int i = a; i <= b; i++) {
        if (intr) {
            intr = 0;
            ex_err("Interrupted");
        }
        out_line_text(i, flags);
        last_line_printed = i;
        command_lines_printed++;
    }
}

/* ---------- report option ---------- */

void report_changes(long before_lines, const char *what, long n)
{
    (void)before_lines;
    if (in_global || in_exec_buffer)
        return;
    if (n >= OV(OP_REPORT) && n > 0)
        msg("%ld line%s %s", n, n == 1 ? "" : "s", what);
}

/* ---------- addressing ---------- */

typedef struct AddrState {
    int ctx_mark;        /* an address form that sets the context mark was used */
    int mark_line, mark_off;
} AddrState;

static void note_ctx(AddrState *as)
{
    if (!as->ctx_mark) {
        as->ctx_mark = 1;
        as->mark_line = cur;
        as->mark_off = cur_off;
    }
}

static int search_line(Regex *re, int from, int dir)
{
    int l = from;
    if (nlines == 0)
        ex_err("Pattern not found");
    for (int n = 0; n < nlines; n++) {
        l += dir;
        if (l > nlines) {
            if (!OV(OP_WRAPSCAN))
                break;
            l = 1;
        } else if (l < 1) {
            if (!OV(OP_WRAPSCAN))
                break;
            l = nlines;
        }
        if (intr) {
            intr = 0;
            ex_err("Interrupted");
        }
        if (re_exec(re, LT(l), LL(l), 0, 0, NULL))
            return l;
    }
    ex_err("Pattern not found");
}

/* one address: base and offsets.  Returns 1 if anything was parsed. */
static int parse_addr(const char **pp, int *val, AddrState *as)
{
    const char *p = skipbl(*pp);
    long v = 0;
    int got = 0;
    switch (*p) {
    case '.':
        v = cur;
        p++;
        got = 1;
        break;
    case '$':
        v = nlines;
        p++;
        got = 1;
        note_ctx(as);
        break;
    case '\'': {
        int idx, l, off;
        p++;
        idx = mark_index((unsigned char)*p);
        if (idx < 0)
            ex_err("Invalid mark name");
        if (!mark_get(idx, &l, &off))
            ex_err("Mark '%c' not set", *p);
        p++;
        v = l;
        got = 1;
        note_ctx(as);
        break;
    }
    case '/':
    case '?': {
        int dir = *p == '/' ? 1 : -1, delim = *p;
        Regex *re;
        p++;
        re = get_re(&p, delim, 1);
        v = search_line(re, cur, dir);
        re_free(re);
        got = 1;
        note_ctx(as);
        break;
    }
    default:
        if (isdigit((unsigned char)*p)) {
            while (isdigit((unsigned char)*p)) {
                v = v * 10 + (*p - '0');
                if (v > INT_MAX / 4)
                    v = INT_MAX / 4;
                p++;
            }
            got = 1;
            note_ctx(as);
        }
        break;
    }
    /* offsets */
    for (;;) {
        const char *q = skipbl(p);
        if (*q == '+' || *q == '-') {
            int sign = *q == '+' ? 1 : -1;
            long n = 1;
            q++;
            if (isdigit((unsigned char)*q)) {
                n = 0;
                while (isdigit((unsigned char)*q)) {
                    n = n * 10 + (*q - '0');
                    if (n > INT_MAX / 4)
                        n = INT_MAX / 4;
                    q++;
                }
            }
            if (!got) {
                v = cur;
                got = 1;
            }
            v += sign * n;
            p = q;
        } else if (got && isdigit((unsigned char)*q)) {
            long n = 0;
            while (isdigit((unsigned char)*q)) {
                n = n * 10 + (*q - '0');
                if (n > INT_MAX / 4)
                    n = INT_MAX / 4;
                q++;
            }
            v += n;
            p = q;
        } else
            break;
    }
    if (got) {
        if (v < 0 || v > nlines)
            ex_err("Illegal address: only %d line%s in the file", nlines, nlines == 1 ? "" : "s");
        *val = (int)v;
    }
    *pp = p;
    return got;
}

/* address list; fills c->a1, c->a2, c->naddr */
static void parse_addrs(const char **pp, Cmd *c, AddrState *as)
{
    const char *p = *pp;
    int list[2], n = 0, total = 0, sep = 0;
    for (;;) {
        int v, got;
        p = skipbl(p);
        if (*p == '%') {
            p++;
            list[0] = nlines > 0 ? 1 : 0;
            list[1] = nlines;
            n = 2;
            total += 2;
            p = skipbl(p);
            if (*p == ',' || *p == ';') {
                if (*p == ';')
                    cur = list[1];
                p++;
                sep = 1;
                /* the address after the separator */
                if (parse_addr(&p, &v, as)) {
                    list[0] = list[1];
                    list[1] = v;
                    total++;
                    continue;
                }
                list[0] = list[1];
                list[1] = cur;
                total++;
                continue;
            }
            break;
        }
        got = parse_addr(&p, &v, as);
        p = skipbl(p);
        if (*p == ',' || *p == ';') {
            if (!got)
                v = cur;
            if (n == 2)
                list[0] = list[1], n = 1;
            list[n++] = v;
            total++;
            if (*p == ';') {
                cur = v;
                if (cur >= 1)
                    set_nonblank();
            }
            p++;
            sep = 1;
            continue;
        }
        if (got) {
            if (n == 2)
                list[0] = list[1], n = 1;
            list[n++] = v;
            total++;
        } else if (sep) {
            if (n == 2)
                list[0] = list[1], n = 1;
            list[n++] = cur;
            total++;
        }
        break;
    }
    c->naddr = total > 2 ? 2 : total;
    if (n == 2) {
        c->a1 = list[0];
        c->a2 = list[1];
    } else if (n == 1)
        c->a1 = c->a2 = list[0];
    *pp = p;
}

/* the destination address of co, t and m */
int parse_dest_address(const char **pp)
{
    Cmd c;
    AddrState as;
    memset(&c, 0, sizeof c);
    memset(&as, 0, sizeof as);
    parse_addrs(pp, &c, &as);
    if (c.naddr == 0)
        ex_err("Destination address required");
    return c.a2;
}

/* ---------- argument extraction ---------- */

/* up to the first non-escaped terminator; esc is '\\' or ^V */
static const char *scan_arg(const char *p, int esc, int stop_bar, int stop_quote)
{
    while (*p && *p != '\n') {
        if ((unsigned char)*p == esc && p[1]) {
            p += 2;
            continue;
        }
        if (*p == '\026' && p[1]) {   /* ^V escapes too */
            p += 2;
            continue;
        }
        if (stop_bar && *p == '|')
            break;
        if (stop_quote && *p == '"')
            break;
        p++;
    }
    return p;
}

static const char *scan_to_newline(const char *p)
{
    while (*p && *p != '\n') {
        if (*p == '\\' && p[1]) {
            p += 2;
            continue;
        }
        p++;
    }
    return p;
}

/* ---------- execution context ---------- */

/* get the next line of text input for a/i/c */
char *ctx_text_line(Ctx *ctx, int lnum, const char *indent)
{
    if (ctx->first_text) {
        char *t = ctx->first_text;
        ctx->first_text = NULL;
        return t;
    }
    if (*ctx->p) {
        const char *e = strchr(ctx->p, '\n');
        char *t;
        if (!e)
            e = ctx->p + strlen(ctx->p);
        t = xstrndup(ctx->p, (size_t)(e - ctx->p));
        ctx->p = *e ? e + 1 : e;
        return t;
    }
    if (ctx->src)
        return ctx->src->get(ctx->src, 1, lnum, indent);
    return NULL;
}

static void exec_one(void *arg);

int ex_run(const char *text, Source *src)
{
    Ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.p = text;
    ctx.src = src;
    ctx.prev_bar = 0;
    do {
        ctx.first = (ctx.p == text);
        if (protect(exec_one, &ctx) < 0) {
            free(ctx.first_text);
            return -1;
        }
    } while (*ctx.p);
    return 0;
}

static void implied_print(Ctx *ctx, Cmd *c, AddrState *as)
{
    int flags = last_print_flags;
    if (c->naddr == 0) {
        if (!in_global) {
            if (cur + 1 > nlines)
                ex_err(nlines == 0 ? "Empty file" : "At end-of-file");
            cur++;
        }
        if (cur < 1)
            ex_err("Empty file");
        c->a1 = c->a2 = cur;
    } else {
        if (c->a1 > c->a2)
            ex_err("The second address is smaller than the first");
        if (c->a1 < 1)
            ex_err(nlines == 0 ? "Empty file" : "Illegal address");
    }
    print_lines(c->a1, c->a2, flags);
    cur = c->a2;
    set_nonblank();
    if (as->ctx_mark)
        mark_set(MARK_CONTEXT, as->mark_line, as->mark_off);
    (void)ctx;
}

static void validate_addrs(Cmd *c)
{
    const CmdDef *d = c->def;
    if (d->addrs == 0) {
        if (c->naddr > 0)
            ex_err("The %s command doesn't take an address", d->name);
        return;
    }
    if (c->naddr == 0) {
        if ((d->flags & ADEFALL)) {
            c->a1 = nlines > 0 ? 1 : 0;
            c->a2 = nlines;
        } else if (d->flags & A1DEFLAST)
            c->a1 = c->a2 = nlines;
        else
            c->a1 = c->a2 = cur;
    }
    if (d->addrs == 1) {
        c->a1 = c->a2;
        if (nlines == 0) {
            if (!(d->flags & AEMPTY) || c->a2 != 0)
                ex_err("Empty file");
            return;
        }
        if (c->a2 == 0 && !(d->flags & AZERO))
            ex_err("Illegal address: line 0");
        return;
    }
    /* two addresses */
    if (c->a1 > c->a2)
        ex_err("The second address is smaller than the first");
    if (nlines == 0) {
        if (!(d->flags & AEMPTY2) || c->a1 != 0 || c->a2 != 0)
            ex_err("Empty file");
        return;
    }
    if (c->a1 == 0 && !(d->flags & AZERO))
        ex_err("Illegal address: line 0");
}

static void exec_one(void *arg)
{
    Ctx *ctx = arg;
    const char *p = ctx->p, *name_start, *argstart, *argend;
    Cmd c;
    AddrState as;
    char word[64];
    size_t wl = 0;
    long cc_before;
    int saved_cur;

    memset(&c, 0, sizeof c);
    memset(&as, 0, sizeof as);
    c.ctx = ctx;
    /* 1, 2: colons and blanks */
    while (*p == ':' || *p == ' ' || *p == '\t')
        p++;
    /* 3: comments */
    if (*p == '"') {
        p = scan_to_newline(p);
        if (*p == '\n')
            p++;
        ctx->p = p;
        ctx->prev_bar = 0;
        return;
    }
    /* 4: addresses */
    saved_cur = cur;
    parse_addrs(&p, &c, &as);
    p = skipbl(p);
    /* 6: implied commands */
    if (*p == '|' || *p == '\n' || *p == 0) {
        int term = *p;
        if (*p)
            p++;
        ctx->p = p;
        if (term != '|') {
            if (mode != MODE_EX && vi_active && !in_global) {
                if (c.naddr > 0) {
                    cur = c.a2;
                    if (cur < 1 && nlines > 0)
                        cur = 1;
                    set_nonblank();
                }
                ctx->prev_bar = 0;
                return;
            }
            if (ctx->prev_bar) {
                ctx->prev_bar = 0;
                return;
            }
        }
        ctx->prev_bar = term == '|';
        implied_print(ctx, &c, &as);
        return;
    }
    (void)saved_cur;
    /* 7: command name */
    name_start = p;
    if (isalpha((unsigned char)*p)) {
        while (isalpha((unsigned char)p[wl]) && wl < sizeof word - 1) {
            word[wl] = p[wl];
            wl++;
        }
        word[wl] = 0;
        /* d[elete] followed by l p + - # */
        if (word[0] == 'd') {
            size_t k = 0;
            while (k < wl && k < 6 && word[k] == "delete"[k])
                k++;
            if (k < wl && strchr("lp", word[k])) {
                wl = k;
                word[wl] = 0;
            }
        } else if (word[0] == 'k' && wl == 2 && islower((unsigned char)word[1])) {
            wl = 1;
            word[1] = 0;
        } else if (word[0] == 's' && wl > 1 && strspn(word + 1, "gcrpl") == wl - 1) {
            wl = 1;
            word[1] = 0;
        }
        p += wl;
    } else {
        word[0] = *p;
        word[1] = 0;
        wl = 1;
        p++;
    }
    c.def = lookup(word, wl);
    if (!c.def) {
        ex_err("The %.*s command is unknown", (int)(wl ? wl : 1), name_start);
    }
    snprintf(c.name, sizeof c.name, "%s", c.def->name);
    /* '!' modifier */
    if ((c.def->flags & BANG) && *p == '!' && !(c.def->flags & ARG_WRITE && 0)) {
        c.bang = 1;
        p++;
    }
    /* 8-12: the rest of the command */
    argstart = p;
    switch (c.def->flags & ARG_MASK) {
    case ARG_BANG:
        argend = scan_to_newline(p);
        break;
    case ARG_READ: {
        const char *q = skipbl(p);
        if (*q == '!') {
            c.shell = 1;
            argstart = q + 1;
            argend = scan_to_newline(argstart);
        } else
            argend = scan_arg(p, '\\', 1, 1);
        break;
    }
    case ARG_WRITE: {
        const char *q = skipbl(p);
        if (q != p && *q == '!') {
            c.shell = 1;
            argstart = q + 1;
            argend = scan_to_newline(argstart);
        } else
            argend = scan_arg(p, '\\', 1, 1);
        break;
    }
    case ARG_VISUAL:
        if (!vi_active) {
            argend = scan_arg(p, '\\', 1, 1);
            break;
        }
        if (*p == '!') {
            c.bang = 1;
            p++;
            argstart = p;
        }
        /* fall through: edit-style arguments */
        /* FALLTHROUGH */
    case ARG_EDIT: {
        const char *q = skipbl(p);
        if (*q == '+') {
            while (*q && *q != '\n' && *q != ' ' && *q != '\t') {
                if (*q == '\\' && q[1])
                    q++;
                q++;
            }
        }
        argend = scan_arg(q, '\\', 1, 1);
        break;
    }
    case ARG_DELIM: {
        const char *q = skipbl(p);
        int d = (unsigned char)*q;
        if (d && !isalnum(d) && d != '"' && d != '\n' && d != '\\' && d != '|') {
            q = re_skip(q + 1, d);
            if ((unsigned char)*q == d) {
                q++;
                if (c.def->fn == c_subst) {
                    while (*q && *q != '\n' && (unsigned char)*q != d) {
                        if (*q == '\\' && q[1])
                            q++;
                        q++;
                    }
                    if ((unsigned char)*q == d)
                        q++;
                    argend = scan_arg(q, '\\', 1, 1);
                } else if (c.def->fn == c_global)
                    argend = scan_to_newline(q);
                else
                    argend = scan_arg(q, '\\', 1, 1);
            } else if (c.def->fn == c_global)
                argend = scan_to_newline(q);
            else
                argend = q;
        } else
            argend = scan_arg(p, '\\', 1, 1);
        break;
    }
    case ARG_MAP:
        argend = scan_arg(p, '\026', 1, 1);
        break;
    default:
        argend = scan_arg(p, '\\', 1, 1);
        break;
    }
    c.arg = xstrndup(argstart, (size_t)(argend - argstart));
    p = argend;
    c.term_bar = *p == '|';
    if ((c.def->flags & ARG_TEXT) && *p == '|') {
        const char *e = scan_to_newline(p + 1);
        ctx->first_text = xstrndup(p + 1, (size_t)(e - p - 1));
        p = e;
    }
    if (*p == '"') {
        p = scan_to_newline(p);
        c.term_bar = 0;
    }
    if (*p == '|' || *p == '\n')
        p++;
    ctx->p = p;
    ctx->prev_bar = c.term_bar;

    validate_addrs(&c);
    if (!(c.def->fn == c_quit || c.def->fn == c_write || c.def->fn == c_xit))
        ex_quit_pending = 0;
    cc_before = change_count;
    command_lines_printed = 0;
    last_line_printed = 0;
    c.flags = 0;
    c.flagoff = 0;
    undo_open();
    {
        jmp_buf jb, *saved = errjmp;
        errjmp = &jb;
        if (setjmp(jb)) {
            errjmp = saved;
            undo_close();
            free(c.arg);
            ex_err_silent();
        }
        c.def->fn(&c);
        errjmp = saved;
    }
    undo_close();
    if (as.ctx_mark)
        mark_set(MARK_CONTEXT, as.mark_line, as.mark_off);
    /* flags */
    if (c.flags & (PF_NUM | PF_LIST | PF_PRINT))
        last_print_flags = c.flags & (PF_NUM | PF_LIST);
    if (c.flagoff) {
        int nc = cur + c.flagoff;
        if (nc < 1 || nc > nlines)
            ex_err("Illegal address");
        cur = nc;
        set_nonblank();
    }
    if ((c.flags & (PF_NUM | PF_LIST | PF_PRINT)) || c.flagoff) {
        if (cur >= 1 && cur <= nlines && !(c.no_flag_print) &&
            !(command_lines_printed && last_line_printed == cur))
            print_lines(cur, cur, c.flags);
    } else if (OV(OP_AUTOPRINT) && change_count != cc_before && !(c.def->flags & NOAP) &&
               !c.no_autoprint && mode == MODE_EX && !vi_active && !in_global && !in_exec_buffer &&
               !c.term_bar && *argend != '|' && !silent && cur >= 1 && cur <= nlines &&
               !(command_lines_printed && last_line_printed == cur)) {
        print_lines(cur, cur, 0);
    }
    free(c.arg);
}

/* ---------- input sources ---------- */

typedef struct FdReader {
    int fd;
    char buf[8192];
    int pos, len;
    int eof;
} FdReader;

static FdReader stdin_reader = { 0, { 0 }, 0, 0, 0 };

/* characters typed ahead in visual mode before switching to ex mode */
void ex_typeahead(const char *s, int n)
{
    FdReader *r = &stdin_reader;
    if (n <= 0)
        return;
    if (r->pos > 0) {
        memmove(r->buf, r->buf + r->pos, (size_t)(r->len - r->pos));
        r->len -= r->pos;
        r->pos = 0;
    }
    for (int i = 0; i < n && r->len < (int)sizeof r->buf; i++)
        r->buf[r->len++] = s[i] == '\r' ? '\n' : s[i];
}

/* entering visual mode: give buffered terminal input back as keys */
void ex_typeahead_to_keys(void)
{
    FdReader *r = &stdin_reader;
    if (!interactive || r->pos >= r->len)
        return;
    key_push(r->buf + r->pos, r->len - r->pos, 0, 0);
    r->pos = r->len = 0;
}

void restore_stdin_position(void)
{
    FdReader *r = &stdin_reader;
    if (r->len > r->pos && !interactive) {
        if (lseek(0, -(off_t)(r->len - r->pos), SEEK_CUR) >= 0)
            r->pos = r->len = 0;
    }
}

/* read one line; returns 0 at EOF (no data), 1 = line ending in newline,
 * 2 = data without newline (EOF or tty eof character) */
static int fd_read_line(FdReader *r, Str *out)
{
    int got = 0;
    for (;;) {
        if (r->pos >= r->len) {
            ssize_t n;
            if (r->eof && !interactive)
                return got ? 2 : 0;
            out_flush();
            n = read(r->fd, r->buf, sizeof r->buf);
            if (n < 0) {
                if (errno == EINTR) {
                    if (intr && interactive) {
                        intr = 0;
                        str_clear(out);
                        return -1;
                    }
                    if (got_hup)
                        return 0;
                    continue;
                }
                return got ? 2 : 0;
            }
            if (n == 0) {
                r->eof = 1;
                return got ? 2 : 0;
            }
            r->pos = 0;
            r->len = (int)n;
            if (interactive) {
                /* canonical mode: one read is one line or an eof-terminated chunk */
                int nl = 0;
                for (int i = 0; i < r->len; i++)
                    if (r->buf[i] == '\n')
                        nl = 1;
                if (!nl) {
                    str_addn(out, r->buf, (size_t)r->len);
                    r->pos = r->len;
                    return 2;
                }
            }
        }
        while (r->pos < r->len) {
            char ch = r->buf[r->pos++];
            if (ch == '\n')
                return 1;
            if (ch)
                str_addc(out, ch);
            got = 1;
        }
    }
}

static void prompt_out(const char *s)
{
    if (silent)
        return;
    out_str(s);
    out_flush();
}

/* overwrite the text input prompt (line number and autoindent) */
static void erase_prompt(int lnum, const char *indent)
{
    int w = OV(OP_NUMBER) ? 8 : 0, ts = (int)OV(OP_TABSTOP);
    (void)lnum;
    for (const char *p = indent ? indent : ""; *p; p++)
        w = *p == '\t' ? w + ts - w % ts : w + 1;
    out_str("\r");
    for (int i = 0; i < w + 2; i++)
        out_str(" ");
    out_str("\r");
}

/* standard input source (terminal or not) */
char *src_tty_get(Source *s, int textmode, int lnum, const char *indent)
{
    Str line;
    int r;
    (void)s;
    str_init(&line);
    if (!silent && interactive) {
        if (textmode) {
            char nb[32];
            if (OV(OP_NUMBER)) {
                snprintf(nb, sizeof nb, "%6d  ", lnum);
                prompt_out(nb);
            }
            if (indent)
                prompt_out(indent);
        } else if (OV(OP_PROMPT))
            prompt_out(":");
    }
    for (;;) {
        size_t before = line.len;
        r = fd_read_line(&stdin_reader, &line);
        if (r == -1) {
            /* interrupted: discard the partial line */
            str_free(&line);
            if (!silent)
                out_str("\nInterrupted\n");
            return xstrdup(textmode ? "\003" : "\"");
        }
        if (r == 0) {
            if (line.len == 0 && interactive && before == 0) {
                /* the eof character typed at the beginning of a line */
                str_free(&line);
                if (stdin_reader.eof && 0)
                    return NULL;
                stdin_reader.eof = 0;
                if (!textmode && !silent)
                    out_str("\n");
                else if (textmode && !silent)
                    erase_prompt(lnum, indent);
                return xstrdup("\004");
            }
            if (line.len == 0) {
                str_free(&line);
                return NULL;
            }
            break;
        }
        if (r == 2 && interactive) {
            /* eof character after some input */
            if (textmode && line.len == 1 && (line.s[0] == '0' || line.s[0] == '^')) {
                char *t = xmalloc(3);
                t[0] = '\004';
                t[1] = line.s[0];
                t[2] = 0;
                str_free(&line);
                if (!silent)
                    erase_prompt(lnum, indent), out_str("  \r");
                return t;
            }
            continue;   /* no special meaning: keep reading */
        }
        if (r == 2)
            break;
        /* a line ending in a backslash continues in command mode */
        if (!textmode && line.len > 0 && line.s[line.len - 1] == '\\') {
            str_addc(&line, '\n');
            continue;
        }
        break;
    }
    return str_detach(&line);
}

void src_fd(Source *s, int fd)
{
    memset(s, 0, sizeof *s);
    s->fd = fd;
    s->kind = SRC_TTY;
    s->get = src_tty_get;
}

/* ---------- top level ---------- */

void ex_loop(void)
{
    Source src;
    src_fd(&src, 0);
    cur_source = &src;
    for (;;) {
        char *line;
        int r;
        mode = MODE_EX;
        if (got_hup)
            hup_exit();
        intr = 0;
        line = src.get(&src, 0, 0, NULL);
        if (!line)
            hup_exit();
        r = ex_run(line, &src);
        free(line);
        if (r < 0 && batch_stdin)
            ex_exit(1);
        if (r < 0)
            exit_status = 1;
    }
}
