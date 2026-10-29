/*
 * cmds.c - ex commands that examine or change the edit buffer.
 */
#include "ex.h"
#include "cmd.h"

/* parse "[ buffer]" for d and y: a letter, after optional blanks */
static int parse_bufname(const char **pp, int allow_digit)
{
    const char *p = *pp, *q = skipbl(p);
    int c = (unsigned char)*q;
    if (q == p && (c == 'l' || c == 'p'))
        return 0;       /* "dp", "dl": flags */
    if (isalpha(c) || (allow_digit && isdigit(c)) || (allow_digit && c == '"')) {
        *pp = q + 1;
        return c;
    }
    return 0;
}

static void report_net(long a0, long d0)
{
    long a = rep_added - a0, d = rep_deleted - d0, net = a - d;
    if (in_global || in_exec_buffer)
        return;
    if (net < 0)
        net = -net;
    if (net >= OV(OP_REPORT) && net > 0)
        msg("%ld %s line%s", net, a > d ? "more" : "fewer", net == 1 ? "" : "s");
}

/* ---------- print, list, number ---------- */

void c_print(Cmd *c)
{
    const char *p = c->arg;
    int count, flags = 0, off = 0, base = 0;
    if (parse_count(&p, &count))
        apply_count(c, count);
    parse_flags(&p, &flags, &off);
    end_args(p);
    if (c->def->name[0] == 'l')
        base = PF_LIST;
    else if (c->def->name[0] == 'n' || c->def->name[0] == '#')
        base = PF_NUM;
    if (c->a1 < 1)
        ex_err("Empty file");
    print_lines(c->a1, c->a2, base | flags);
    if (cur != c->a2) {
        cur = c->a2;
        set_nonblank();
    }
    c->flags = base | flags | PF_PRINT;
    c->flagoff = off;
}

/* ---------- delete, yank, put ---------- */

void c_delete(Cmd *c)
{
    const char *p = c->arg;
    int reg, count, n;
    reg = parse_bufname(&p, 0);
    if (parse_count(&p, &count))
        apply_count(c, count);
    parse_flags(&p, &c->flags, &c->flagoff);
    end_args(p);
    n = c->a2 - c->a1 + 1;
    reg_store_lines(reg, c->a1, c->a2, 0);
    buf_delete(c->a1, c->a2);
    cur = c->a1 <= nlines ? c->a1 : nlines;
    set_nonblank();
    report_changes(0, "deleted", n);
}

void c_yank(Cmd *c)
{
    const char *p = c->arg;
    int reg, count, n;
    reg = parse_bufname(&p, 0);
    if (parse_count(&p, &count))
        apply_count(c, count);
    end_args(p);
    n = c->a2 - c->a1 + 1;
    reg_store_lines(reg, c->a1, c->a2, 0);
    if (!in_global && !in_exec_buffer && n >= OV(OP_REPORT) + 1)
        msg("%d lines yanked", n);
}

void c_put(Cmd *c)
{
    const char *p = skipbl(c->arg);
    int reg = 0;
    Reg *r;
    if (*p) {
        reg = (unsigned char)*p++;
        if (!isalnum(reg) && reg != '"')
            ex_err("Invalid buffer name");
    }
    end_args(p);
    r = reg_get(reg);
    if (!r)
        ex_err("Invalid buffer name");
    if (r->n == 0)
        ex_err("Buffer %c is empty", reg ? reg : '"');
    {
        long a0 = rep_added, d0 = rep_deleted;
        buf_insert(c->a1, r->t, r->l, r->n);
        cur = c->a1 + r->n;
        set_nonblank();
        report_net(a0, d0);
    }
}

/* ---------- copy and move ---------- */

int parse_dest_address(const char **pp);

void c_copy(Cmd *c)
{
    const char *p = c->arg;
    int dest, n = c->a2 - c->a1 + 1;
    char **t;
    int *l;
    long a0 = rep_added, d0 = rep_deleted;
    dest = parse_dest_address(&p);
    parse_flags(&p, &c->flags, &c->flagoff);
    end_args(p);
    t = xmalloc((size_t)n * sizeof(char *));
    l = xmalloc((size_t)n * sizeof(int));
    for (int i = 0; i < n; i++) {
        t[i] = xstrndup(LT(c->a1 + i), (size_t)LL(c->a1 + i));
        l[i] = LL(c->a1 + i);
    }
    buf_insert(dest, t, l, n);
    for (int i = 0; i < n; i++)
        free(t[i]);
    free(t);
    free(l);
    cur = dest + n;
    set_nonblank();
    report_net(a0, d0);
}

void buf_move(int a1, int a2, int dest);

void c_move(Cmd *c)
{
    const char *p = c->arg;
    int dest, n = c->a2 - c->a1 + 1;
    dest = parse_dest_address(&p);
    parse_flags(&p, &c->flags, &c->flagoff);
    end_args(p);
    if (dest >= c->a1 && dest <= c->a2)
        ex_err("Destination line is inside the move range");
    buf_move(c->a1, c->a2, dest);
    cur = dest < c->a1 ? dest + n : dest;
    set_nonblank();
    report_changes(0, "moved", n);
}

/* ---------- join ---------- */

void join_lines(int a, int b, int bang)
{
    Str s;
    str_init(&s);
    str_addn(&s, LT(a), (size_t)LL(a));
    for (int i = a + 1; i <= b; i++) {
        const char *t = LT(i);
        int len = LL(i), k = 0;
        if (bang) {
            str_addn(&s, t, (size_t)len);
            continue;
        }
        while (k < len && (t[k] == ' ' || t[k] == '\t'))
            k++;
        if (k == len)
            continue;
        if ((s.len > 0 && is_blankc((unsigned char)s.s[s.len - 1])) || t[k] == ')')
            ;
        else if (s.len > 0 && s.s[s.len - 1] == '.')
            str_adds(&s, "  ");
        else
            str_addc(&s, ' ');
        str_addn(&s, t + k, (size_t)(len - k));
    }
    buf_change(a, s.s ? s.s : "", (int)s.len);
    if (b > a)
        buf_delete(a + 1, b);
    str_free(&s);
}

void c_join(Cmd *c)
{
    const char *p = c->arg;
    int count, has = parse_count(&p, &count);
    parse_flags(&p, &c->flags, &c->flagoff);
    end_args(p);
    if (has) {
        if (c->naddr <= 1)
            c->a2 = c->a1 + count;
        else
            c->a2 = c->a2 + count - 1;
    } else if (c->naddr <= 1)
        c->a2 = c->a1 + 1;
    if (c->a2 > nlines)
        c->a2 = nlines;
    if (c->a1 >= c->a2 && c->naddr < 2)
        ex_err("No following lines to join");
    join_lines(c->a1, c->a2, c->bang);
    cur = c->a1;
    set_nonblank();
}

/* ---------- shifts ---------- */

static int indent_width(const char *s, int len, int *nchars)
{
    int col = 0, k = 0, ts = (int)OV(OP_TABSTOP);
    while (k < len && (s[k] == ' ' || s[k] == '\t')) {
        if (s[k] == '\t')
            col += ts - col % ts;
        else
            col++;
        k++;
    }
    if (nchars)
        *nchars = k;
    return col;
}

void make_indent(Str *b, int width)
{
    int ts = (int)OV(OP_TABSTOP);
    while (width >= ts) {
        str_addc(b, '\t');
        width -= ts;
    }
    while (width-- > 0)
        str_addc(b, ' ');
}

void shift_line(int i, int amount)
{
    const char *s = LT(i);
    int len = LL(i), k, w;
    Str b;
    if (len == 0)
        return;
    w = indent_width(s, len, &k);
    if (amount < 0 && w == 0)
        return;
    w += amount;
    if (w < 0)
        w = 0;
    str_init(&b);
    make_indent(&b, w);
    str_addn(&b, s + k, (size_t)(len - k));
    buf_change(i, b.s ? b.s : "", (int)b.len);
    str_free(&b);
}

void shift_lines(int a, int b, int dir, int times)
{
    int amount = dir * times * (int)OV(OP_SHIFTWIDTH);
    reg_store_lines(0, a, b, 0);
    for (int i = a; i <= b; i++)
        shift_line(i, amount);
}

void c_shift(Cmd *c)
{
    const char *p = c->arg;
    int times = 1, count, ch = c->def->name[0];
    for (;;) {
        p = skipbl(p);
        if (*p != ch)
            break;
        times++;
        p++;
    }
    if (parse_count(&p, &count))
        apply_count(c, count);
    parse_flags(&p, &c->flags, &c->flagoff);
    end_args(p);
    shift_lines(c->a1, c->a2, ch == '>' ? 1 : -1, times);
    cur = c->a2;
    set_nonblank();
    report_changes(0, ch == '>' ? ">ed" : "<ed", c->a2 - c->a1 + 1);
}

/* ---------- marks ---------- */

void c_mark(Cmd *c)
{
    const char *p = skipbl(c->arg);
    int idx;
    if (!*p)
        ex_err("Mark name required");
    idx = mark_index((unsigned char)*p);
    if (idx < 0)
        ex_err("Invalid mark name '%c'", *p);
    end_args(p + 1);
    mark_set(idx, c->a1, first_nonblank(c->a1));
}

/* ---------- undo ---------- */

void c_undo(Cmd *c)
{
    int fc, bd, what;
    long a0 = rep_added, d0 = rep_deleted;
    end_args(c->arg);
    if (in_global)
        ex_err("The undo command cannot be used in a global command");
    set_context_mark();
    if (do_undo(&fc, &bd, &what) < 0)
        ex_err("Nothing to undo");
    if (fc > 0 && fc <= nlines)
        cur = fc;
    else if (bd >= 1 && bd <= nlines)
        cur = bd;
    else
        cur = nlines > 0 ? 1 : 0;
    set_nonblank();
    report_net(a0, d0);
}

/* ---------- = and ^D ---------- */

void c_equal(Cmd *c)
{
    const char *p = c->arg;
    char buf[32];
    parse_flags(&p, &c->flags, &c->flagoff);
    end_args(p);
    snprintf(buf, sizeof buf, "%d\n", c->a1);
    out_str(buf);
    c->no_flag_print = 0;
}

void c_ctrld(Cmd *c)
{
    int n = (int)OV(OP_SCROLL);
    end_args(c->arg);
    if (cur >= nlines)
        ex_err("Already at end-of-file");
    if (cur + n > nlines)
        n = nlines - cur;
    print_lines(cur + 1, cur + n, last_print_flags & 0);
    cur += n;
    set_nonblank();
}

/* ---------- z ---------- */

void c_z(Cmd *c)
{
    const char *p = c->arg;
    int bang = 0, type = 0, ntype = 0, count = 0, has_count, line, flags = 0, off = 0;
    if (*p == '!') {
        bang = 1;
        p++;
    }
    if (*p == ' ' || *p == '\t') {
        const char *q = skipbl(p);
        if (*q == '-' || *q == '+' || *q == '.' || *q == '=' || *q == '^')
            ex_err("No blanks allowed before the z type");
    }
    if (*p == '-' || *p == '+' || *p == '.' || *p == '=' || *p == '^') {
        type = *p;
        while (*p == type) {
            ntype++;
            p++;
        }
        if ((type == '.' || type == '=') && ntype > 1)
            ex_err("Only one %c allowed", type);
    }
    has_count = parse_count(&p, &count);
    parse_flags(&p, &flags, &off);
    end_args(p);
    if (has_count)
        opt_window_set(count), opts[OP_WINDOW].changed = 1;
    else
        count = bang ? rows - 1 : 2 * (int)OV(OP_SCROLL);
    line = c->a1;
    if (c->naddr == 0 && !type) {
        if (cur + 1 > nlines)
            ex_err("At end-of-file");
        line = cur + 1;
    }
    if (line < 1)
        line = 1;
    set_context_mark();
    switch (type) {
    case 0:
        break;
    case '-':
        line -= ntype * count - 1;
        if (line < 1)
            ex_err("Illegal address");
        break;
    case '+':
        line += (ntype - 1) * count + 1;
        if (line > nlines)
            ex_err("At end-of-file");
        break;
    case '^':
        line -= (ntype + 1) * count - 1;
        if (line < 1)
            ex_err("Illegal address");
        break;
    case '.':
    case '=': {
        int n, a, b;
        if (count == 0)
            break;
        n = (has_count || bang) ? (count - 1) / 2 : (count - 3) / 2;
        if (n >= 3) {
            a = line - n;
            if (a < 1)
                a = 1;
            if (a < line)
                print_lines(a, line - 1, flags);
        }
        if (type == '=') {
            int w = cols / 2 < 40 ? cols / 2 : 40;
            Str d;
            str_init(&d);
            for (int i = 0; i < w; i++)
                str_addc(&d, '-');
            str_addc(&d, '\n');
            out_write(d.s, d.len);
            print_lines(line, line, flags);
            out_write(d.s, d.len);
            str_free(&d);
        } else
            print_lines(line, line, flags);
        if (n >= 3 && count >= 3) {
            b = line + n;
            if (b > nlines)
                b = nlines;
            if (b > line)
                print_lines(line + 1, b, flags);
        }
        cur = line;
        if (type == '.' && last_line_printed > 0)
            cur = last_line_printed;
        if (type == '=')
            cur = line;
        set_nonblank();
        return;
    }
    }
    {
        int b = line + count - 1;
        if (b > nlines)
            b = nlines;
        if (count > 0)
            print_lines(line, b, flags);
        cur = count > 0 ? b : line;
        set_nonblank();
    }
}

/* ---------- text input: append, insert, change ---------- */

static int line_indent(int l)
{
    return (l >= 1 && l <= nlines) ? indent_width(LT(l), LL(l), NULL) : 0;
}

/* ex text input mode; returns the number of lines added */
static int text_input(Cmd *c, int after, int indent0)
{
    int ai = (OV(OP_AUTOINDENT) != 0) ^ c->bang;
    int indent = 0, added = 0, keep_next = -1;
    if (silent || in_global)
        ai = 0;
    if (ai)
        indent = indent0;
    for (;;) {
        Str ind, full;
        char *t;
        int this_indent = indent;
        str_init(&ind);
        if (ai)
            make_indent(&ind, indent);
    reprompt:
        t = ctx_text_line(c->ctx, after + 1, ai ? (ind.s ? ind.s : "") : NULL);
        if (!t) {
            str_free(&ind);
            break;
        }
        if (t[0] == '\003' && t[1] == 0) {
            free(t);
            str_free(&ind);
            break;
        }
        if (t[0] == '\004' && interactive && c->ctx->src) {
            if (ai && t[1] == 0 && this_indent > 0) {
                int sw = (int)OV(OP_SHIFTWIDTH);
                this_indent = ((this_indent - 1) / sw) * sw;
                indent = this_indent;
            } else if (ai && (t[1] == '0' || t[1] == '^')) {
                if (t[1] == '^')
                    keep_next = indent;
                this_indent = 0;
                if (t[1] == '0')
                    indent = 0;
            } else if (t[1] != 0) {
                /* "0" or "^" without autoindent: ordinary text */
                memmove(t, t + 1, strlen(t));
                goto have_text;
            }
            free(t);
            str_clear(&ind);
            if (ai)
                make_indent(&ind, this_indent);
            goto reprompt;
        }
    have_text:
        if (strcmp(t, ".") == 0) {
            free(t);
            str_free(&ind);
            break;
        }
        str_init(&full);
        if (ai && t[0])
            str_addn(&full, ind.s ? ind.s : "", ind.len);
        str_adds(&full, t);
        if (ai && t[0])
            indent = indent_width(full.s, (int)full.len, NULL);
        else if (ai)
            indent = this_indent;
        if (keep_next >= 0) {
            indent = keep_next;
            keep_next = -1;
        }
        buf_insert1(after, full.s ? full.s : "", (int)full.len);
        after++;
        added++;
        cur = after;
        free(t);
        str_free(&full);
        str_free(&ind);
    }
    c->no_autoprint = 1;
    return added;
}

void c_append(Cmd *c)
{
    end_args(c->arg);
    if (!text_input(c, c->a1, line_indent(c->a1)))
        cur = c->a1 ? c->a1 : (nlines > 0 ? 1 : 0);
    set_nonblank();
}

void c_insert(Cmd *c)
{
    int after = c->a1 > 0 ? c->a1 - 1 : 0;
    end_args(c->arg);
    if (!text_input(c, after, line_indent(c->a1)))
        cur = after >= 1 ? after : (nlines > 0 ? 1 : 0);
    set_nonblank();
}

void c_change(Cmd *c)
{
    const char *p = c->arg;
    int count, after, ind;
    if (parse_count(&p, &count))
        apply_count(c, count);
    end_args(p);
    ind = line_indent(c->a1);
    reg_store_lines(0, c->a1, c->a2, 0);
    buf_delete(c->a1, c->a2);
    after = c->a1 - 1;
    if (!text_input(c, after, ind))
        cur = after >= 1 ? after : (nlines > 0 ? 1 : 0);
    if (cur > nlines)
        cur = nlines;
    set_nonblank();
}

/* ---------- global ---------- */

void c_global(Cmd *c)
{
    const char *p = skipbl(c->arg);
    int inverse = c->def->name[0] == 'v' || c->bang;
    Regex *re;
    unsigned long *ids;
    int nids = 0, executed = 0, lastmarked = 0;
    long gen = buffer_generation, a0 = rep_added, d0 = rep_deleted;
    Str cmds;
    if (in_global)
        ex_err("The global command cannot be used in a global command");
    if (*p && !isalnum((unsigned char)*p) && *p != '\\' && *p != '|' && *p != '"') {
        int d = (unsigned char)*p++;
        re = get_re(&p, d, 1);
    } else {
        if (!last_re)
            ex_err("No previous regular expression");
        re = last_re;
        re_ref(re);
    }
    str_init(&cmds);
    for (; *p; p++) {
        if (*p == '\\' && p[1] == '\n') {
            str_addc(&cmds, '\n');
            p++;
        } else
            str_addc(&cmds, *p);
    }
    if (!search_dir)
        search_dir = 1;
    ids = xmalloc((size_t)(c->a2 - c->a1 + 1 > 0 ? c->a2 - c->a1 + 1 : 1) * sizeof *ids);
    for (int l = c->a1; l <= c->a2 && l >= 1; l++) {
        int m;
        if (intr) {
            intr = 0;
            free(ids);
            re_free(re);
            str_free(&cmds);
            ex_err("Interrupted");
        }
        m = re_exec(re, LT(l), LL(l), 0, 0, NULL);
        if (m != inverse) {
            ids[nids++] = lp(l)->id;
            lastmarked = l;
        }
    }
    re_free(re);
    set_context_mark();
    {
        const char *q = skipbl(cmds.s ? cmds.s : "");
        int empty = *q == 0;
        if (empty && !vi_active) {
            str_free(&cmds);
            str_adds(&cmds, "p");
        }
        if (!empty || !vi_active) {
            in_global = 1;
            for (int i = 0; i < nids; i++) {
                int l = line_of_id(ids[i]);
                int r;
                if (!l)
                    continue;
                if (intr) {
                    intr = 0;
                    in_global = 0;
                    free(ids);
                    str_free(&cmds);
                    ex_err("Interrupted");
                }
                cur = l;
                set_nonblank();
                r = ex_run(cmds.s, NULL);
                executed++;
                if (r < 0 || buffer_generation != gen) {
                    in_global = 0;
                    free(ids);
                    str_free(&cmds);
                    if (buffer_generation != gen)
                        ex_err("The edit buffer was replaced during the global command");
                    ex_err_silent();
                }
            }
            in_global = 0;
        }
    }
    if (!executed && lastmarked) {
        cur = lastmarked;
        set_nonblank();
    }
    free(ids);
    str_free(&cmds);
    report_net(a0, d0);
    c->no_autoprint = 1;
}

/* ---------- substitute ---------- */

static Regex *last_s_re;
static Repl *last_s_repl;
static int last_s_g, last_s_c;

static int confirm_subst(int line, const char *done, int donelen, const char *rest, int restlen,
                         int mso, int meo)
{
    Str l1, l2;
    int col = 0, r;
    str_init(&l1);
    str_init(&l2);
    str_addn(&l1, done, (size_t)donelen);
    str_addn(&l1, rest, (size_t)restlen);
    (void)line;
    /* blanks under the text before the match, carets under the match */
    for (int p = 0; p < (int)l1.len;) {
        int n = ch_len(l1.s, (int)l1.len, p);
        int w = ch_width(l1.s, (int)l1.len, p, col);
        int pos_in_rest = p - donelen;
        if (p < donelen || pos_in_rest < mso) {
            if (l1.s[p] == '\t' && !OV(OP_LIST))
                str_addc(&l2, '\t');
            else
                for (int k = 0; k < w; k++)
                    str_addc(&l2, ' ');
        } else if (pos_in_rest < meo) {
            for (int k = 0; k < w; k++)
                str_addc(&l2, '^');
        } else
            break;
        col += w;
        p += n;
    }
    if (meo == mso)
        str_addc(&l2, '^');
    r = vi_active ? vs_confirm(l1.s ? l1.s : "", l2.s ? l2.s : "") : 0;
    if (!vi_active) {
        char *ans;
        out_write(l1.s ? l1.s : "", l1.len);
        out_str("\n");
        out_write(l2.s ? l2.s : "", l2.len);
        out_str("\n");
        out_flush();
        if (cur_source)
            ans = cur_source->get(cur_source, 1, 0, NULL);
        else
            ans = NULL;
        r = ans && (ans[0] == 'y' || ans[0] == 'Y');
        if (ans && ans[0] == '\003')
            intr = 1;
        free(ans);
    }
    str_free(&l1);
    str_free(&l2);
    return r;
}

/* kind: 0 = s, 1 = &, 2 = ~ */
static void subst(Cmd *c, int kind)
{
    const char *p = c->arg;
    Regex *re = NULL;
    Repl *rp = NULL;
    int gflag = 0, cflag = 0, rflag = 0, count, nsubs = 0, nlines_changed = 0, lastline = 0;
    int own_rp = 0;
    if (kind == 0) {
        const char *q = skipbl(p);
        int d = (unsigned char)*q;
        if (d && !isalnum(d) && d != '\\' && d != '|' && d != '"' && d != '\n' && d != ' ' && d != '\t') {
            const char *pat = q + 1, *pe = re_skip(pat, d);
            const char *rs = pe, *re_end;
            int pat_empty = pe == pat, repl_empty;
            if ((unsigned char)*rs == d)
                rs++;
            /* is the replacement empty? */
            repl_empty = (*rs == 0 || (unsigned char)*rs == d || *rs == '\n');
            if (pat_empty && repl_empty && (unsigned char)*pe == d) {
                /* "s//" or "s///": repeat the last substitution */
                if (!last_s_re || !last_s_repl)
                    ex_err("No previous substitute command");
                re = last_s_re;
                re_ref(re);
                rp = last_s_repl;
                p = rs;
                if ((unsigned char)*p == d)
                    p++;
            } else {
                p = pat;
                re = get_re(&p, d, 1);
                rp = repl_compile(p, d, &re_end, (int)OV(OP_MAGIC));
                own_rp = 1;
                p = re_end;
                if ((unsigned char)*p == d)
                    p++;
                re_ref(re);
                re_free(last_s_re);
                last_s_re = re;
                repl_free(last_s_repl);
                last_s_repl = rp;
                own_rp = 0;
            }
        } else {
            if (!last_s_re || !last_s_repl)
                ex_err("No previous substitute command");
            re = last_s_re;
            re_ref(re);
            rp = last_s_repl;
            p = q;
        }
    } else {
        if (!last_s_repl)
            ex_err("No previous substitute command");
        if (kind == 1) {
            if (!last_s_re)
                ex_err("No previous substitute command");
            re = last_s_re;
        } else {
            if (!last_re)
                ex_err("No previous regular expression");
            re = last_re;
        }
        re_ref(re);
        rp = last_s_repl;
    }
    /* options */
    for (;;) {
        p = skipbl(p);
        if (*p == 'g')
            gflag ^= 1;
        else if (*p == 'c')
            cflag ^= 1;
        else if (*p == 'r')
            rflag = 1;
        else
            break;
        p++;
    }
    if (OV(OP_EDCOMPATIBLE) && kind == 0) {
        gflag ^= last_s_g;
        cflag ^= last_s_c;
    }
    if (kind == 0) {
        last_s_g = gflag;
        last_s_c = cflag;
    }
    if (rflag) {
        if (!last_re) {
            re_free(re);
            ex_err("No previous regular expression");
        }
        re_free(re);
        re = last_re;
        re_ref(re);
    }
    if (parse_count(&p, &count))
        apply_count(c, count);
    parse_flags(&p, &c->flags, &c->flagoff);
    end_args(p);
    if (re != last_re && kind != 2)
        set_last_re(re);
    if (!search_dir)
        search_dir = 1;
    {
        int a2 = c->a2;
        for (int l = c->a1; l <= a2 && l >= 1; l++) {
            const char *s;
            int len, pos = 0, last_end = 0, did = 0, prev_nonempty = 0;
            Match m;
            Str out;
            if (intr)
                break;
            s = LT(l);
            len = LL(l);
            str_init(&out);
            while (pos <= len && re_exec(re, s, len, pos, 0, &m)) {
                int yes = 1;
                if (m.so[0] == m.eo[0] && m.so[0] == last_end && prev_nonempty) {
                    /* no empty match right after the previous match */
                    int n;
                    prev_nonempty = 0;
                    if (m.so[0] >= len)
                        break;
                    n = ch_len(s, len, m.so[0]);
                    str_addn(&out, s + last_end, (size_t)(m.so[0] + n - last_end));
                    last_end = pos = m.so[0] + n;
                    continue;
                }
                prev_nonempty = m.eo[0] > m.so[0];
                if (cflag) {
                    Str done;
                    str_init(&done);
                    str_addn(&done, out.s ? out.s : "", out.len);
                    str_addn(&done, s + last_end, (size_t)(m.so[0] - last_end));
                    yes = confirm_subst(l, done.s ? done.s : "", (int)done.len, s + m.so[0],
                                        len - m.so[0], 0, m.eo[0] - m.so[0]);
                    str_free(&done);
                    if (intr)
                        break;
                }
                if (yes) {
                    str_addn(&out, s + last_end, (size_t)(m.so[0] - last_end));
                    repl_apply(rp, s, len, &m, &out);
                    did++;
                } else
                    str_addn(&out, s + last_end, (size_t)(m.eo[0] - last_end));
                last_end = m.eo[0];
                if (!gflag)
                    break;
                if (m.eo[0] == m.so[0]) {
                    int n;
                    if (m.eo[0] >= len)
                        break;
                    n = ch_len(s, len, m.eo[0]);
                    str_addn(&out, s + m.eo[0], (size_t)n);
                    last_end = pos = m.eo[0] + n;
                } else
                    pos = m.eo[0];
            }
            if (did) {
                char *nt;
                int start = 0, extra = 0;
                str_addn(&out, s + last_end, (size_t)(len - last_end));
                nt = out.s ? out.s : "";
                /* split at REPL_SPLIT characters */
                for (int k = 0;; k++) {
                    if (nt[k] == REPL_SPLIT || nt[k] == 0) {
                        if (start == 0)
                            buf_change(l, nt, k);
                        else {
                            buf_insert1(l + extra, nt + start, k - start);
                            extra++;
                        }
                        if (nt[k] == 0)
                            break;
                        start = k + 1;
                    }
                }
                nsubs += did;
                nlines_changed++;
                l += extra;
                a2 += extra;
                lastline = l;
            }
            str_free(&out);
        }
    }
    if (own_rp)
        repl_free(rp);
    re_free(re);
    if (intr) {
        intr = 0;
        if (lastline) {
            cur = lastline;
            set_nonblank();
        }
        ex_err("Interrupted");
    }
    if (!nsubs) {
        if (in_global) {
            c->flags = 0;
            c->flagoff = 0;
            return;
        }
        ex_err("No match found");
    }
    cur = lastline;
    set_nonblank();
    report_changes(0, "changed", nlines_changed);
}

void c_subst(Cmd *c) { subst(c, 0); }
void c_subst_amp(Cmd *c) { subst(c, 1); }
void c_subst_tilde(Cmd *c) { subst(c, 2); }

/* ---------- execute buffer ---------- */

static int last_exec_reg;

void c_exec(Cmd *c)
{
    const char *p = skipbl(c->arg);
    int reg = 0, a1 = c->a1, a2 = c->a2;
    Reg copy;
    Str text;
    unsigned long *ids;
    int n;
    long gen = buffer_generation, a0 = rep_added, d0 = rep_deleted;
    if (*p && *p != '@' && *p != '*') {
        reg = (unsigned char)*p++;
        if (!isalnum(reg) && reg != '"')
            ex_err("Invalid buffer name");
    } else if (*p) {
        p++;
        reg = 0;
    }
    end_args(p);
    if (!reg) {
        if (!last_exec_reg)
            ex_err("No previously executed buffer");
        reg = last_exec_reg;
    }
    if (!reg_get(reg))
        ex_err("Invalid buffer name");
    last_exec_reg = reg;
    memset(&copy, 0, sizeof copy);
    reg_copy(&copy, reg_get(reg));
    str_init(&text);
    for (int i = 0; i < copy.n; i++) {
        str_addn(&text, copy.t[i], (size_t)copy.l[i]);
        if (copy.linemode || i < copy.n - 1)
            str_addc(&text, '\n');
    }
    reg_free(&copy);
    n = a2 - a1 + 1;
    if (a1 < 1)
        n = 1;
    ids = xmalloc((size_t)n * sizeof *ids);
    for (int i = 0; i < n; i++)
        ids[i] = a1 >= 1 ? lp(a1 + i)->id : 0;
    in_exec_buffer++;
    for (int i = 0; i < n; i++) {
        int r;
        if (ids[i]) {
            int l = line_of_id(ids[i]);
            if (!l) {
                in_exec_buffer--;
                free(ids);
                str_free(&text);
                ex_err("Line no longer exists");
            }
            cur = l;
            set_nonblank();
        }
        r = ex_run(text.s ? text.s : "", NULL);
        if (r < 0 || (n > 1 && buffer_generation != gen)) {
            in_exec_buffer--;
            free(ids);
            str_free(&text);
            if (r >= 0)
                ex_err("The edit buffer was replaced");
            ex_err_silent();
        }
    }
    in_exec_buffer--;
    free(ids);
    str_free(&text);
    report_net(a0, d0);
    c->no_autoprint = 1;
}
