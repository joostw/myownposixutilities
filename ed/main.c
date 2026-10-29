/*
 * main.c - addresses, the commands, global commands, signals and the
 * command loop of ed.
 */
#include "ed.h"

int opt_s;
char *prompt;
int prompting;
sigjmp_buf jmp;
volatile sig_atomic_t jmp_ok, interrupted;
static volatile sig_atomic_t hangup;
static int g_depth;
static Buf textbuf;

/* ---------- addresses ---------- */

static void skip_blanks(const char **pp)
{
    while (**pp == ' ' || **pp == '\t')
        (*pp)++;
}

static int parse_number(const char **pp, long *v)
{
    long n = 0;
    if (!isdigit((unsigned char)**pp))
        return 0;
    while (isdigit((unsigned char)**pp)) {
        if (n > (LONG_MAX - 9) / 10)
            return fail("Number too big");
        n = n * 10 + (**pp - '0');
        (*pp)++;
    }
    *v = n;
    return 1;
}

static int search(const char **pp, int delim, long *v)
{
    int closed;
    regex_t *re;
    regmatch_t m[1];
    long i;
    (*pp)++;
    re = get_re(pp, delim, &closed);
    if (!re)
        return -1;
    if (!closed && **pp)
        return fail("Missing pattern delimiter");
    for (long k = 1; k <= nlines; k++) {
        if (delim == '/')
            i = (cur + k - 1) % nlines + 1;
        else {
            /* backwards from the line before the current one; from line 0 that is $ */
            long origin = cur >= 1 ? cur : nlines + 1;
            i = origin - k;
            while (i < 1)
                i += nlines;
        }
        if (re_match(re, lines[i], m, 1, 0, 0)) {
            *v = i;
            return 0;
        }
        if (interrupted)
            return fail("Interrupt");
    }
    return fail("No match");
}

/* one address with its offsets; returns 1 if present, 0 if absent, -1 error */
static int parse_address(const char **pp, long *v)
{
    const char *p;
    int have = 0;
    long base = cur;
    skip_blanks(pp);
    p = *pp;
    if (*p == '.') {
        base = cur;
        p++;
        have = 1;
    } else if (*p == '$') {
        base = nlines;
        p++;
        have = 1;
    } else if (isdigit((unsigned char)*p)) {
        if (parse_number(&p, &base) < 0)
            return -1;
        have = 1;
    } else if (*p == '\'') {
        int c = (unsigned char)p[1];
        if (c < 'a' || c > 'z')
            return fail("Invalid mark character");
        if (!marks[c - 'a'] || !(base = line_index(marks[c - 'a'])))
            return fail("Invalid address");
        p += 2;
        have = 1;
    } else if (*p == '/' || *p == '?') {
        *pp = p;
        if (search(pp, *p, &base) < 0)
            return -1;
        p = *pp;
        have = 1;
    }
    for (;;) {
        long n;
        const char *q = p;
        while (*q == ' ' || *q == '\t')
            q++;
        if (*q == '+' || *q == '-' || *q == '^') {
            int sign = *q == '+' ? 1 : -1;
            q++;
            if (isdigit((unsigned char)*q)) {
                if (parse_number(&q, &n) < 0)
                    return -1;
            } else
                n = 1;
            base += sign * n;
            have = 1;
            p = q;
        } else if (isdigit((unsigned char)*q) && have) {
            if (parse_number(&q, &n) < 0)
                return -1;
            base += n;
            p = q;
        } else
            break;
    }
    *pp = p;
    *v = base;
    return have;
}

static int check_addr(long v)
{
    if (v < 0 || v > nlines)
        return fail("Invalid address");
    return 0;
}

/* the address list; returns the number of addresses (0, 1 or 2) */
static int parse_addresses(const char **pp, long *a1, long *a2)
{
    long vals[2] = { 0, 0 };
    int count = 0;
    long v;
    int r = parse_address(pp, &v);
    if (r < 0 || (r && check_addr(v) < 0))
        return -1;
    for (;;) {
        char sep;
        long left;
        int left_given = r;
        skip_blanks(pp);
        sep = **pp;
        if (sep != ',' && sep != ';') {
            if (r) {
                vals[0] = vals[1];
                vals[1] = v;
                count++;
            }
            break;
        }
        (*pp)++;
        left = left_given ? v : (sep == ',' ? 1 : cur);
        /* addresses are checked by the commands that use them, except
           that ; makes the address the current line */
        if (sep == ';') {
            /* the current line stays set, even if the command fails */
            cur = left;
        }
        vals[0] = vals[1];
        vals[1] = left;
        count++;
        r = parse_address(pp, &v);
        if (r < 0 || (r && check_addr(v) < 0))
            return -1;
        if (!r) {
            v = left_given ? left : nlines;
            r = 1;
            /* an omitted address before another separator */
        }
    }
    if (count >= 2) {
        *a1 = vals[0];
        *a2 = vals[1];
        return 2;
    }
    if (count == 1) {
        *a1 = *a2 = vals[1];
        return 1;
    }
    return 0;
}

/* ---------- command helpers ---------- */

static int parse_suffix(const char **pp, int *flags)
{
    const char *p = *pp;
    *flags = 0;
    for (;;) {
        if (*p == 'p')
            *flags |= PF_P;
        else if (*p == 'l')
            *flags |= PF_L;
        else if (*p == 'n')
            *flags |= PF_N;
        else
            break;
        p++;
    }
    if (*p)
        return fail("Invalid command suffix");
    *pp = p;
    return 0;
}

static int print_current(int flags)
{
    if (!flags)
        return 0;
    if (cur < 1 || cur > nlines)
        return fail("Invalid address");
    print_lines(cur, cur, flags);
    return 0;
}

static int need_lines(long a1, long a2)
{
    if (a1 < 1 || a2 > nlines || a1 > a2)
        return fail("Invalid address");
    return 0;
}

/* a file parameter: separated by blanks; NULL if none, "" error */
static int get_file(const char **pp, char **name)
{
    const char *p = *pp;
    *name = NULL;
    if (!*p)
        return 0;
    if (*p != ' ' && *p != '\t')
        return fail("Unexpected command suffix");
    skip_blanks(&p);
    if (*p)
        *name = strdup(p);
    *pp = p + strlen(p);
    return 0;
}

static char *expand_shell(const char *cmd, int *changed_text)
{
    Buf b = { 0 };
    const char *p = cmd;
    *changed_text = 0;
    if (*p == '!') {
        if (!last_shell) {
            fail("No previous command");
            return NULL;
        }
        buf_adds(&b, last_shell);
        p++;
        *changed_text = 1;
    }
    for (; *p; p++) {
        if (*p == '\\' && p[1] == '%') {
            buf_addc(&b, '%');
            p++;
        } else if (*p == '%') {
            if (!filename) {
                free(b.s);
                fail("No current filename");
                return NULL;
            }
            buf_adds(&b, filename);
            *changed_text = 1;
        } else
            buf_addc(&b, *p);
    }
    if (!b.s)
        buf_adds(&b, "");
    free(last_shell);
    last_shell = strdup(b.s);
    return b.s;
}

static int input_lines(Reader *r, long after)
{
    Line **v;
    long n;
    if (read_text(r, &textbuf, &v, &n) < 0)
        return -1;
    if (n)
        buf_insert(after, v, n);
    cur = after + n;
    free(v);
    return 0;
}

static int do_move(long a1, long a2, long dest, int copy)
{
    long n = a2 - a1 + 1;
    Line **v = xmalloc((size_t)n * sizeof(Line *));
    if (!copy && dest >= a1 && dest < a2) {
        free(v);
        return fail("Invalid destination");
    }
    for (long i = 0; i < n; i++)
        v[i] = copy ? line_new(lines[a1 + i]->s, lines[a1 + i]->len) : lines[a1 + i];
    if (copy) {
        buf_insert(dest, v, n);
        cur = dest + n;
    } else if (dest == a2 || dest == a1 - 1) {
        changed();
        cur = a2;
    } else {
        buf_delete(a1, a2);
        if (dest > a2)
            dest -= n;
        buf_insert(dest, v, n);
        cur = dest + n;
    }
    free(v);
    return 0;
}

/* ---------- global commands ---------- */

static int list_next(Reader *r, Buf *b)
{
    buf_clear(b);
    if (r->i >= r->n)
        return 0;
    buf_adds(b, r->v[r->i++]);
    return 1;
}

static int collect_list(const char *first, Reader *src, char ***v, int *n)
{
    Buf cur_line = { 0 }, more = { 0 };
    int cap = 0;
    *v = NULL;
    *n = 0;
    buf_adds(&cur_line, first);
    for (;;) {
        size_t k = 0;
        int cont;
        while (k < cur_line.len && cur_line.s[cur_line.len - 1 - k] == '\\')
            k++;
        cont = k % 2 == 1;
        if (cont)
            cur_line.s[--cur_line.len] = 0;
        if (*n + 1 > cap) {
            cap = cap ? cap * 2 : 8;
            *v = xrealloc(*v, (size_t)cap * sizeof(char *));
        }
        (*v)[(*n)++] = strdup(cur_line.s ? cur_line.s : "");
        if (!cont)
            break;
        {
            int r = src->next(src, &more);
            if (r < 0) {
                free(cur_line.s);
                free(more.s);
                return fail("Interrupt");
            }
            if (r == 0)
                break;
            buf_clear(&cur_line);
            buf_add(&cur_line, more.s ? more.s : "", more.len);
        }
    }
    free(cur_line.s);
    free(more.s);
    return 0;
}

static void free_list(char **v, int n)
{
    for (int i = 0; i < n; i++)
        free(v[i]);
    free(v);
}

static long next_marked(long from)
{
    for (long i = from; i <= nlines; i++)
        if (lines[i]->gmark)
            return i;
    for (long i = 1; i < from && i <= nlines; i++)
        if (lines[i]->gmark)
            return i;
    return 0;
}

static int global(long a1, long a2, int invert, int interactive, const char **pp, Reader *src)
{
    int delim = (unsigned char)**pp, closed;
    regex_t *re;
    regmatch_t m[1];
    char **list = NULL;
    int nlist = 0, r = 0;
    long pos = 1, count = 0;
    Buf last_cmd = { 0 }, inb = { 0 };
    if (g_depth)
        return fail("Cannot nest global commands");
    if (!delim)
        return fail("Missing pattern delimiter");
    (*pp)++;
    re = get_re(pp, delim, &closed);
    if (!re)
        return -1;
    if (interactive) {
        if (**pp)
            return fail("Invalid command suffix");
    } else {
        if (collect_list(closed ? *pp : "", src, &list, &nlist) < 0)
            return -1;
        if (nlist == 1 && !*list[0]) {
            free(list[0]);
            list[0] = strdup("p");
        }
    }
    *pp += strlen(*pp);
    for (long i = 1; i <= nlines; i++)
        lines[i]->gmark = 0;
    for (long i = a1; i <= a2; i++) {
        int match = re_match(re, lines[i], m, 1, 0, 0);
        if (match != invert) {
            lines[i]->gmark = 1;
            count++;
        }
    }
    g_depth++;
    while (count > 0) {
        long i = next_marked(pos);
        if (!i)
            break;
        if (interrupted) {
            r = fail("Interrupt");
            break;
        }
        lines[i]->gmark = 0;
        cur = i;
        pos = i;
        if (interactive) {
            int k;
            print_lines(i, i, PF_P);
            k = stdin_reader.next(&stdin_reader, &inb);
            if (k < 0) {
                r = fail("Interrupt");
                break;
            }
            if (k == 0)
                break;
            if (inb.len == 0)
                continue;
            if (inb.len == 1 && inb.s[0] == '&') {
                if (!last_cmd.len) {
                    r = fail("No previous command");
                    break;
                }
            } else {
                buf_clear(&last_cmd);
                buf_add(&last_cmd, inb.s, inb.len);
            }
            {
                const char *c = last_cmd.s;
                const char *q = c;
                long d1, d2;
                Reader none = { list_next, NULL, 0, 0 };
                /* a, c, i and the global commands are not allowed here */
                if (parse_addresses(&q, &d1, &d2) < 0) {
                    r = -1;
                    break;
                }
                skip_blanks(&q);
                if (*q && strchr("acigGvV", *q)) {
                    r = fail("Invalid command");
                    break;
                }
                if (exec_command(&c, &none, 1) < 0) {
                    r = -1;
                    break;
                }
            }
        } else {
            Reader lr = { list_next, list, nlist, 0 };
            while (lr.i < lr.n) {
                const char *c = lr.v[lr.i++];
                int k = exec_command(&c, &lr, 1);
                if (k < 0) {
                    r = -1;
                    break;
                }
                if (k == 1) {
                    r = 1;
                    break;
                }
            }
            if (r)
                break;
        }
        count--;
        out_flush();
    }
    g_depth--;
    for (long i = 1; i <= nlines; i++)
        lines[i]->gmark = 0;
    free_list(list, nlist);
    free(last_cmd.s);
    free(inb.s);
    return r;
}

/* ---------- commands ---------- */

int exec_command(const char **pp, Reader *r, int in_global)
{
    const char *p = *pp;
    long a1 = 0, a2 = 0;
    int naddr, flags = 0, c;
    naddr = parse_addresses(&p, &a1, &a2);
    if (naddr < 0)
        return -1;
    skip_blanks(&p);
    c = (unsigned char)*p;
    if (c)
        p++;
#define NO_ADDR()                                                                    \
    do {                                                                             \
        if (naddr)                                                                   \
            return fail("Unexpected address");                                       \
    } while (0)
#define DEFAULT1(x)                                                                  \
    do {                                                                             \
        if (!naddr)                                                                  \
            a1 = a2 = (x);                                                           \
    } while (0)
#define DEFAULT2(x, y)                                                               \
    do {                                                                             \
        if (naddr == 0) {                                                            \
            a1 = (x);                                                                \
            a2 = (y);                                                                \
        } else if (naddr == 1)                                                       \
            a1 = a2;                                                                 \
    } while (0)
    switch (c) {
    case 'a':
    case 'i': {
        long at;
        DEFAULT1(cur);
        if (parse_suffix(&p, &flags) < 0)
            return -1;
        at = c == 'a' ? a2 : (a2 > 0 ? a2 - 1 : 0);
        if (check_addr(a2) < 0)
            return -1;
        undo_begin();
        if (input_lines(r, at) < 0) {
            undo_end();
            return -1;
        }
        if (cur == at && c == 'i' && a2 > 0)
            cur = a2;
        undo_end();
        return print_current(flags);
    }
    case 'c': {
        long n0;
        DEFAULT2(cur, cur);
        if (need_lines(a1, a2) < 0 || parse_suffix(&p, &flags) < 0)
            return -1;
        undo_begin();
        n0 = nlines;
        buf_delete(a1, a2);
        if (input_lines(r, a1 - 1) < 0) {
            undo_end();
            return -1;
        }
        if (cur == a1 - 1) {
            /* no text: the line after the deleted ones */
            cur = a2 < n0 ? a1 : nlines;
            if (cur > nlines)
                cur = nlines;
        }
        undo_end();
        return print_current(flags);
    }
    case 'd':
        DEFAULT2(cur, cur);
        if (need_lines(a1, a2) < 0 || parse_suffix(&p, &flags) < 0)
            return -1;
        undo_begin();
        buf_delete(a1, a2);
        cur = a1 <= nlines ? a1 : nlines;
        undo_end();
        return print_current(flags);
    case 'e':
    case 'E': {
        char *name;
        NO_ADDR();
        if (get_file(&p, &name) < 0)
            return -1;
        {
            int is_cmd = name && name[0] == '!';
            long n = 0;
            if (modified == 1 && c == 'e') {
                modified = 2;
                free(name);
                return fail("Warning: buffer modified");
            }
            if (!name && !filename) {
                free(name);
                return fail("No current filename");
            }
            if (is_cmd) {
                int ch;
                char *cmd = expand_shell(name + 1, &ch);
                if (!cmd) {
                    free(name);
                    return -1;
                }
                if (ch)
                    printf("%s\n", cmd);
                buf_clear_all();
                n = read_file(cmd, 0, 1);
                free(cmd);
            } else {
                struct stat st;
                if (name) {
                    free(filename);
                    filename = strdup(name);
                }
                buf_clear_all();
                if (stat(filename, &st) < 0 && errno == ENOENT) {
                    fprintf(stderr, "%s: %s\n", filename, strerror(errno));
                    n = -2;
                } else
                    n = read_file(filename, 0, 0);
            }
            free(name);
            modified = 0;
            if (n == -1)
                return -1;
            if (n >= 0 && !opt_s)
                printf("%ld\n", n);
            /* after e, there is nothing to undo */
            undo_forget();
            return 0;
        }
    }
    case 'f': {
        char *name;
        NO_ADDR();
        if (get_file(&p, &name) < 0)
            return -1;
        if (name) {
            free(filename);
            filename = name;
        }
        if (!filename)
            return fail("No current filename");
        printf("%s\n", filename);
        return 0;
    }
    case 'g':
    case 'v':
    case 'G':
    case 'V': {
        int k;
        DEFAULT2(1, nlines);
        if (need_lines(a1, a2) < 0)
            return -1;
        undo_begin();
        k = global(a1, a2, c == 'v' || c == 'V', c == 'G' || c == 'V', &p, r);
        undo_end_global();
        *pp = p;
        return k;
    }
    case 'h':
        NO_ADDR();
        if (parse_suffix(&p, &flags) < 0 || flags)
            return fail("Invalid command suffix");
        if (errmsg)
            printf("%s\n", errmsg);
        return 0;
    case 'H':
        NO_ADDR();
        if (*p)
            return fail("Invalid command suffix");
        help_mode = !help_mode;
        if (help_mode && errmsg)
            printf("%s\n", errmsg);
        return 0;
    case 'j': {
        DEFAULT2(cur, cur + 1);
        if (parse_suffix(&p, &flags) < 0)
            return -1;
        if (need_lines(a1, a2) < 0)
            return -1;
        if (naddr == 1 || a1 == a2)
            return print_current(flags);   /* nothing to join */
        {
            Buf b = { 0 };
            Line *l;
            for (long i = a1; i <= a2; i++)
                buf_add(&b, lines[i]->s, lines[i]->len);
            l = line_new(b.s ? b.s : "", b.len);
            free(b.s);
            undo_begin();
            buf_delete(a1 + 1, a2);
            buf_replace(a1, l);
            undo_end();
        }
        cur = a1;
        return print_current(flags);
    }
    case 'k': {
        int m = (unsigned char)*p;
        DEFAULT1(cur);
        if (m < 'a' || m > 'z')
            return fail("Invalid mark character");
        p++;
        if (need_lines(a2, a2) < 0 || parse_suffix(&p, &flags) < 0)
            return -1;
        marks[m - 'a'] = lines[a2];
        return print_current(flags);
    }
    case 'l':
    case 'n':
    case 'p':
        DEFAULT2(cur, cur);
        if (need_lines(a1, a2) < 0 || parse_suffix(&p, &flags) < 0)
            return -1;
        flags |= c == 'l' ? PF_L : c == 'n' ? PF_N : PF_P;
        print_lines(a1, a2, flags);
        if (interrupted)
            return fail("Interrupt");
        cur = a2;
        return 0;
    case 'm':
    case 't': {
        long dest, save = cur;
        int k;
        DEFAULT2(cur, cur);
        if (need_lines(a1, a2) < 0)
            return -1;
        k = parse_address(&p, &dest);
        if (k < 0)
            return -1;
        if (!k)
            dest = cur;   /* extension, as historical eds: the current line */
        if (check_addr(dest) < 0) {
            cur = save;
            return fail("Invalid destination");
        }
        if (parse_suffix(&p, &flags) < 0)
            return -1;
        undo_begin();
        if (do_move(a1, a2, dest, c == 't') < 0) {
            undo_end();
            return -1;
        }
        undo_end();
        return print_current(flags);
    }
    case 'P':
        NO_ADDR();
        if (*p)
            return fail("Invalid command suffix");
        prompting = !prompting;
        return 0;
    case 'q':
    case 'Q':
        NO_ADDR();
        if (*p)
            return fail("Invalid command suffix");
        if (c == 'q' && modified == 1) {
            modified = 2;
            return fail("Warning: buffer modified");
        }
        return 1;
    case 'r': {
        char *name;
        long n;
        int is_cmd;
        DEFAULT1(nlines);
        if (check_addr(a2) < 0 || get_file(&p, &name) < 0)
            return -1;
        if (!name && !filename)
            return fail("No current filename");
        is_cmd = name && name[0] == '!';
        undo_begin();
        if (is_cmd) {
            int ch;
            char *cmd = expand_shell(name + 1, &ch);
            if (!cmd) {
                free(name);
                undo_end();
                return -1;
            }
            if (ch)
                printf("%s\n", cmd);
            n = read_file(cmd, a2, 1);
            free(cmd);
        } else {
            if (name && !filename)
                filename = strdup(name);
            n = read_file(name ? name : filename, a2, 0);
        }
        undo_end();
        free(name);
        if (n < 0)
            return -1;
        if (!opt_s)
            printf("%ld\n", n);
        return 0;
    }
    case 's': {
        int delim = (unsigned char)*p, closed, split, gflag = 0;
        long count = 1;
        regex_t *re;
        Buf full = { 0 };
        const char *q;
        int k;
        DEFAULT2(cur, cur);
        if (need_lines(a1, a2) < 0)
            return -1;
        if (!delim || delim == ' ' || delim == '\n')
            return fail("Missing pattern delimiter");
        p++;
        re = get_re(&p, delim, &closed);
        if (!re)
            return -1;
        if (!closed)
            return fail("Missing pattern delimiter");
        /* the replacement may continue on the next lines (escaped newlines) */
        buf_adds(&full, p);
        if (!in_global) {
            for (;;) {
                size_t bs = 0;
                Buf more = { 0 };
                while (bs < full.len && full.s[full.len - 1 - bs] == '\\')
                    bs++;
                if (bs % 2 == 0)
                    break;
                if (r->next(r, &more) <= 0) {
                    free(more.s);
                    break;
                }
                buf_addc(&full, '\n');
                buf_add(&full, more.s ? more.s : "", more.len);
                free(more.s);
            }
        }
        q = full.s ? full.s : "";
        if (parse_replacement(&q, delim, &closed, &split) < 0) {
            free(full.s);
            return -1;
        }
        if (!closed)
            flags |= PF_P;
        else {
            for (;;) {
                if (*q == 'g') {
                    gflag = 1;
                    q++;
                } else if (isdigit((unsigned char)*q)) {
                    const char *t = q;
                    if (parse_number(&t, &count) < 0 || count == 0) {
                        free(full.s);
                        return fail("Invalid count");
                    }
                    q = t;
                } else if (*q == 'p') {
                    flags |= PF_P;
                    q++;
                } else if (*q == 'l') {
                    flags |= PF_L;
                    q++;
                } else if (*q == 'n') {
                    flags |= PF_N;
                    q++;
                } else
                    break;
            }
            if (*q) {
                free(full.s);
                return fail("Invalid command suffix");
            }
        }
        free(full.s);
        undo_begin();
        k = substitute(a1, a2, re, gflag, count, in_global);
        undo_end();
        if (k < 0)
            return -1;
        if (k == 0)
            return 0;
        return print_current(flags);
    }
    case 'u':
        NO_ADDR();
        if (parse_suffix(&p, &flags) < 0)
            return -1;
        if (undo() < 0)
            return -1;
        return print_current(flags);
    case 'w':
    case 'W': {
        char *name;
        long n;
        int quit = 0, is_cmd, whole;
        if (c == 'w' && *p == 'q') {
            quit = 1;
            p++;
        }
        if (naddr == 0) {
            a1 = nlines ? 1 : 0;
            a2 = nlines;
        } else if (naddr == 1)
            a1 = a2;
        if ((naddr || nlines) && need_lines(a1, a2) < 0)
            return -1;
        if (get_file(&p, &name) < 0)
            return -1;
        if (!name && !filename)
            return fail("No current filename");
        is_cmd = name && name[0] == '!';
        whole = a1 <= 1 && a2 == nlines;
        if (is_cmd) {
            int ch;
            char *cmd = expand_shell(name + 1, &ch);
            if (!cmd) {
                free(name);
                return -1;
            }
            if (ch)
                printf("%s\n", cmd);
            n = write_file(cmd, "w", a1, a2, 1);
            free(cmd);
        } else {
            if (name && !filename)
                filename = strdup(name);
            n = write_file(name ? name : filename, c == 'W' ? "a" : "w", a1, a2, 0);
            if (n >= 0 && whole)
                modified = 0;
        }
        free(name);
        if (n < 0)
            return -1;
        if (!opt_s)
            printf("%ld\n", n);
        if (quit) {
            if (modified == 1) {
                modified = 2;
                return fail("Warning: buffer modified");
            }
            return 1;
        }
        return 0;
    }
    case '=':
        DEFAULT1(nlines);
        if (check_addr(a2) < 0 || parse_suffix(&p, &flags) < 0)
            return -1;
        printf("%ld\n", a2);
        return 0;
    case '!': {
        int ch, st;
        char *cmd;
        NO_ADDR();
        cmd = expand_shell(p, &ch);
        if (!cmd)
            return -1;
        if (ch)
            printf("%s\n", cmd);
        st = run_shell(cmd);
        free(cmd);
        if (st == -1 && errmsg)
            return -1;
        if (!opt_s)
            printf("!\n");
        return 0;
    }
    case 0:
        /* the null command */
        if (naddr == 0) {
            a2 = cur + 1;
            if (a2 > nlines)
                return fail("Invalid address");
        }
        if (a2 < 1 || a2 > nlines)
            return fail("Invalid address");
        print_lines(a2, a2, PF_P);
        cur = a2;
        return 0;
    default:
        return fail("Unknown command");
    }
}

/* ---------- signals ---------- */

static void on_int(int sig)
{
    (void)sig;
    interrupted = 1;
}

static void on_hup(int sig)
{
    (void)sig;
    hangup = 1;
    interrupted = 1;
}

static void save_hup(void)
{
    if (nlines > 0 && modified) {
        if (write_file("ed.hup", "w", 1, nlines, 0) < 0) {
            const char *h = getenv("HOME");
            if (h) {
                Buf b = { 0 };
                buf_adds(&b, h);
                buf_adds(&b, "/ed.hup");
                write_file(b.s, "w", 1, nlines, 0);
                free(b.s);
            }
        }
    }
    _exit(2);
}

static void install_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_int;
    sigaction(SIGINT, &sa, NULL);
    sa.sa_handler = on_hup;
    sigaction(SIGHUP, &sa, NULL);
    signal(SIGQUIT, SIG_IGN);
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    int c;
    Buf line = { 0 };
    setlocale(LC_ALL, "");
    while ((c = getopt(argc, argv, ":p:s")) != -1) {
        switch (c) {
        case 'p':
            prompt = optarg;
            prompting = 1;
            break;
        case 's':
            opt_s = 1;
            break;
        case ':':
            fprintf(stderr, "ed: option requires an argument -- %c\n", optopt);
            fprintf(stderr, "usage: ed [-p string] [-s] [file]\n");
            return 2;
        default:
            fprintf(stderr, "ed: illegal option -- %c\n", optopt);
            fprintf(stderr, "usage: ed [-p string] [-s] [file]\n");
            return 2;
        }
    }
    if (optind < argc && strcmp(argv[optind], "-") == 0) {
        /* unspecified: the historical form of -s */
        opt_s = 1;
        optind++;
    }
    if (argc - optind > 1) {
        fprintf(stderr, "usage: ed [-p string] [-s] [file]\n");
        return 2;
    }
    install_signals();
    buf_clear_all();
    if (optind < argc) {
        const char *f = argv[optind];
        if (f[0] == '!') {
            long n = read_file(f + 1, 0, 1);
            if (n < 0)
                report_error();
            else if (!opt_s)
                printf("%ld\n", n);
        } else {
            struct stat st;
            filename = strdup(f);
            if (stat(f, &st) < 0 && errno == ENOENT)
                fprintf(stderr, "%s: %s\n", f, strerror(errno));
            else {
                long n = read_file(f, 0, 0);
                if (n < 0)
                    report_error();
                else if (!opt_s)
                    printf("%ld\n", n);
            }
        }
        modified = 0;
        undo_forget();
    }
    for (;;) {
        int k;
        const char *p;
        if (hangup)
            save_hup();
        if (prompting)
            fputs(prompt ? prompt : "*", stdout);
        out_flush();
        interrupted = 0;
        k = stdin_reader.next(&stdin_reader, &line);
        if (hangup)
            save_hup();
        if (k < 0) {
            putchar('\n');
            errmsg = "Interrupt";
            report_error();
            continue;
        }
        if (k == 0) {
            /* end-of-file: as q */
            if (modified == 1) {
                modified = 2;
                errmsg = "Warning: buffer modified";
                report_error();
                continue;
            }
            break;
        }
        p = line.s;
        k = exec_command(&p, &stdin_reader, 0);
        if (hangup)
            save_hup();
        if (k < 0) {
            if (interrupted && !errmsg)
                errmsg = "Interrupt";
            report_error();
            flush_input();
        } else if (k == 1)
            break;
        out_flush();
    }
    out_flush();
    return had_error ? 1 : 0;
}
