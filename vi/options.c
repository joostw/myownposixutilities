/*
 * options.c - edit options and the set command.
 */
#include "ex.h"

Opt opts[] = {
    { "autoindent", "ai", OT_BOOL, 0, NULL, 0, 0, 0 },
    { "autoprint", "ap", OT_BOOL, 1, NULL, 0, 0, 0 },
    { "autowrite", "aw", OT_BOOL, 0, NULL, 0, 0, 0 },
    { "beautify", "bf", OT_BOOL, 0, NULL, 0, 0, 0 },
    { "directory", "dir", OT_STR, 0, NULL, 0, 0, 0 },
    { "edcompatible", "ed", OT_BOOL, 0, NULL, 0, 0, 0 },
    { "errorbells", "eb", OT_BOOL, 0, NULL, 0, 0, 0 },
    { "exrc", "ex", OT_BOOL, 0, NULL, 0, 0, 0 },
    { "ignorecase", "ic", OT_BOOL, 0, NULL, 0, 0, 0 },
    { "list", NULL, OT_BOOL, 0, NULL, 0, 0, 0 },
    { "magic", NULL, OT_BOOL, 1, NULL, 0, 0, 0 },
    { "mesg", NULL, OT_BOOL, 1, NULL, 0, 0, 0 },
    { "number", "nu", OT_BOOL, 0, NULL, 0, 0, 0 },
    { "paragraphs", "para", OT_STR, 0, NULL, 0, 0, 0 },
    { "prompt", NULL, OT_BOOL, 1, NULL, 0, 0, 0 },
    { "readonly", "ro", OT_BOOL, 0, NULL, 0, 0, 0 },
    { "redraw", NULL, OT_BOOL, 0, NULL, 0, 0, 0 },
    { "remap", NULL, OT_BOOL, 1, NULL, 0, 0, 0 },
    { "report", NULL, OT_NUM, 5, NULL, 0, 0, 0 },
    { "scroll", "scr", OT_NUM, 11, NULL, 0, 0, 0 },
    { "sections", "sect", OT_STR, 0, NULL, 0, 0, 0 },
    { "shell", "sh", OT_STR, 0, NULL, 0, 0, 0 },
    { "shiftwidth", "sw", OT_NUM, 8, NULL, 0, 0, 0 },
    { "showmatch", "sm", OT_BOOL, 0, NULL, 0, 0, 0 },
    { "showmode", "smd", OT_BOOL, 0, NULL, 0, 0, 0 },
    { "slowopen", "slow", OT_BOOL, 0, NULL, 0, 0, 0 },
    { "tabstop", "ts", OT_NUM, 8, NULL, 0, 0, 0 },
    { "taglength", "tl", OT_NUM, 0, NULL, 0, 0, 0 },
    { "tags", "tag", OT_STR, 0, NULL, 0, 0, 0 },
    { "term", NULL, OT_STR, 0, NULL, 0, 0, 0 },
    { "terse", NULL, OT_BOOL, 0, NULL, 0, 0, 0 },
    { "warn", NULL, OT_BOOL, 1, NULL, 0, 0, 0 },
    { "window", "wi", OT_NUM, 23, NULL, 0, 0, 0 },
    { "wrapmargin", "wm", OT_NUM, 0, NULL, 0, 0, 0 },
    { "wrapscan", "ws", OT_BOOL, 1, NULL, 0, 0, 0 },
    { "writeany", "wa", OT_BOOL, 0, NULL, 0, 0, 0 },
};

static long dnum[OP_NOPTS];
static char *dstr[OP_NOPTS];
int window_from_w;      /* -w was given */

static void set_str(int o, const char *v)
{
    free(opts[o].str);
    opts[o].str = xstrdup(v);
}

void opts_init(void)
{
    const char *e;
    char *dir = "/var/tmp";
    if (access(dir, W_OK) != 0)
        dir = "/tmp";
    if ((e = getenv("TMPDIR")) && *e && access(e, W_OK) == 0 && access("/var/tmp", W_OK) != 0)
        dir = (char *)e;
    set_str(OP_DIRECTORY, dir);
    set_str(OP_PARAGRAPHS, "IPLPPPQPP LIpplpipbp");
    set_str(OP_SECTIONS, "NHSHH HUnhsh");
    e = getenv("SHELL");
    set_str(OP_SHELL, e && *e ? e : "sh");
    set_str(OP_TAGS, "tags");
    e = getenv("TERM");
    set_str(OP_TERM, e && *e ? e : "dumb");
    opts_screen_changed();
    for (int i = 0; i < OP_NOPTS; i++) {
        dnum[i] = opts[i].num;
        dstr[i] = opts[i].str ? xstrdup(opts[i].str) : NULL;
        opts[i].changed = 0;
    }
}

/* the number of lines in the display changed: recompute dependent defaults */
void opts_screen_changed(void)
{
    const char *e = getenv("LINES");
    long lines = rows;
    if (!opts[OP_SCROLL].changed)
        opts[OP_SCROLL].num = (lines - 1) / 2 > 0 ? (lines - 1) / 2 : 1;
    if (!opts[OP_WINDOW].changed && !window_from_w) {
        long w = lines - 1;
        if (e && *e && atol(e) > 0 && !vi_active)
            w = atol(e);
        opt_window_set(w);
        opts[OP_WINDOW].changed = 0;
    } else
        opt_window_set(opts[OP_WINDOW].num);
}

void opt_window_set(long v)
{
    if (v <= 0 || v > rows - 1)
        v = rows - 1 > 0 ? rows - 1 : 1;
    opts[OP_WINDOW].num = v;
}

void opt_set_num(int o, long v)
{
    opts[o].num = v;
    opts[o].changed = 1;
}

static int find_opt(const char *name, size_t n)
{
    for (int i = 0; i < OP_NOPTS; i++) {
        if (strlen(opts[i].name) == n && strncmp(opts[i].name, name, n) == 0)
            return i;
        if (opts[i].abbr && strlen(opts[i].abbr) == n && strncmp(opts[i].abbr, name, n) == 0)
            return i;
    }
    return -1;
}

static void opt_format(int i, Str *b)
{
    Opt *o = &opts[i];
    if (o->type == OT_BOOL)
        str_fmt(b, "%s%s", o->num ? "" : "no", o->name);
    else if (o->type == OT_NUM)
        str_fmt(b, "%s=%ld", o->name, o->num);
    else
        str_fmt(b, "%s=%s", o->name, o->str ? o->str : "");
}

static int opt_differs(int i)
{
    if (opts[i].type == OT_STR)
        return strcmp(opts[i].str ? opts[i].str : "", dstr[i] ? dstr[i] : "") != 0;
    return opts[i].num != dnum[i];
}

static void show_opts(int all)
{
    char *items[OP_NOPTS];
    int n = 0, w = 0, ncol, nrow;
    for (int i = 0; i < OP_NOPTS; i++) {
        Str b;
        if (!all && i != OP_TERM && !opt_differs(i))
            continue;
        str_init(&b);
        opt_format(i, &b);
        if ((int)b.len > w)
            w = (int)b.len;
        items[n++] = str_detach(&b);
    }
    ncol = (cols - 1) / (w + 2);
    if (ncol < 1)
        ncol = 1;
    nrow = (n + ncol - 1) / ncol;
    for (int r = 0; r < nrow; r++) {
        Str line;
        str_init(&line);
        for (int c = 0; c < ncol; c++) {
            int k = c * nrow + r;
            if (k >= n)
                break;
            str_adds(&line, items[k]);
            if ((c + 1) * nrow + r < n)
                for (int pad = (int)strlen(items[k]); pad < w + 2; pad++)
                    str_addc(&line, ' ');
        }
        str_addc(&line, '\n');
        out_write(line.s, line.len);
        str_free(&line);
    }
    for (int i = 0; i < n; i++)
        free(items[i]);
}

static void show_one(int i)
{
    Str b;
    str_init(&b);
    opt_format(i, &b);
    str_addc(&b, '\n');
    out_write(b.s, b.len);
    str_free(&b);
}

void cmd_set(const char *arg)
{
    const char *p = arg;
    while (*p == ' ' || *p == '\t')
        p++;
    if (!*p) {
        show_opts(0);
        return;
    }
    while (*p) {
        Str tok;
        int has_eq = 0, query = 0, no = 0, i;
        const char *name;
        size_t nlen;
        char *val = NULL;
        str_init(&tok);
        while (*p && *p != ' ' && *p != '\t') {
            if (*p == '\\' && p[1]) {
                p++;
                str_addc(&tok, *p++);
                continue;
            }
            str_addc(&tok, *p++);
        }
        while (*p == ' ' || *p == '\t')
            p++;
        if (!tok.s) {
            continue;
        }
        if (*p == '?' && (p[1] == 0 || p[1] == ' ' || p[1] == '\t') && !strchr(tok.s, '=')) {
            query = 1;
            p++;
            while (*p == ' ' || *p == '\t')
                p++;
        }
        if (strcmp(tok.s, "all") == 0) {
            show_opts(1);
            str_free(&tok);
            continue;
        }
        name = tok.s;
        {
            char *eq = strchr(tok.s, '=');
            size_t tl = strlen(tok.s);
            if (eq) {
                has_eq = 1;
                nlen = (size_t)(eq - tok.s);
                val = eq + 1;
            } else if (tl > 0 && tok.s[tl - 1] == '?') {
                query = 1;
                nlen = tl - 1;
            } else
                nlen = tl;
        }
        i = find_opt(name, nlen);
        if (i < 0 && !has_eq && nlen > 2 && strncmp(name, "no", 2) == 0) {
            i = find_opt(name + 2, nlen - 2);
            if (i >= 0 && opts[i].type == OT_BOOL)
                no = 1;
            else
                i = -1;
        }
        if (i < 0) {
            char *bad = xstrndup(name, nlen);
            str_free(&tok);
            ex_err("%s: unknown option", bad);
        }
        if (query || (!has_eq && !no && opts[i].type != OT_BOOL)) {
            show_one(i);
        } else if (opts[i].type == OT_BOOL) {
            if (has_eq) {
                str_free(&tok);
                ex_err("%s: option is a Boolean", opts[i].name);
            }
            opts[i].num = !no;
            opts[i].changed = 1;
            if (i == OP_READONLY)
                opts[i].user_set = !no;
        } else if (opts[i].type == OT_NUM) {
            char *end;
            long v = strtol(val, &end, 10);
            if (!*val || *end || v < 0) {
                str_free(&tok);
                ex_err("%s: illegal number", opts[i].name);
            }
            if ((i == OP_TABSTOP || i == OP_SHIFTWIDTH) && v == 0) {
                str_free(&tok);
                ex_err("%s: value must be greater than zero", opts[i].name);
            }
            if (i == OP_WINDOW) {
                opt_window_set(v);
                opts[i].changed = 1;
            } else
                opt_set_num(i, v);
            if (i == OP_TABSTOP || i == OP_WINDOW || i == OP_WRAPMARGIN)
                vs_redraw_all();
        } else {
            if ((i == OP_PARAGRAPHS || i == OP_SECTIONS) && strlen(val) % 2) {
                str_free(&tok);
                ex_err("%s: value must have an even number of characters", opts[i].name);
            }
            set_str(i, val);
            opts[i].changed = 1;
        }
        if (i == OP_LIST || i == OP_NUMBER)
            vs_redraw_all();
        str_free(&tok);
    }
}
