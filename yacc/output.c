/*
 * output.c - the code file and the header file: token definitions,
 * the copied declarations, the packed parser tables and the parser
 * itself (skeleton below), with the actions.
 */
#include "yacc.h"

/* ---------- the output buffer, counting lines ---------- */

typedef struct Out {
    Str s;
    int line;            /* number of the line being written */
} Out;

static void put(Out *o, const char *s)
{
    for (const char *q = s; *q; q++)
        if (*q == '\n')
            o->line++;
    str_adds(&o->s, s);
}

static void putf(Out *o, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n >= 0 && (size_t)n < sizeof tmp) {
        put(o, tmp);
        return;
    }
    {
        char *big = xmalloc((size_t)n + 1);
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        put(o, big);
        free(big);
    }
}

static void put_quoted_name(Out *o, const char *name)
{
    put(o, "\"");
    for (const char *q = name; *q; q++) {
        char b[8];
        unsigned char c = (unsigned char)*q;
        if (c == '"' || c == '\\') {
            b[0] = '\\';
            b[1] = (char)c;
            b[2] = 0;
        } else if (c < 0x20 || c >= 0x7f)
            snprintf(b, sizeof b, "\\%03o", c);
        else {
            b[0] = (char)c;
            b[1] = 0;
        }
        put(o, b);
    }
    put(o, "\"");
}

/* #line to the grammar */
static void line_to(Out *o, int line)
{
    if (opt_l || line <= 0)
        return;
    putf(o, "#line %d ", line);
    put_quoted_name(o, input_name);
    put(o, "\n");
}

/* #line back to the code file */
static void line_back(Out *o)
{
    if (opt_l)
        return;
    putf(o, "#line %d ", o->line + 1);
    put_quoted_name(o, code_name);
    put(o, "\n");
}

int is_c_name(const char *s)
{
    if (!(isalpha((unsigned char)*s) || *s == '_'))
        return 0;
    for (s++; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '_'))
            return 0;
    return 1;
}

/* ---------- table packing ---------- */

typedef struct Vec {
    int n;
    int *idx, *val;      /* positions and values */
    int lo, hi;          /* min and max index */
    int base;
} Vec;

static int *table, *check;
static int tsize, tcap;
static int ylast;
static char *base_used;
static int base_off, base_cap;
#define BASE_NONE INT_MIN

static int *nextfree;          /* union-find: the first free position at or after i */

static void table_grow(int need)
{
    if (need <= tcap)
        return;
    int n = tcap ? tcap : 1024;
    while (n < need)
        n *= 2;
    table = xrealloc(table, (size_t)n * sizeof *table);
    check = xrealloc(check, (size_t)n * sizeof *check);
    nextfree = xrealloc(nextfree, ((size_t)n + 1) * sizeof *nextfree);
    for (int i = tcap; i < n; i++) {
        table[i] = 0;
        check[i] = -1;
        nextfree[i] = i;
    }
    nextfree[n] = n;
    tcap = n;
}

static int find_free(int i)
{
    int r = i;
    while (r < tcap && nextfree[r] != r)
        r = nextfree[r];
    while (i < tcap && nextfree[i] != i) {
        int nx = nextfree[i];
        nextfree[i] = r;
        i = nx;
    }
    return r;
}

static int base_taken(int b)
{
    int i = b + base_off;
    return i >= 0 && i < base_cap && base_used[i];
}

static void base_take(int b)
{
    int i = b + base_off;
    if (i >= base_cap) {
        int n = base_cap ? base_cap : 1024;
        while (n <= i)
            n *= 2;
        base_used = xrealloc(base_used, (size_t)n);
        memset(base_used + base_cap, 0, (size_t)(n - base_cap));
        base_cap = n;
    }
    base_used[i] = 1;
}

static int vec_cmp(const void *a, const void *b)
{
    const Vec *x = *(Vec *const *)a, *y = *(Vec *const *)b;
    int wx = x->hi - x->lo, wy = y->hi - y->lo;
    if (x->n != y->n)
        return y->n - x->n;
    if (wx != wy)
        return wy - wx;
    return 0;
}

/*
 * Vectors with the same positions (a "shape") are tried from where the
 * last one of that shape went: positions only fill up, so every earlier
 * base still fails.  Identical vectors share a base.
 */
typedef struct Shape {
    Vec *last;
    struct Shape *next;
} Shape;

static unsigned vec_hash(const Vec *x, int with_values)
{
    unsigned h = (unsigned)x->n * 2654435761u;
    for (int k = 0; k < x->n; k++) {
        h = h * 31 + (unsigned)x->idx[k];
        if (with_values)
            h = h * 31 + (unsigned)x->val[k];
    }
    return h;
}

static int same_shape(const Vec *a, const Vec *b)
{
    return a->n == b->n && !memcmp(a->idx, b->idx, (size_t)a->n * sizeof(int));
}

static void pack(Vec **v, int n)
{
    int nh = 1;
    Shape **shapes, **same;
    while (nh < 2 * n)
        nh *= 2;
    shapes = xcalloc((size_t)nh, sizeof *shapes);
    same = xcalloc((size_t)nh, sizeof *same);
    qsort(v, (size_t)n, sizeof *v, vec_cmp);
    for (int i = 0; i < n; i++) {
        Vec *x = v[i];
        Shape *sh, *sm;
        unsigned hs, hv;
        int b, p;
        if (!x->n) {
            x->base = BASE_NONE;
            continue;
        }
        /* an identical vector already placed */
        hv = vec_hash(x, 1) & (unsigned)(nh - 1);
        for (sm = same[hv]; sm; sm = sm->next)
            if (same_shape(sm->last, x) && !memcmp(sm->last->val, x->val, (size_t)x->n * sizeof(int)))
                break;
        if (sm) {
            x->base = sm->last->base;
            continue;
        }
        hs = vec_hash(x, 0) & (unsigned)(nh - 1);
        for (sh = shapes[hs]; sh; sh = sh->next)
            if (same_shape(sh->last, x))
                break;
        /* place the first entry at each free position in turn */
        if (sh) {
            table_grow(sh->last->base + x->idx[0] + 2);
            p = find_free(sh->last->base + x->idx[0] + 1);
            table_grow(p + 1);
        } else
            p = find_free(0);
        for (;;) {
            int ok = 1;
            b = p - x->idx[0];
            if (b + x->lo >= 0 && !base_taken(b)) {
                table_grow(b + x->hi + 1);
                for (int k = 1; k < x->n; k++)
                    if (check[b + x->idx[k]] != -1) {
                        ok = 0;
                        break;
                    }
            } else
                ok = 0;
            if (ok)
                break;
            table_grow(p + 2);
            p = find_free(p + 1);
            table_grow(p + 1);
        }
        for (int k = 0; k < x->n; k++) {
            int q = b + x->idx[k];
            table[q] = x->val[k];
            check[q] = x->idx[k];
            nextfree[q] = q + 1;
            if (q + 1 > tsize)
                tsize = q + 1;
        }
        base_take(b);
        x->base = b;
        if (sh)
            sh->last = x;
        else {
            sh = xmalloc(sizeof *sh);
            sh->last = x;
            sh->next = shapes[hs];
            shapes[hs] = sh;
        }
        sm = xmalloc(sizeof *sm);
        sm->last = x;
        sm->next = same[hv];
        same[hv] = sm;
    }
    for (int i = 0; i < nh; i++) {
        while (shapes[i]) {
            Shape *t = shapes[i]->next;
            free(shapes[i]);
            shapes[i] = t;
        }
        while (same[i]) {
            Shape *t = same[i]->next;
            free(same[i]);
            same[i] = t;
        }
    }
    free(shapes);
    free(same);
}

/* ---------- emitting arrays ---------- */

static void put_array(Out *o, const char *name, const int *v, int n)
{
    int lo = 0, hi = 0;
    const char *type;
    for (int i = 0; i < n; i++) {
        if (v[i] < lo)
            lo = v[i];
        if (v[i] > hi)
            hi = v[i];
    }
    if (lo >= -127 && hi <= 127)
        type = "signed char";
    else if (lo >= -32767 && hi <= 32767)
        type = "short";
    else
        type = "int";
    putf(o, "static const %s %s[] = {", type, name);
    for (int i = 0; i < n; i++) {
        if (i % 10 == 0)
            put(o, "\n   ");
        putf(o, " %d%s", v[i], i + 1 < n ? "," : "");
    }
    if (!n)
        put(o, " 0");
    put(o, "\n};\n");
}

/* ---------- the parser ---------- */

static const char *const skel_macros =
    "#ifndef YYINITDEPTH\n"
    "#define YYINITDEPTH 200\n"
    "#endif\n"
    "#ifndef YYMAXDEPTH\n"
    "#define YYMAXDEPTH 10000\n"
    "#endif\n"
    "\n"
    "#define YYACCEPT goto yyacceptlab\n"
    "#define YYABORT goto yyabortlab\n"
    "#define YYERROR goto yyerrlab\n"
    "#define yyerrok (yyerrflag = 0)\n"
    "#define yyclearin (yychar = YYEMPTY)\n"
    "#define YYRECOVERING() (yyerrflag != 0)\n"
    "\n";

static const char *const skel_begin =
    "int\n"
    "yyparse(void)\n"
    "{\n"
    "    int yystate, yyn, yym, yytoken, yyerrflag = 0, yyresult;\n"
    "    int yyssa[YYINITDEPTH];\n"
    "    YYSTYPE yyvsa[YYINITDEPTH];\n"
    "    int *yyss = yyssa, *yyssp;\n"
    "    YYSTYPE *yyvs = yyvsa, *yyvsp;\n"
    "    long yystacksize = YYINITDEPTH;\n"
    "    YYSTYPE yyval, yypushval;\n"
    "\n"
    "    yychar = YYEMPTY;\n"
    "    yynerrs = 0;\n"
    "    yyssp = yyss;\n"
    "    yyvsp = yyvs;\n"
    "    *yyssp = yystate = 0;\n"
    "    memset(yyvsp, 0, sizeof *yyvsp);\n"
    "    memset(&yyval, 0, sizeof yyval);\n"
    "    if (0)\n"
    "        goto yyerrlab;  /* YYERROR may not be used */\n"
    "\n"
    "yyloop:\n"
    "    yyn = yypact[yystate];\n"
    "    if (yyn == YYPACT_NONE)\n"
    "        goto yydefault;\n"
    "    if (yychar == YYEMPTY) {\n"
    "        yychar = yylex();\n"
    "        if (yychar <= 0)\n"
    "            yychar = YYEOF;\n"
    "#if YYDEBUG\n"
    "        if (yydebug)\n"
    "            fprintf(stderr, \"yydebug: state %d, reading %d (%s)\\n\", yystate, yychar,\n"
    "                    yytname[YYTRANSLATE(yychar)]);\n"
    "#endif\n"
    "    }\n"
    "    yytoken = YYTRANSLATE(yychar);\n"
    "    yyn += yytoken;\n"
    "    if (yyn < 0 || yyn > YYLAST || yycheck[yyn] != yytoken)\n"
    "        goto yydefault;\n"
    "    yyn = yytable[yyn];\n"
    "    if (yyn > 0) {\n"
    "#if YYDEBUG\n"
    "        if (yydebug)\n"
    "            fprintf(stderr, \"yydebug: state %d, shifting to state %d\\n\", yystate, yyn);\n"
    "#endif\n"
    "        if (yyerrflag > 0)\n"
    "            --yyerrflag;\n"
    "        yystate = yyn;\n"
    "        yypushval = yylval;\n"
    "        yychar = YYEMPTY;\n"
    "        goto yypush;\n"
    "    }\n"
    "    if (yyn == 0)\n"
    "        goto yyacceptlab;\n"
    "    if (yyn == YYERRACT)\n"
    "        goto yysyntaxerr;\n"
    "    yyn = -yyn;\n"
    "    goto yyreduce;\n"
    "\n"
    "yydefault:\n"
    "    yyn = yydefact[yystate];\n"
    "    if (yyn == 0)\n"
    "        goto yysyntaxerr;\n"
    "\n"
    "yyreduce:\n"
    "#if YYDEBUG\n"
    "    if (yydebug)\n"
    "        fprintf(stderr, \"yydebug: state %d, reducing by rule %d (%s)\\n\", yystate, yyn, yyrule[yyn]);\n"
    "#endif\n"
    "    yym = yyr2[yyn];\n"
    "    if (yym)\n"
    "        yyval = yyvsp[1 - yym];\n"
    "    else\n"
    "        memset(&yyval, 0, sizeof yyval);\n"
    "    switch (yyn) {\n";

static const char *const skel_end =
    "    default:\n"
    "        break;\n"
    "    }\n"
    "    yyssp -= yym;\n"
    "    yyvsp -= yym;\n"
    "    yyn = yyr1[yyn];\n"
    "    yym = yypgoto[yyn];\n"
    "    if (yym != YYPACT_NONE && (yym += *yyssp) >= 0 && yym <= YYLAST && yycheck[yym] == *yyssp)\n"
    "        yystate = yytable[yym];\n"
    "    else\n"
    "        yystate = yydefgoto[yyn];\n"
    "#if YYDEBUG\n"
    "    if (yydebug)\n"
    "        fprintf(stderr, \"yydebug: after reduction, shifting from state %d to state %d\\n\", *yyssp, yystate);\n"
    "#endif\n"
    "    yypushval = yyval;\n"
    "\n"
    "yypush:\n"
    "    if (yyssp >= yyss + yystacksize - 1) {\n"
    "        long yyoff = (long)(yyssp - yyss), yynewsize;\n"
    "        int *yyss1;\n"
    "        YYSTYPE *yyvs1;\n"
    "        if (yystacksize >= YYMAXDEPTH)\n"
    "            goto yyoverflow;\n"
    "        yynewsize = yystacksize * 2;\n"
    "        if (yynewsize > YYMAXDEPTH)\n"
    "            yynewsize = YYMAXDEPTH;\n"
    "        yyss1 = (int *)malloc((size_t)yynewsize * sizeof *yyss1);\n"
    "        yyvs1 = (YYSTYPE *)malloc((size_t)yynewsize * sizeof *yyvs1);\n"
    "        if (!yyss1 || !yyvs1) {\n"
    "            free(yyss1);\n"
    "            free(yyvs1);\n"
    "            goto yyoverflow;\n"
    "        }\n"
    "        memcpy(yyss1, yyss, (size_t)(yyoff + 1) * sizeof *yyss1);\n"
    "        memcpy(yyvs1, yyvs, (size_t)(yyoff + 1) * sizeof *yyvs1);\n"
    "        if (yyss != yyssa) {\n"
    "            free(yyss);\n"
    "            free(yyvs);\n"
    "        }\n"
    "        yyss = yyss1;\n"
    "        yyvs = yyvs1;\n"
    "        yyssp = yyss + yyoff;\n"
    "        yyvsp = yyvs + yyoff;\n"
    "        yystacksize = yynewsize;\n"
    "    }\n"
    "    *++yyssp = yystate;\n"
    "    *++yyvsp = yypushval;\n"
    "    goto yyloop;\n"
    "\n"
    "yysyntaxerr:\n"
    "    if (yyerrflag == 3) {\n"
    "        /* no symbol shifted since the last error: discard the lookahead */\n"
    "        if (yychar == YYEOF)\n"
    "            goto yyabortlab;\n"
    "#if YYDEBUG\n"
    "        if (yydebug)\n"
    "            fprintf(stderr, \"yydebug: state %d, error recovery discards token %d (%s)\\n\", yystate,\n"
    "                    yychar, yytname[YYTRANSLATE(yychar)]);\n"
    "#endif\n"
    "        yychar = YYEMPTY;\n"
    "        goto yyloop;\n"
    "    }\n"
    "    if (yyerrflag == 0) {\n"
    "        ++yynerrs;\n"
    "        yyerror(\"syntax error\");\n"
    "    }\n"
    "\n"
    "yyerrlab:\n"
    "    yyerrflag = 3;\n"
    "    for (;;) {\n"
    "        yyn = yypact[*yyssp];\n"
    "        if (yyn != YYPACT_NONE) {\n"
    "            yyn += YYERRSYM;\n"
    "            if (yyn >= 0 && yyn <= YYLAST && yycheck[yyn] == YYERRSYM && yytable[yyn] > 0) {\n"
    "#if YYDEBUG\n"
    "                if (yydebug)\n"
    "                    fprintf(stderr, \"yydebug: state %d, error recovery shifting to state %d\\n\",\n"
    "                            *yyssp, yytable[yyn]);\n"
    "#endif\n"
    "                yystate = yytable[yyn];\n"
    "                yypushval = yylval;\n"
    "                goto yypush;\n"
    "            }\n"
    "        }\n"
    "        if (yyssp == yyss)\n"
    "            goto yyabortlab;\n"
    "#if YYDEBUG\n"
    "        if (yydebug)\n"
    "            fprintf(stderr, \"yydebug: error recovery discarding state %d\\n\", *yyssp);\n"
    "#endif\n"
    "        --yyssp;\n"
    "        --yyvsp;\n"
    "    }\n"
    "\n"
    "yyoverflow:\n"
    "    yyerror(\"yacc stack overflow\");\n"
    "    yyresult = 2;\n"
    "    goto yyreturn;\n"
    "\n"
    "yyabortlab:\n"
    "#if YYDEBUG\n"
    "    if (yydebug)\n"
    "        fprintf(stderr, \"yydebug: abort\\n\");\n"
    "#endif\n"
    "    yyresult = 1;\n"
    "    goto yyreturn;\n"
    "\n"
    "yyacceptlab:\n"
    "#if YYDEBUG\n"
    "    if (yydebug)\n"
    "        fprintf(stderr, \"yydebug: accept\\n\");\n"
    "#endif\n"
    "    yyresult = 0;\n"
    "\n"
    "yyreturn:\n"
    "    if (yyss != yyssa) {\n"
    "        free(yyss);\n"
    "        free(yyvs);\n"
    "    }\n"
    "    return yyresult;\n"
    "}\n";

static const char *const renamed[] = { "parse", "lex", "error", "lval", "char", "debug", "nerrs" };

static void put_token_defines(Out *o)
{
    for (int i = 0; i < ntokens; i++) {
        Symbol *s = symbols[i];
        if (i == 1 || i == 2 || s->literal || s->name[0] == '$' || !is_c_name(s->name))
            continue;
        putf(o, "#define %s %d\n", s->name, s->value);
    }
}

static void put_stype(Out *o, int with_lines)
{
    if (union_body) {
        put(o, "#if !defined(YYSTYPE) && !defined(YYSTYPE_IS_DECLARED)\n");
        if (with_lines)
            line_to(o, union_line);
        put(o, "typedef union YYSTYPE ");
        put(o, union_body);
        put(o, " YYSTYPE;\n");
        if (with_lines)
            line_back(o);
        put(o, "#define YYSTYPE_IS_DECLARED 1\n#endif\n");
    } else if (!have_tags) {
        put(o, "#if !defined(YYSTYPE) && !defined(YYSTYPE_IS_DECLARED)\n"
               "typedef int YYSTYPE;\n"
               "#define YYSTYPE_IS_DECLARED 1\n"
               "#endif\n");
    }
}

static void put_rule_string(Out *o, int r)
{
    Str s = { 0 };
    str_adds(&s, symbols[rules[r].lhs]->name);
    str_adds(&s, " :");
    for (int i = rules[r].rhs; ritem[i] >= 0; i++) {
        str_addc(&s, ' ');
        str_adds(&s, symbols[ritem[i]]->name);
    }
    put_quoted_name(o, s.s);
    free(s.s);
}

void write_code(FILE *fp, FILE *hdr)
{
    Out o = { { 0 }, 1 };
    const char *pfx = sym_prefix ? sym_prefix : "yy";
    int i;

    putf(&o, "/* A parser generated by yacc from %s.  */\n\n", input_name);
    putf(&o, "#define YYYACC 1\n");
    if (sym_prefix)
        for (i = 0; i < (int)(sizeof renamed / sizeof renamed[0]); i++)
            putf(&o, "#define yy%s %s%s\n", renamed[i], pfx, renamed[i]);
    put(&o, "#define YYEMPTY (-2)\n#define YYEOF 0\n");
    put_token_defines(&o);
    putf(&o, "#define YYERRCODE %d\n", symbols[1]->value);
    putf(&o, "#ifndef YYDEBUG\n#define YYDEBUG %d\n#endif\n", opt_t ? 1 : 0);
    put(&o, "\n");

    /* declarations, in order */
    for (i = 0; i < nchunks; i++) {
        if (chunks[i].is_union) {
            put_stype(&o, 1);
            continue;
        }
        line_to(&o, chunks[i].line);
        put(&o, chunks[i].text);
        if (o.s.len && o.s.s[o.s.len - 1] != '\n')
            put(&o, "\n");
        line_back(&o);
    }
    if (!union_body)
        put_stype(&o, 0);
    put(&o, "\n");

    put(&o, "int yychar;\nYYSTYPE yylval;\nint yynerrs;\nint yydebug;\n\n");
    putf(&o, "#if !defined(%serror) && !defined(YYERROR_IS_DECLARED)\nvoid yyerror(const char *);\n#endif\n", pfx);
    putf(&o, "#if !defined(%slex) && !defined(YYLEX_IS_DECLARED)\nint yylex(void);\n#endif\n", pfx);
    put(&o, "int yyparse(void);\n\n");
    put(&o, "#include <stdlib.h>\n#include <string.h>\n#if YYDEBUG\n#include <stdio.h>\n#endif\n\n");

    /* ---------- tables ---------- */
    {
        int nvec = nstates + nnonterms;
        Vec *vecs = xcalloc((size_t)nvec, sizeof *vecs);
        Vec **order = xmalloc((size_t)nvec * sizeof *order);
        int *pact = xmalloc((size_t)nstates * sizeof(int));
        int *defact = xmalloc((size_t)nstates * sizeof(int));
        int *pgoto = xmalloc((size_t)nnonterms * sizeof(int));
        int *defgoto = xmalloc((size_t)nnonterms * sizeof(int));
        int *r1 = xmalloc((size_t)nrules * sizeof(int));
        int *r2 = xmalloc((size_t)nrules * sizeof(int));
        int errval = -nrules;
        int minbase = 0, pact_none;
        int *cnt = xcalloc((size_t)nstates, sizeof(int));

        base_off = (ntokens > nstates ? ntokens : nstates) + 1;
        for (int s = 0; s < nstates; s++) {
            Vec *v = &vecs[s];
            int n = 0;
            for (int t = 0; t < ntokens; t++)
                if (actions[s][t].kind != A_NONE)
                    n++;
            v->idx = xmalloc((size_t)(n ? n : 1) * sizeof(int));
            v->val = xmalloc((size_t)(n ? n : 1) * sizeof(int));
            for (int t = 0; t < ntokens; t++) {
                Action *a = &actions[s][t];
                int val;
                switch (a->kind) {
                case A_NONE: continue;
                case A_SHIFT: val = a->arg; break;
                case A_REDUCE: val = -a->arg; break;
                case A_ACCEPT: val = 0; break;
                default: val = errval; break;
                }
                if (!v->n || t < v->lo)
                    v->lo = t;
                if (!v->n || t > v->hi)
                    v->hi = t;
                v->idx[v->n] = t;
                v->val[v->n] = val;
                v->n++;
            }
            defact[s] = defred[s] >= 0 ? defred[s] : 0;
        }
        /* gotos: the transitions on each nonterminal, collected in one pass */
        {
            int *gcount = xcalloc((size_t)nnonterms + 1, sizeof(int));
            int *gstart, *gfrom, *gto, total = 0;
            for (int s = 0; s < nstates; s++)
                for (int j = 0; j < states[s].nshifts; j++)
                    if (states[s].shift_sym[j] >= ntokens)
                        gcount[states[s].shift_sym[j] - ntokens]++;
            gstart = xmalloc(((size_t)nnonterms + 1) * sizeof(int));
            for (int a = 0; a < nnonterms; a++) {
                gstart[a] = total;
                total += gcount[a];
                gcount[a] = 0;
            }
            gstart[nnonterms] = total;
            gfrom = xmalloc(((size_t)total + 1) * sizeof(int));
            gto = xmalloc(((size_t)total + 1) * sizeof(int));
            for (int s = 0; s < nstates; s++)
                for (int j = 0; j < states[s].nshifts; j++)
                    if (states[s].shift_sym[j] >= ntokens) {
                        int a = states[s].shift_sym[j] - ntokens;
                        int k = gstart[a] + gcount[a]++;
                        gfrom[k] = s;
                        gto[k] = states[s].shift_to[j];
                    }
            for (int a = 0; a < nnonterms; a++) {
                Vec *v = &vecs[nstates + a];
                int lo = gstart[a], hi = gstart[a + 1], best = -1, bestn = 0;
                for (int k = lo; k < hi; k++)
                    cnt[gto[k]]++;
                for (int k = lo; k < hi; k++)
                    if (cnt[gto[k]] > bestn || (cnt[gto[k]] == bestn && gto[k] < best)) {
                        bestn = cnt[gto[k]];
                        best = gto[k];
                    }
                for (int k = lo; k < hi; k++)
                    cnt[gto[k]] = 0;
                defgoto[a] = best >= 0 ? best : 0;
                v->idx = xmalloc((size_t)(hi - lo + 1) * sizeof(int));
                v->val = xmalloc((size_t)(hi - lo + 1) * sizeof(int));
                for (int k = lo; k < hi; k++) {
                    if (gto[k] == best)
                        continue;
                    if (!v->n || gfrom[k] < v->lo)
                        v->lo = gfrom[k];
                    if (!v->n || gfrom[k] > v->hi)
                        v->hi = gfrom[k];
                    v->idx[v->n] = gfrom[k];
                    v->val[v->n] = gto[k];
                    v->n++;
                }
            }
            free(gcount);
            free(gstart);
            free(gfrom);
            free(gto);
        }
        for (i = 0; i < nvec; i++)
            order[i] = &vecs[i];
        table_grow(1);
        pack(order, nvec);
        if (tsize == 0)
            tsize = 1;
        ylast = tsize - 1;
        for (i = 0; i < nvec; i++)
            if (vecs[i].base != BASE_NONE && vecs[i].base < minbase)
                minbase = vecs[i].base;
        pact_none = minbase - 1 - ntokens;
        for (int s = 0; s < nstates; s++)
            pact[s] = vecs[s].base == BASE_NONE ? pact_none : vecs[s].base;
        for (int a = 0; a < nnonterms; a++)
            pgoto[a] = vecs[nstates + a].base == BASE_NONE ? pact_none : vecs[nstates + a].base;
        /* check the packed tables against the actions they encode */
        for (int s = 0; s < nstates; s++) {
            for (int t = 0; t < ntokens; t++) {
                Action *ac = &actions[s][t];
                int want = ac->kind == A_SHIFT ? ac->arg : ac->kind == A_REDUCE ? -ac->arg
                         : ac->kind == A_ACCEPT ? 0 : errval;
                int q = pact[s] == pact_none ? -1 : pact[s] + t;
                int hit = q >= 0 && q <= ylast && check[q] == t;
                if (ac->kind == A_NONE ? hit : (!hit || table[q] != want))
                    fatal("internal error: action table wrong for state %d, token %d", s, t);
            }
            for (int j = 0; j < states[s].nshifts; j++) {
                int a = states[s].shift_sym[j] - ntokens, to = states[s].shift_to[j], q, got;
                if (a < 0)
                    continue;
                q = pgoto[a] == pact_none ? -1 : pgoto[a] + s;
                got = q >= 0 && q <= ylast && check[q] == s ? table[q] : defgoto[a];
                if (got != to)
                    fatal("internal error: goto table wrong for state %d, symbol %d", s, a);
            }
        }
        for (int r = 0; r < nrules; r++) {
            r1[r] = rules[r].lhs - ntokens;
            r2[r] = rules[r].len;
        }

        putf(&o, "#define YYPACT_NONE (%d)\n", pact_none);
        putf(&o, "#define YYERRACT (%d)\n", errval);
        putf(&o, "#define YYLAST %d\n", ylast);
        putf(&o, "#define YYERRSYM 1\n");
        putf(&o, "#define YYNSTATES %d\n", nstates);
        putf(&o, "#define YYNRULES %d\n", nrules);
        putf(&o, "#define YYMAXTOKEN %d\n\n", max_token_value);

        /* token translation */
        if (max_token_value <= 65535) {
            int *tr = xmalloc(((size_t)max_token_value + 1) * sizeof(int));
            for (int k = 0; k <= max_token_value; k++)
                tr[k] = 2;
            for (int t = 0; t < ntokens; t++)
                if (t != 2)
                    tr[symbols[t]->value] = t;
            put_array(&o, "yytranslate", tr, max_token_value + 1);
            put(&o, "#define YYTRANSLATE(c) ((unsigned)(c) <= (unsigned)YYMAXTOKEN ? yytranslate[c] : 2)\n\n");
            free(tr);
        } else {
            /* sparse token numbers: a sorted table and a binary search */
            int n = ntokens - 1, k = 0;
            int *vals = xmalloc((size_t)n * sizeof(int)), *syms = xmalloc((size_t)n * sizeof(int));
            for (int t = 0; t < ntokens; t++) {
                if (t == 2)
                    continue;
                int j = k++;
                while (j > 0 && vals[j - 1] > symbols[t]->value) {
                    vals[j] = vals[j - 1];
                    syms[j] = syms[j - 1];
                    j--;
                }
                vals[j] = symbols[t]->value;
                syms[j] = t;
            }
            put(&o, "static const int yytokval[] = {");
            for (k = 0; k < n; k++)
                putf(&o, "%s%d", k % 10 ? ", " : (k ? ",\n    " : "\n    "), vals[k]);
            put(&o, "\n};\n");
            put_array(&o, "yytoksym", syms, n);
            putf(&o, "static int yytrans(int c)\n{\n    int lo = 0, hi = %d;\n"
                     "    while (lo <= hi) {\n        int m = (lo + hi) / 2;\n"
                     "        if (yytokval[m] == c)\n            return yytoksym[m];\n"
                     "        if (yytokval[m] < c)\n            lo = m + 1;\n        else\n            hi = m - 1;\n"
                     "    }\n    return 2;\n}\n#define YYTRANSLATE(c) yytrans(c)\n\n", n - 1);
            free(vals);
            free(syms);
        }
        put_array(&o, "yyr1", r1, nrules);
        put_array(&o, "yyr2", r2, nrules);
        put_array(&o, "yydefact", defact, nstates);
        put_array(&o, "yypact", pact, nstates);
        put_array(&o, "yypgoto", pgoto, nnonterms);
        put_array(&o, "yydefgoto", defgoto, nnonterms);
        put_array(&o, "yytable", table, tsize);
        put_array(&o, "yycheck", check, tsize);

        put(&o, "\n#if YYDEBUG\nstatic const char *const yytname[] = {");
        for (i = 0; i < nsyms; i++) {
            put(&o, i % 4 ? " " : "\n    ");
            put_quoted_name(&o, symbols[i]->name);
            put(&o, ",");
        }
        put(&o, "\n};\nstatic const char *const yyrule[] = {");
        for (i = 0; i < nrules; i++) {
            put(&o, "\n    ");
            put_rule_string(&o, i);
            put(&o, ",");
        }
        put(&o, "\n};\n#endif\n\n");

        for (i = 0; i < nvec; i++) {
            free(vecs[i].idx);
            free(vecs[i].val);
        }
        free(vecs);
        free(order);
        free(pact);
        free(defact);
        free(pgoto);
        free(defgoto);
        free(r1);
        free(r2);
        free(cnt);
    }

    put(&o, skel_macros);
    put(&o, skel_begin);
    for (int r = 1; r < nrules; r++) {
        if (!rules[r].action)
            continue;
        putf(&o, "    case %d:\n", r);
        line_to(&o, rules[r].action_line);
        put(&o, rules[r].action);
        put(&o, "\n");
        line_back(&o);
        put(&o, "        break;\n");
    }
    put(&o, skel_end);

    if (programs) {
        put(&o, "\n");
        line_to(&o, programs_line);
        put(&o, programs);
        if (o.s.len && o.s.s[o.s.len - 1] != '\n')
            put(&o, "\n");
    }
    fwrite(o.s.s, 1, o.s.len, fp);
    free(o.s.s);

    if (hdr) {
        Out h = { { 0 }, 1 };
        put(&h, "/* Token definitions for a parser generated by yacc.  */\n\n");
        put_token_defines(&h);
        put(&h, "\n");
        put_stype(&h, 0);
        putf(&h, "extern YYSTYPE %slval;\n", pfx);
        putf(&h, "int %sparse(void);\n", pfx);
        fwrite(h.s.s, 1, h.s.len, hdr);
        free(h.s.s);
    }
}
