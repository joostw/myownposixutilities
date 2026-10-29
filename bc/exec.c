/*
 * exec.c - names, the syntax tree, and executing it: expressions with
 * the scales of the standard, statements, function calls with
 * parameters and auto variables pushed and popped (dynamic scoping),
 * and output with long numbers split across lines.
 */
#include "bc.h"
#include <stdint.h>

long reg_scale = 0, reg_ibase = 10, reg_obase = 10;
static sigjmp_buf top_jmp;

void exec_abort(void)
{
    siglongjmp(top_jmp, 1);
}
volatile sig_atomic_t interrupted;

/* ---------- names ---------- */

#define NHASH 211
static Sym *symtab[NHASH];

Sym *lookup(const char *name)
{
    unsigned h = 0;
    Sym *s;
    for (const char *p = name; *p; p++)
        h = h * 31 + (unsigned char)*p;
    h %= NHASH;
    for (s = symtab[h]; s; s = s->next)
        if (!strcmp(s->name, name))
            return s;
    s = calloc(1, sizeof *s);
    if (!s)
        bc_nomem();
    s->name = malloc(strlen(name) + 1);
    if (!s->name)
        bc_nomem();
    strcpy(s->name, name);
    s->next = symtab[h];
    symtab[h] = s;
    return s;
}

static Num *var_slot(Sym *s)
{
    if (!s->nvals) {
        s->cvals = 4;
        s->vals = malloc((size_t)s->cvals * sizeof *s->vals);
        if (!s->vals)
            bc_nomem();
        s->vals[0] = num_zero();
        s->nvals = 1;
    }
    return &s->vals[s->nvals - 1];
}

static Array *arr_cur(Sym *s)
{
    if (!s->narrs) {
        s->carrs = 4;
        s->arrs = malloc((size_t)s->carrs * sizeof *s->arrs);
        if (!s->arrs)
            bc_nomem();
        s->arrs[0] = calloc(1, sizeof(Array));
        if (!s->arrs[0])
            bc_nomem();
        s->narrs = 1;
    }
    return s->arrs[s->narrs - 1];
}

static void push_var(Sym *s, Num v)
{
    var_slot(s);
    if (s->nvals == s->cvals) {
        s->cvals *= 2;
        s->vals = realloc(s->vals, (size_t)s->cvals * sizeof *s->vals);
        if (!s->vals)
            bc_nomem();
    }
    s->vals[s->nvals++] = v;
}

static void pop_var(Sym *s)
{
    num_free(&s->vals[--s->nvals]);
}

static void free_array(Array *a)
{
    for (long i = 0; i < a->n; i++)
        num_free(&a->v[i]);
    free(a->v);
    free(a);
}

static void push_arr(Sym *s, Array *a)
{
    arr_cur(s);
    if (s->narrs == s->carrs) {
        s->carrs *= 2;
        s->arrs = realloc(s->arrs, (size_t)s->carrs * sizeof *s->arrs);
        if (!s->arrs)
            bc_nomem();
    }
    s->arrs[s->narrs++] = a;
}

static void pop_arr(Sym *s)
{
    free_array(s->arrs[--s->narrs]);
}

static Array *copy_array(Array *a)
{
    Array *c = calloc(1, sizeof *c);
    if (!c)
        bc_nomem();
    c->n = a->n;
    if (a->n) {
        c->v = malloc((size_t)a->n * sizeof *c->v);
        if (!c->v)
            bc_nomem();
        for (long i = 0; i < a->n; i++)
            c->v[i] = num_copy(a->v[i]);
    }
    return c;
}

static Num *elem_slot(Sym *s, long i)
{
    Array *a = arr_cur(s);
    if (i < 0 || i >= BC_DIM_MAX_V)
        bc_runtime_error("array index out of bounds: %s[%ld]", s->name, i);
    if (i >= a->n) {
        long n = a->n ? a->n : 16;
        while (n <= i)
            n *= 2;
        if (n > BC_DIM_MAX_V)
            n = BC_DIM_MAX_V;
        a->v = realloc(a->v, (size_t)n * sizeof *a->v);
        if (!a->v)
            bc_nomem();
        for (long k = a->n; k < n; k++)
            a->v[k] = num_zero();
        a->n = n;
    }
    return &a->v[i];
}

/* ---------- the tree ---------- */

Node *node(int kind, Node *a, Node *b)
{
    Node *n = calloc(1, sizeof *n);
    if (!n)
        bc_nomem();
    n->kind = kind;
    n->a = a;
    n->b = b;
    n->line = cur_line;
    n->cache_base = -1;
    return n;
}

Node *append(Node *list, Node *n)
{
    Node *p;
    if (!n)
        return list;
    if (!list)
        return n;
    for (p = list; p->next; p = p->next)
        ;
    p->next = n;
    return list;
}

void free_tree(Node *n)
{
    while (n) {
        Node *next = n->next;
        free_tree(n->a);
        free_tree(n->b);
        free_tree(n->c);
        free_tree(n->d);
        free(n->str);
        num_free(&n->cache);
        free(n);
        n = next;
    }
}

/* ---------- checks before running ---------- */

static int check(Node *n, int loops, int infunc)
{
    int ok = 1;
    for (; n; n = n->next) {
        switch (n->kind) {
        case S_BREAK:
            if (!loops) {
                cur_line = n->line;
                bc_error("break outside a for or while");
                ok = 0;
            }
            break;
        case S_CONTINUE:
            if (!loops) {
                cur_line = n->line;
                bc_error("continue outside a for or while");
                ok = 0;
            }
            break;
        case S_RETURN:
            if (!infunc) {
                cur_line = n->line;
                bc_error("return outside a function");
                ok = 0;
            }
            break;
        case S_FOR:
        case S_WHILE:
            ok &= check(n->d ? n->d : n->b, loops + 1, infunc);
            continue;
        case S_IF:
            ok &= check(n->b, loops, infunc);
            ok &= check(n->c, loops, infunc);
            continue;
        case S_BLOCK:
            ok &= check(n->a, loops, infunc);
            continue;
        }
    }
    return ok;
}

/* ---------- output ---------- */

static int out_col;
static int line_length = 70;

void out_flush(void)
{
    fflush(stdout);
}

static void out_plain(int c)
{
    putchar(c);
    if (c == '\n')
        out_col = 0;
    else
        out_col++;
}

/* a character of a number: lines are continued with a backslash, as in
   historical bc: line_length - 1 characters, then the backslash */
static void out_num(int c, void *arg)
{
    (void)arg;
    out_col++;
    if (line_length > 1 && out_col == line_length - 1) {
        putchar('\\');
        putchar('\n');
        out_col = 1;
    }
    putchar(c);
}

static void print_num(Num v)
{
    num_print(v, reg_obase, out_num, NULL);
}

static void print_string(const char *s, int escapes)
{
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (escapes && c == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'a': c = '\a'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'q': c = '"'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'e':
            case '\\': c = '\\'; break;
            default:
                out_plain('\\');
                c = (unsigned char)*s;
            }
        }
        out_plain(c);
    }
}

/* ---------- evaluation ---------- */

static Num eval(Node *n);
static int exec(Node *n);

enum { X_NORMAL, X_BREAK, X_CONTINUE, X_RETURN };
static Num retval;

/* the pushed names of the active calls, for unwinding after an error */
typedef struct Pushed {
    Sym *sym;
    int array;
} Pushed;
static Pushed *pushed;
static long npushed, cpushed;
static long depth;
/* the stack of the interpreter: calls stop well before its end */
static uintptr_t stack_base;
static size_t stack_avail;

void set_stack_limit(void *base, size_t size)
{
    stack_base = (uintptr_t)base;
    stack_avail = size > ((size_t)1 << 20) ? size - ((size_t)512 << 10) : size / 2;
}

static int stack_exhausted(void)
{
    char here;
    uintptr_t p = (uintptr_t)&here;
    size_t used = p < stack_base ? stack_base - p : p - stack_base;
    return stack_base && used > stack_avail;
}

static void note_push(Sym *s, int array)
{
    if (npushed == cpushed) {
        cpushed = cpushed ? cpushed * 2 : 64;
        pushed = realloc(pushed, (size_t)cpushed * sizeof *pushed);
        if (!pushed)
            bc_nomem();
    }
    pushed[npushed].sym = s;
    pushed[npushed].array = array;
    npushed++;
}

static void unwind_to(long mark)
{
    while (npushed > mark) {
        Pushed *p = &pushed[--npushed];
        if (p->array)
            pop_arr(p->sym);
        else
            pop_var(p->sym);
    }
}

static Num reg_value(int kind)
{
    return num_from_long(kind == E_SCALE ? reg_scale : kind == E_IBASE ? reg_ibase : reg_obase);
}

/* stores v (consumed) in a named expression; returns the value stored */
static Num store(Node *t, Num v, long index)
{
    switch (t->kind) {
    case E_VAR: {
        Num *slot = var_slot(t->sym);
        num_free(slot);
        *slot = v;
        return num_copy(v);
    }
    case E_ELEM: {
        Num *slot = elem_slot(t->sym, index);
        num_free(slot);
        *slot = v;
        return num_copy(v);
    }
    default: {
        long l;
        int fits = num_to_long(v, &l);
        num_free(&v);
        if (t->kind == E_SCALE) {
            if (!fits || l > BC_SCALE_MAX_V) {
                bc_warning("scale too large, set to %ld", BC_SCALE_MAX_V);
                l = BC_SCALE_MAX_V;
            } else if (l < 0) {
                bc_warning("negative scale, set to 0");
                l = 0;
            }
            reg_scale = l;
        } else if (t->kind == E_IBASE) {
            if (fits && l < 2) {
                bc_warning("ibase too small, set to 2");
                l = 2;
            } else if (!fits || l > 16) {
                bc_warning("ibase too large, set to 16");
                l = 16;
            }
            reg_ibase = l;
        } else {
            if (fits && l < 2) {
                bc_warning("obase too small, set to 2");
                l = 2;
            } else if (!fits || l > BC_BASE_MAX_V) {
                bc_warning("obase too large, set to %ld", BC_BASE_MAX_V);
                l = BC_BASE_MAX_V;
            }
            reg_obase = l;
        }
        return num_from_long(l);
    }
    }
}

static Num load(Node *t, long index)
{
    switch (t->kind) {
    case E_VAR:
        return num_copy(*var_slot(t->sym));
    case E_ELEM: {
        Array *a = arr_cur(t->sym);
        if (index < 0 || index >= BC_DIM_MAX_V)
            bc_runtime_error("array index out of bounds: %s[%ld]", t->sym->name, index);
        if (index >= a->n)
            return num_zero();
        return num_copy(a->v[index]);
    }
    default:
        return reg_value(t->kind);
    }
}

static long elem_index(Node *t)
{
    if (t->kind == E_ELEM) {
        Num iv = eval(t->a);
        long i;
        int fits = num_to_long(iv, &i);
        num_free(&iv);
        if (!fits)
            bc_runtime_error("array index out of bounds: %s[]", t->sym->name);
        if (i < 0 || i >= BC_DIM_MAX_V)
            bc_runtime_error("array index out of bounds: %s[%ld]", t->sym->name, i);
        return i;
    }
    return 0;
}

static Num arith(int kind, Num a, Num b)
{
    Num r;
    switch (kind) {
    case E_ADD: return num_add(a, b);
    case E_SUB: return num_sub(a, b);
    case E_MUL: return num_mul(a, b, reg_scale);
    case E_DIV:
        if (num_div(a, b, reg_scale, &r))
            bc_runtime_error("divide by zero");
        return r;
    case E_MOD:
        if (num_mod(a, b, reg_scale, &r))
            bc_runtime_error("divide by zero");
        return r;
    case E_POW: {
        int e = num_pow(a, b, reg_scale, &r);
        if (e == -1)
            bc_runtime_error("divide by zero");
        if (e == -2)
            bc_runtime_error("exponent too large");
        if (!num_is_integer(b))
            bc_warning("non-zero scale in exponent");
        return r;
    }
    }
    return num_zero();
}

static int assign_kind(int op)
{
    switch (op) {
    case A_ADD: return E_ADD;
    case A_SUB: return E_SUB;
    case A_MUL: return E_MUL;
    case A_DIV: return E_DIV;
    case A_MOD: return E_MOD;
    case A_POW: return E_POW;
    }
    return 0;
}

static Num call(Node *n)
{
    Func *f = n->sym->func;
    Node *arg;
    int nargs = 0;
    long mark;
    Num result;
    if (!f)
        bc_runtime_error("function %s not defined", n->sym->name);
    for (arg = n->a; arg; arg = arg->next)
        nargs++;
    if (f->builtin) {
        Num args[2];
        int k = 0;
        if (nargs != f->nparams)
            bc_runtime_error("function %s: %d argument%s expected", n->sym->name, f->nparams,
                             f->nparams == 1 ? "" : "s");
        for (arg = n->a; arg; arg = arg->next) {
            if (arg->kind == E_ARRAYARG)
                bc_runtime_error("function %s: an array argument where a value is expected", n->sym->name);
            args[k++] = eval(arg);
        }
        result = math_call(f->builtin, args, nargs, reg_scale);
        for (int i = 0; i < k; i++)
            num_free(&args[i]);
        return result;
    }
    if (nargs != f->nparams)
        bc_runtime_error("function %s: %d argument%s expected", n->sym->name, f->nparams,
                         f->nparams == 1 ? "" : "s");
    if (stack_exhausted())
        bc_runtime_error("function calls nested too deeply");
    /* evaluate the arguments, then push the parameters and autos */
    {
        Num *vals = malloc((size_t)(nargs ? nargs : 1) * sizeof *vals);
        Array **arrs = malloc((size_t)(nargs ? nargs : 1) * sizeof *arrs);
        Param *p;
        int i;
        long emark = npushed;
        if (!vals || !arrs)
            bc_nomem();
        for (arg = n->a, p = f->params, i = 0; arg; arg = arg->next, p = p->next, i++) {
            if (p->array != (arg->kind == E_ARRAYARG)) {
                for (int k = 0; k < i; k++) {
                    if (f->params && arrs[k])
                        free_array(arrs[k]);
                    else
                        num_free(&vals[k]);
                }
                free(vals);
                free(arrs);
                (void)emark;
                bc_runtime_error(p->array ? "function %s: parameter %s[] needs an array"
                                          : "function %s: parameter %s needs a value",
                                 n->sym->name, p->sym->name);
            }
            if (p->array) {
                arrs[i] = copy_array(arr_cur(arg->sym));
                vals[i] = num_zero();
            } else {
                arrs[i] = NULL;
                vals[i] = eval(arg);
            }
        }
        mark = npushed;
        for (p = f->params, i = 0; p; p = p->next, i++) {
            if (p->array)
                push_arr(p->sym, arrs[i]);
            else
                push_var(p->sym, vals[i]);
            note_push(p->sym, p->array);
        }
        free(vals);
        free(arrs);
        for (p = f->autos; p; p = p->next) {
            if (p->array) {
                Array *a = calloc(1, sizeof *a);
                if (!a)
                    bc_nomem();
                push_arr(p->sym, a);
            } else
                push_var(p->sym, num_zero());
            note_push(p->sym, p->array);
        }
    }
    depth++;
    retval = num_zero();
    {
        int r = X_NORMAL;
        for (Node *s = f->body; s && r == X_NORMAL; s = s->next)
            r = exec(s);
        if (r != X_RETURN) {
            num_free(&retval);
            retval = num_zero();
        }
    }
    depth--;
    result = retval;
    retval = num_zero();
    unwind_to(mark);
    return result;
}

static int truth(Node *n)
{
    Num v = eval(n);
    int t = !num_is_zero(v);
    num_free(&v);
    return t;
}

static Num eval(Node *n)
{
    Num a, b, r;
    switch (n->kind) {
    case E_NUM: {
        int base = (int)reg_ibase;
        if (n->cache_base != base) {
            num_free(&n->cache);
            n->cache = num_parse(n->str, base);
            n->cache_base = base;
        }
        return num_copy(n->cache);
    }
    case E_VAR:
    case E_SCALE:
    case E_IBASE:
    case E_OBASE:
        return load(n, 0);
    case E_ELEM: {
        long i = elem_index(n);
        return load(n, i);
    }
    case E_CALL:
        return call(n);
    case E_ARRAYARG:
        bc_runtime_error("%s[] used as a value", n->sym->name);
        return num_zero();
    case E_NEG:
        a = eval(n->a);
        r = num_neg(a);
        num_free(&a);
        return r;
    case E_ADD:
    case E_SUB:
    case E_MUL:
    case E_DIV:
    case E_MOD:
    case E_POW:
        a = eval(n->a);
        b = eval(n->b);
        r = arith(n->kind, a, b);
        num_free(&a);
        num_free(&b);
        return r;
    case E_PREINC:
    case E_PREDEC:
    case E_POSTINC:
    case E_POSTDEC: {
        long i = elem_index(n->a);
        Num old = load(n->a, i), one = num_from_long(1), nv, stored;
        nv = (n->kind == E_PREINC || n->kind == E_POSTINC) ? num_add(old, one) : num_sub(old, one);
        num_free(&one);
        stored = store(n->a, nv, i);
        if (n->kind == E_PREINC || n->kind == E_PREDEC) {
            num_free(&old);
            return stored;
        }
        num_free(&stored);
        return old;
    }
    case E_ASSIGN: {
        long i = elem_index(n->a);
        Num v;
        if (n->op == A_SET)
            v = eval(n->b);
        else {
            Num old = load(n->a, i);
            b = eval(n->b);
            v = arith(assign_kind(n->op), old, b);
            num_free(&old);
            num_free(&b);
        }
        return store(n->a, v, i);
    }
    case E_LENGTH:
        a = eval(n->a);
        r = num_from_long(num_length(a));
        num_free(&a);
        return r;
    case E_SQRT:
        a = eval(n->a);
        if (num_sqrt(a, reg_scale, &r)) {
            num_free(&a);
            bc_runtime_error("square root of a negative number");
        }
        num_free(&a);
        return r;
    case E_SCALEF:
        a = eval(n->a);
        r = num_from_long(a.scale);
        num_free(&a);
        return r;
    case E_REL: {
        int c;
        a = eval(n->a);
        b = eval(n->b);
        c = num_cmp(a, b);
        num_free(&a);
        num_free(&b);
        switch (n->op) {
        case R_EQ: c = c == 0; break;
        case R_NE: c = c != 0; break;
        case R_LT: c = c < 0; break;
        case R_LE: c = c <= 0; break;
        case R_GT: c = c > 0; break;
        default: c = c >= 0; break;
        }
        return num_from_long(c);
    }
    case E_AND:
        return num_from_long(truth(n->a) && truth(n->b));
    case E_OR:
        return num_from_long(truth(n->a) || truth(n->b));
    case E_NOT:
        return num_from_long(!truth(n->a));
    }
    bc_runtime_error("internal error: bad expression");
    return num_zero();
}

static void check_interrupt(void)
{
    if (interrupted) {
        interrupted = 0;
        bc_runtime_error("interrupted");
    }
}

static int exec_list(Node *s)
{
    int r = X_NORMAL;
    for (; s && r == X_NORMAL; s = s->next)
        r = exec(s);
    return r;
}

int exec_line;

static int exec(Node *n)
{
    exec_line = n->line;
    switch (n->kind) {
    case S_EXPR: {
        Num v = eval(n->a);
        if (n->a->kind != E_ASSIGN || n->a->paren) {
            print_num(v);
            out_plain('\n');
        }
        num_free(&v);
        return X_NORMAL;
    }
    case S_STRING:
        print_string(n->str, 0);
        return X_NORMAL;
    case S_PRINT:
        for (Node *p = n->a; p; p = p->next) {
            if (p->kind == S_STRING)
                print_string(p->str, 1);
            else {
                Num v = eval(p);
                print_num(v);
                num_free(&v);
            }
        }
        return X_NORMAL;
    case S_BREAK:
        return X_BREAK;
    case S_CONTINUE:
        return X_CONTINUE;
    case S_HALT:
        bc_quit(0);
        return X_NORMAL;
    case S_RETURN:
        num_free(&retval);
        retval = n->a ? eval(n->a) : num_zero();
        return X_RETURN;
    case S_IF:
        if (truth(n->a))
            return exec(n->b);
        if (n->c)
            return exec(n->c);
        return X_NORMAL;
    case S_WHILE:
        while (truth(n->a)) {
            int r;
            check_interrupt();
            r = exec(n->b);
            if (r == X_BREAK)
                break;
            if (r == X_RETURN)
                return r;
        }
        return X_NORMAL;
    case S_FOR:
        if (n->a) {
            Num v = eval(n->a);
            num_free(&v);
        }
        for (;;) {
            int r;
            if (n->b && !truth(n->b))
                break;
            check_interrupt();
            r = exec(n->d);
            if (r == X_BREAK)
                break;
            if (r == X_RETURN)
                return r;
            if (n->c) {
                Num v = eval(n->c);
                num_free(&v);
            }
        }
        return X_NORMAL;
    case S_BLOCK:
        return exec_list(n->a);
    }
    return X_NORMAL;
}

/* ---------- running ---------- */

void run_item(Node *list)
{
    long mark = npushed;
    interrupted = 0;
    if (!list)
        return;
    if (!check(list, 0, 0)) {
        free_tree(list);
        return;
    }
    if (sigsetjmp(top_jmp, 1)) {
        /* a runtime error: undo the calls in progress */
        unwind_to(mark);
        depth = 0;
        num_free(&retval);
        retval = num_zero();
        out_flush();
        free_tree(list);
        return;
    }
    exec_list(list);
    free_tree(list);
}

static void free_params(Param *p)
{
    while (p) {
        Param *n = p->next;
        free(p);
        p = n;
    }
}

void define_function(Sym *name, Param *params, Param *autos, Node *body, int line)
{
    Func *f;
    int n = 0;
    (void)line;
    /* names may not repeat among the parameters and autos */
    for (Param *p = params; p; p = p->next) {
        n++;
        for (Param *q = p->next; q; q = q->next)
            if (q->sym == p->sym && q->array == p->array)
                bc_error("function %s: parameter %s%s declared twice", name->name, p->sym->name,
                         p->array ? "[]" : "");
        for (Param *q = autos; q; q = q->next)
            if (q->sym == p->sym && q->array == p->array)
                bc_error("function %s: %s%s is both a parameter and an auto", name->name, p->sym->name,
                         p->array ? "[]" : "");
    }
    if (!check(body, 0, 1)) {
        free_tree(body);
        free_params(params);
        free_params(autos);
        return;
    }
    f = calloc(1, sizeof *f);
    if (!f)
        bc_nomem();
    f->params = params;
    f->autos = autos;
    f->nparams = n;
    f->body = body;
    /* the old definition is kept: a call to it may be running (it cannot
       be, at the top level, but its tree is small) */
    name->func = f;
}

void exec_init(void)
{
    const char *ll = getenv("BC_LINE_LENGTH");
    gnu_sqrt_quirk = getenv("BC_TEST_GNU_SQRT") != NULL;
    retval = num_zero();
    if (ll && *ll) {
        long v = strtol(ll, NULL, 10);
        if (v == 0 || v > 1)
            line_length = v > INT_MAX ? INT_MAX : (int)v;
    }
}
