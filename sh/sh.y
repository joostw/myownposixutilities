%{
/*
 * sh.y - the shell grammar (XCU 2.10), for yacc -p shy.
 *
 * The grammar follows the standard's, with these differences, all
 * carried over from the shell's earlier hand-written parser:
 *   - compound lists may be empty ("{ }", "if then fi");
 *   - each kind of command line has its own start token: a complete
 *     command from the shell's input (START_LINE, accepted at its
 *     newline without reading further), the commands of "$(...)"
 *     (START_CMDSUB, up to the ")") and a string such as the text of
 *     a backquoted command (START_STRING, up to the end).
 *
 * Tokens come from shylex() in parser.c, which recognizes reserved
 * words, assignment words, function names and alias substitutions
 * where the shell's rules call for them.  The syntax errors that
 * concern a single token in a fixed place (a bad "for" variable, a case
 * pattern, a missing redirection target, ...) are reported there; the
 * error rules here report a missing "then", "fi", "do", "done", "}"
 * or ")", and everything else is reported as an unexpected token.
 * Each construct is split into parts (if_then, while_do, ...) so that
 * an error rule is on the parser's stack only while its keyword is the
 * next token.
 */
#include "sh.h"
#include "parse.h"

#define YYMAXDEPTH 10000000

static SCmd *sc_new(int line);
static void sc_assign(SCmd *s, Word *w);
static void sc_word(SCmd *s, Word *w);
static void sc_redir(SCmd *s, Redir *r);
static Seq *seq_new(Node *n);
static void seq_add(Seq *q, Node *n);
static Node *seq_node(Seq *q, int line);
static Node *item(Node *ao, int async);
static RV *redir(Tok *op, Tok *target, Tok *ionum);
static RList *rlist_add(RList *l, Redir *r);
static WList *wlist_add(WList *l, Word *w);
static CList *clist_add(CList *l, CaseItem *ci);
static CaseItem *case_item(WList *pats, Node *body, int fallthrough);
static Node *unary(int type, int line, Node *a);
static Node *for_node(Tok *f, Tok *name, WList *words);
%}

%union {
    Tok *t;
    Node *n;
    RV *rv;
    RList *rl;
    WList *wl;
    CList *cl;
    CaseItem *ci;
    SCmd *sc;
    Seq *q;
}

%token <t> WORD ASSIGNMENT_WORD FNAME IO_NUMBER NEWLINE EOFTOK LATE
%token <t> AND_IF OR_IF DSEMI SEMI_AND SEMI AMP PIPE LPAREN RPAREN
%token <t> LESS GREAT DLESS DGREAT LESSAND GREATAND LESSGREAT DLESSDASH CLOBBER
%token <t> If Then Else Elif Fi Do Done Case Esac While Until For Lbrace Rbrace Bang In
%token START_LINE START_CMDSUB START_STRING

%type <n> tlist tlist_s compound_list clist clist_s and_or pipeline command compound_command
%type <n> subshell brace_group if_clause else_part while_clause until_clause for_clause case_clause
%type <n> function_definition function_body simple_command
%type <n> if_then elif_then while_do until_do for_do
%type <t> io_op
%type <rv> io_redirect io_file
%type <rl> redirect_list
%type <wl> wordlist patterns pattern_part
%type <cl> case_items
%type <ci> case_item case_item_ns
%type <sc> sc_pre sc_full
%type <q> pipe_seq

%start start
%%

start           : START_LINE line
                | START_CMDSUB compound_list RPAREN
                    { parse_result = $2; YYACCEPT; }
                | START_CMDSUB compound_list error
                    { shy_syntax(CTX_CMDSUB, NULL); }
                | START_STRING compound_list EOFTOK
                    { parse_result = $2; YYACCEPT; }
                | START_STRING compound_list error
                    { shy_syntax(CTX_GENERIC, NULL); }
                ;

/* a complete command; the newline that ends it is the last token read */
line            : NEWLINE
                    { parse_result = NULL; YYACCEPT; }
                | EOFTOK
                    { parse_result = NULL; parse_at_eof = 1; YYACCEPT; }
                | tlist NEWLINE
                    { parse_result = $1; YYACCEPT; }
                | tlist EOFTOK
                    { parse_result = $1; YYACCEPT; }
                | error
                    { shy_syntax(CTX_GENERIC, NULL); }
                ;

tlist           : and_or                    { $$ = item($1, 0); }
                | tlist_s                   { $$ = $1; }
                | tlist_s and_or            { $$ = make_list($1, item($2, 0), node_first_line($2)); }
                ;
tlist_s         : and_or SEMI               { $$ = item($1, 0); }
                | and_or AMP                { $$ = item($1, 1); }
                | tlist_s and_or SEMI       { $$ = make_list($1, item($2, 0), node_first_line($2)); }
                | tlist_s and_or AMP        { $$ = make_list($1, item($2, 1), node_first_line($2)); }
                ;

compound_list   : linebreak                 { $$ = NULL; }
                | linebreak clist           { $$ = $2; }
                ;
clist           : and_or                    { $$ = item($1, 0); }
                | clist_s                   { $$ = $1; }
                | clist_s and_or            { $$ = make_list($1, item($2, 0), node_first_line($2)); }
                ;
clist_s         : and_or SEMI linebreak     { $$ = item($1, 0); }
                | and_or AMP linebreak      { $$ = item($1, 1); }
                | and_or newline_list       { $$ = item($1, 0); }
                | clist_s and_or SEMI linebreak
                                            { $$ = make_list($1, item($2, 0), node_first_line($2)); }
                | clist_s and_or AMP linebreak
                                            { $$ = make_list($1, item($2, 1), node_first_line($2)); }
                | clist_s and_or newline_list
                                            { $$ = make_list($1, item($2, 0), node_first_line($2)); }
                ;

and_or          : pipeline                  { $$ = $1; }
                | and_or AND_IF linebreak pipeline
                    { $$ = new_node(N_AND, $2->line); $$->u.bin.a = $1; $$->u.bin.b = $4; }
                | and_or OR_IF linebreak pipeline
                    { $$ = new_node(N_OR, $2->line); $$->u.bin.a = $1; $$->u.bin.b = $4; }
                ;

pipeline        : pipe_seq                  { $$ = seq_node($1, node_first_line($1->v[0])); }
                | Bang pipe_seq             { $$ = unary(N_NOT, $1->line, seq_node($2, $1->line)); }
                ;
pipe_seq        : command                   { $$ = seq_new($1); }
                | pipe_seq PIPE linebreak command
                                            { seq_add($1, $4); $$ = $1; }
                ;

command         : simple_command            { $$ = $1; }
                | compound_command          { $$ = $1; }
                | compound_command redirect_list
                                            { $$ = $1; $$->redir = $2->head; }
                | function_definition       { $$ = $1; }
                ;

compound_command: brace_group | subshell | for_clause | case_clause | if_clause
                | while_clause | until_clause
                ;

subshell        : LPAREN compound_list RPAREN
                    { $$ = unary(N_SUBSHELL, $1->line, $2); }
                | LPAREN compound_list error
                    { shy_syntax(CTX_RPAREN, NULL); }
                ;
brace_group     : Lbrace compound_list Rbrace
                    { $$ = unary(N_BRACE, $1->line, $2); }
                | Lbrace compound_list error
                    { shy_syntax(CTX_EXPECT, "}"); }
                ;

if_then         : If compound_list Then
                    { $$ = new_node(N_IF, $1->line); $$->u.nif.cond = $2; }
                | If compound_list error
                    { shy_syntax(CTX_EXPECT, "then"); }
                ;
if_clause       : if_then compound_list else_part Fi
                    { $$ = $1; $$->u.nif.then = $2; $$->u.nif.els = $3; }
                | if_then compound_list else_part error
                    { shy_syntax(CTX_EXPECT, "fi"); }
                ;
elif_then       : Elif compound_list Then
                    { $$ = new_node(N_IF, $1->line); $$->u.nif.cond = $2; }
                | Elif compound_list error
                    { shy_syntax(CTX_EXPECT, "then"); }
                ;
else_part       : /* empty */               { $$ = NULL; }
                | Else compound_list        { $$ = $2; }
                | elif_then compound_list else_part
                    { $$ = $1; $$->u.nif.then = $2; $$->u.nif.els = $3; }
                ;

while_do        : While compound_list Do
                    { $$ = new_node(N_WHILE, $1->line); $$->u.loop.cond = $2; }
                | While compound_list error
                    { shy_syntax(CTX_EXPECT, "do"); }
                ;
until_do        : Until compound_list Do
                    { $$ = new_node(N_UNTIL, $1->line); $$->u.loop.cond = $2; }
                | Until compound_list error
                    { shy_syntax(CTX_EXPECT, "do"); }
                ;
while_clause    : while_do compound_list Done
                    { $$ = $1; $$->u.loop.body = $2; }
                | while_do compound_list error
                    { shy_syntax(CTX_EXPECT, "done"); }
                ;
until_clause    : until_do compound_list Done
                    { $$ = $1; $$->u.loop.body = $2; }
                | until_do compound_list error
                    { shy_syntax(CTX_EXPECT, "done"); }
                ;

/* the "do" of a for loop is checked by the classifier */
for_do          : For WORD linebreak Do
                    { $$ = for_node($1, $2, NULL); }
                | For WORD SEMI linebreak Do
                    { $$ = for_node($1, $2, NULL); }
                | For WORD linebreak In wordlist SEMI linebreak Do
                    { $$ = for_node($1, $2, $5); }
                | For WORD linebreak In wordlist newline_list Do
                    { $$ = for_node($1, $2, $5); }
                ;
for_clause      : for_do compound_list Done
                    { $$ = $1; $$->u.nfor.body = $2; }
                | for_do compound_list error
                    { shy_syntax(CTX_EXPECT, "done"); }
                ;
wordlist        : /* empty */               { $$ = wlist_add(NULL, NULL); }
                | wordlist WORD             { $$ = wlist_add($1, $2->word); }
                ;

case_clause     : Case WORD linebreak In linebreak case_items Esac
                    { $$ = new_node(N_CASE, $1->line);
                      $$->u.ncase.word = $2->word; $$->u.ncase.items = $6->head; }
                | Case WORD linebreak In linebreak case_items case_item_ns Esac
                    { $$ = new_node(N_CASE, $1->line);
                      $$->u.ncase.word = $2->word; $$->u.ncase.items = clist_add($6, $7)->head; }
                ;
case_items      : /* empty */               { $$ = clist_add(NULL, NULL); }
                | case_items case_item      { $$ = clist_add($1, $2); }
                ;
case_item       : pattern_part compound_list DSEMI linebreak
                                            { $$ = case_item($1, $2, 0); }
                | pattern_part compound_list SEMI_AND linebreak
                                            { $$ = case_item($1, $2, 1); }
                ;
case_item_ns    : pattern_part compound_list
                                            { $$ = case_item($1, $2, 0); }
                ;
pattern_part    : patterns RPAREN           { $$ = $1; }
                | LPAREN patterns RPAREN    { $$ = $2; }
                ;
patterns        : WORD                      { $$ = wlist_add(NULL, $1->word); }
                | patterns PIPE WORD        { $$ = wlist_add($1, $3->word); }
                ;

function_definition
                : FNAME LPAREN RPAREN linebreak function_body
                    { $$ = new_node(N_FUNCDEF, $1->line);
                      $$->u.func.name = word_literal($1->word); $$->u.func.body = $5; }
                ;
function_body   : compound_command          { $$ = $1; }
                | compound_command redirect_list
                                            { $$ = $1; $$->redir = $2->head; }
                ;

simple_command  : sc_pre                    { $$ = $1->n; }
                | sc_full                   { $$ = $1->n; }
                ;
sc_pre          : ASSIGNMENT_WORD           { $$ = sc_new($1->line); sc_assign($$, $1->word); }
                | io_redirect               { $$ = sc_new($1->line); sc_redir($$, $1->r); }
                | sc_pre ASSIGNMENT_WORD    { $$ = $1; sc_assign($$, $2->word); }
                | sc_pre io_redirect        { $$ = $1; sc_redir($$, $2->r); }
                ;
sc_full         : WORD                      { $$ = sc_new($1->line); sc_word($$, $1->word); }
                | sc_pre WORD               { $$ = $1; sc_word($$, $2->word); }
                | sc_full WORD              { $$ = $1; sc_word($$, $2->word); }
                | sc_full io_redirect       { $$ = $1; sc_redir($$, $2->r); }
                ;

redirect_list   : io_redirect               { $$ = rlist_add(NULL, $1->r); }
                | redirect_list io_redirect { $$ = rlist_add($1, $2->r); }
                ;
io_redirect     : io_file                   { $$ = $1; }
                | IO_NUMBER io_op WORD      { $$ = redir($2, $3, $1); }
                ;
io_file         : io_op WORD                { $$ = redir($1, $2, NULL); }
                ;
io_op           : LESS | GREAT | DGREAT | LESSAND | GREATAND | LESSGREAT | CLOBBER
                | DLESS | DLESSDASH
                ;

newline_list    : NEWLINE
                | newline_list NEWLINE
                ;
linebreak       : /* empty */
                | newline_list
                ;

%%

/* ---------- building the tree ---------- */

static SCmd *sc_new(int line)
{
    SCmd *s = stalloc(sizeof *s);
    s->n = new_node(N_SIMPLE, line);
    s->at = &s->n->u.simple.assigns;
    s->wt = &s->n->u.simple.words;
    s->rt = &s->n->redir;
    return s;
}

static void sc_assign(SCmd *s, Word *w)
{
    *s->at = w;
    s->at = &w->next;
}

static void sc_word(SCmd *s, Word *w)
{
    *s->wt = w;
    s->wt = &w->next;
}

static void sc_redir(SCmd *s, Redir *r)
{
    *s->rt = r;
    s->rt = &r->next;
}

static Seq *seq_new(Node *n)
{
    Seq *q = stalloc(sizeof *q);
    q->cap = 64;
    q->v = stalloc((size_t)q->cap * sizeof *q->v);
    q->v[0] = n;
    q->n = 1;
    return q;
}

static void seq_add(Seq *q, Node *n)
{
    if (q->n == q->cap) {
        Node **nv = stalloc((size_t)q->cap * 2 * sizeof *nv);
        memcpy(nv, q->v, (size_t)q->cap * sizeof *nv);
        q->v = nv;
        q->cap *= 2;
    }
    q->v[q->n++] = n;
}

static Node *seq_node(Seq *q, int line)
{
    Node *n;
    if (q->n == 1)
        return q->v[0];
    n = new_node(N_PIPE, line);
    n->u.pipe.n = q->n;
    n->u.pipe.cmds = stalloc((size_t)q->n * sizeof(Node *));
    memcpy(n->u.pipe.cmds, q->v, (size_t)q->n * sizeof(Node *));
    return n;
}

static Node *unary(int type, int line, Node *a)
{
    Node *n = new_node(type, line);
    n->u.un.a = a;
    return n;
}

/* a list element: an and-or list, run asynchronously when it ended with & */
static Node *item(Node *ao, int async)
{
    return async ? unary(N_ASYNC, node_first_line(ao), ao) : ao;
}

static Node *for_node(Tok *f, Tok *name, WList *words)
{
    Node *n = new_node(N_FOR, f->line);
    n->u.nfor.var = word_literal(name->word);
    if (words) {
        n->u.nfor.has_in = 1;
        n->u.nfor.words = words->head;
    }
    return n;
}

static RV *redir(Tok *op, Tok *target, Tok *ionum)
{
    RV *v = stalloc(sizeof *v);
    Redir *r = target->here;
    int fd = ionum ? ionum->ionum : -1, def = 0;
    if (!r) {
        r = stalloc(sizeof *r);
        memset(r, 0, sizeof *r);
    }
    r->word = target->word;
    switch (op->ytok) {
    case LESS: r->type = R_IN; break;
    case GREAT: r->type = R_OUT; def = 1; break;
    case CLOBBER: r->type = R_CLOBBER; def = 1; break;
    case DGREAT: r->type = R_APPEND; def = 1; break;
    case LESSGREAT: r->type = R_RDWR; break;
    case LESSAND: r->type = R_DUPIN; break;
    case GREATAND: r->type = R_DUPOUT; def = 1; break;
    case DLESS: r->type = R_HERE; break;
    case DLESSDASH: r->type = R_HERESTRIP; break;
    }
    r->fd = fd < 0 ? def : fd;
    v->r = r;
    v->line = ionum ? ionum->line : op->line;
    return v;
}

static RList *rlist_add(RList *l, Redir *r)
{
    if (!l) {
        l = stalloc(sizeof *l);
        l->head = NULL;
        l->tail = &l->head;
    }
    *l->tail = r;
    l->tail = &r->next;
    return l;
}

static WList *wlist_add(WList *l, Word *w)
{
    if (!l) {
        l = stalloc(sizeof *l);
        l->head = NULL;
        l->tail = &l->head;
    }
    if (w) {
        *l->tail = w;
        l->tail = &w->next;
    }
    return l;
}

static CList *clist_add(CList *l, CaseItem *ci)
{
    if (!l) {
        l = stalloc(sizeof *l);
        l->head = NULL;
        l->tail = &l->head;
    }
    if (ci) {
        *l->tail = ci;
        l->tail = &ci->next;
    }
    return l;
}

static CaseItem *case_item(WList *pats, Node *body, int fallthrough)
{
    CaseItem *ci = stalloc(sizeof *ci);
    memset(ci, 0, sizeof *ci);
    ci->patterns = pats->head;
    ci->body = body;
    ci->fallthrough = fallthrough;
    return ci;
}
