/*
 * tables.c - the parser actions of each state: shift, reduce, accept or
 * error per terminal, with conflicts resolved by precedence and
 * associativity or by the default rules (shift; the earlier rule), and
 * the default reduction of each state.
 */
#include "yacc.h"

Action **actions;
int *defred;
int *consistent;
Conflict *conflicts;
int nconflicts;
Resolution *resolutions;
int nresolutions;
int *state_sr, *state_rr;
int total_sr, total_rr;
int final_state = -1;

static int cconflicts, cresolutions;

static void add_conflict(int s, int t, int kind, int rule1, int rule2, int shift_to)
{
    if (nconflicts == cconflicts) {
        cconflicts = cconflicts ? cconflicts * 2 : 32;
        conflicts = xrealloc(conflicts, (size_t)cconflicts * sizeof *conflicts);
    }
    conflicts[nconflicts].state = s;
    conflicts[nconflicts].token = t;
    conflicts[nconflicts].kind = kind;
    conflicts[nconflicts].rule1 = rule1;
    conflicts[nconflicts].rule2 = rule2;
    conflicts[nconflicts].shift_to = shift_to;
    nconflicts++;
}

static void add_resolution(int s, int t, int rule, int how)
{
    if (nresolutions == cresolutions) {
        cresolutions = cresolutions ? cresolutions * 2 : 32;
        resolutions = xrealloc(resolutions, (size_t)cresolutions * sizeof *resolutions);
    }
    resolutions[nresolutions].state = s;
    resolutions[nresolutions].token = t;
    resolutions[nresolutions].rule = rule;
    resolutions[nresolutions].how = how;
    nresolutions++;
}

/* does the state contain the completed item of rule 0 ($accept: start $end .)? */
static int is_accepting(int s)
{
    State *st = &states[s];
    for (int i = 0; i < st->nkernel; i++)
        if (st->kernel[i] == 2)
            return 1;
    return 0;
}

/*
 * Conflict resolution can remove every shift into a state; such states
 * are dropped (with their conflicts) and the others renumbered.
 */
static void remove_unreachable(void)
{
    int *map = xmalloc((size_t)nstates * sizeof(int));
    int *stack = xmalloc((size_t)nstates * sizeof(int));
    char *seen = xcalloc((size_t)nstates, 1);
    int sp = 0, n = 0, k, nkeep;

    seen[0] = 1;
    stack[sp++] = 0;
    while (sp) {
        int s = stack[--sp];
        for (int j = 0; j < states[s].nshifts; j++) {
            int sym = states[s].shift_sym[j], to = states[s].shift_to[j];
            if (sym < ntokens && actions[s][sym].kind != A_SHIFT && actions[s][sym].kind != A_ACCEPT)
                continue;
            if (!seen[to]) {
                seen[to] = 1;
                stack[sp++] = to;
            }
        }
    }
    for (int s = 0; s < nstates; s++)
        map[s] = seen[s] ? n++ : -1;
    nkeep = n;
    if (nkeep < nstates) {
        for (int s = 0; s < nstates; s++) {
            int d = map[s];
            if (d < 0) {
                free(actions[s]);
                continue;
            }
            State *st = &states[s];
            int m = 0;
            /* drop the transitions resolved away (into removed states) */
            for (int j = 0; j < st->nshifts; j++)
                if (map[st->shift_to[j]] >= 0) {
                    st->shift_sym[m] = st->shift_sym[j];
                    st->shift_to[m] = map[st->shift_to[j]];
                    m++;
                }
            st->nshifts = m;
            for (int t = 0; t < ntokens; t++)
                if (actions[s][t].kind == A_SHIFT || actions[s][t].kind == A_ACCEPT)
                    actions[s][t].arg = map[actions[s][t].arg];
            states[d] = *st;
            actions[d] = actions[s];
            defred[d] = defred[s];
            consistent[d] = consistent[s];
            state_sr[d] = state_sr[s];
            state_rr[d] = state_rr[s];
        }
        for (k = 0, n = 0; k < nconflicts; k++)
            if (map[conflicts[k].state] >= 0) {
                conflicts[n] = conflicts[k];
                conflicts[n].state = map[conflicts[k].state];
                if (conflicts[n].kind == 0)
                    conflicts[n].shift_to = map[conflicts[n].shift_to];
                n++;
            }
        nconflicts = n;
        for (k = 0, n = 0; k < nresolutions; k++)
            if (map[resolutions[k].state] >= 0) {
                resolutions[n] = resolutions[k];
                resolutions[n].state = map[resolutions[k].state];
                n++;
            }
        nresolutions = n;
        if (final_state >= 0)
            final_state = map[final_state];
        nstates = nkeep;
    }
    total_sr = total_rr = 0;
    for (int s = 0; s < nstates; s++) {
        total_sr += state_sr[s];
        total_rr += state_rr[s];
    }
    free(map);
    free(stack);
    free(seen);
}

void build_actions(void)
{
    size_t tw = NWORDS(ntokens);
    Word *errset = xcalloc(tw, sizeof(Word));
    int *count = xcalloc((size_t)nrules, sizeof(int));
    const char *pol = getenv("YACC_TEST_DEFAULTS");
    int byacc_defaults = pol && !strcmp(pol, "byacc");

    actions = xmalloc((size_t)nstates * sizeof *actions);
    defred = xmalloc((size_t)nstates * sizeof *defred);
    consistent = xcalloc((size_t)nstates, sizeof *consistent);
    state_sr = xcalloc((size_t)nstates, sizeof *state_sr);
    state_rr = xcalloc((size_t)nstates, sizeof *state_rr);

    for (int s = 0; s < nstates; s++) {
        State *st = &states[s];
        Action *row = xcalloc((size_t)ntokens, sizeof *row);
        int shifts_error = 0, token_shifts = 0;
        Word **la = st->la;
        actions[s] = row;
        memset(errset, 0, tw * sizeof(Word));

        for (int j = 0; j < st->nshifts; j++) {
            int t = st->shift_sym[j];
            if (t >= ntokens)
                break;
            token_shifts++;
            if (t == 1)
                shifts_error = 1;
            if (t == 0 && is_accepting(st->shift_to[j])) {
                row[t].kind = A_ACCEPT;
                final_state = s;
            } else
                row[t].kind = A_SHIFT;
            row[t].arg = st->shift_to[j];
        }
        /* the final reduction of $accept is never used: the parser accepts on $end */
        if (st->nreds == 1 && st->red_rule[0] == 0) {
            defred[s] = -1;
            consistent[s] = 1;
            continue;
        }

        if (byacc_defaults) {
            /* testing only: byacc resolves a token with a shift and several
               reductions, one of them by precedence, differently */
            for (int t = 0; t < ntokens; t++) {
                int nr = 0, withprec = 0;
                if (row[t].kind != A_SHIFT || !symbols[t]->prec)
                    continue;
                for (int k = 0; k < st->nreds; k++)
                    if (BIT_TEST(la[k], t)) {
                        nr++;
                        withprec |= rules[st->red_rule[k]].prec != 0;
                    }
                if (nr > 1 && withprec) {
                    fprintf(stderr, "%s: test: resolution differs from byacc\n", progname);
                    exit(3);
                }
            }
        }

        /* precedence and associativity */
        for (int k = 0; k < st->nreds; k++) {
            int r = st->red_rule[k];
            if (!rules[r].prec)
                continue;
            for (int t = 0; t < ntokens; t++) {
                int tp;
                if (!BIT_TEST(la[k], t) || row[t].kind != A_SHIFT)
                    continue;
                tp = symbols[t]->prec;
                if (!tp)
                    continue;
                if (tp < rules[r].prec || (tp == rules[r].prec && symbols[t]->assoc == LEFT)) {
                    row[t].kind = A_NONE;
                    add_resolution(s, t, r, A_REDUCE);
                } else if (tp > rules[r].prec || symbols[t]->assoc == RIGHT) {
                    BIT_CLR(la[k], t);
                    add_resolution(s, t, r, A_SHIFT);
                } else {
                    /* nonassociative: an error */
                    row[t].kind = A_NONE;
                    BIT_CLR(la[k], t);
                    BIT_SET(errset, t);
                    add_resolution(s, t, r, A_ERR);
                }
            }
        }

        /* the remaining conflicts: shift wins, then the earliest rule */
        for (int t = 0; t < ntokens; t++) {
            int first = -1, nr = 0;
            for (int k = 0; k < st->nreds; k++)
                if (BIT_TEST(la[k], t)) {
                    if (first < 0)
                        first = st->red_rule[k];
                    else {
                        add_conflict(s, t, 1, first, st->red_rule[k], -1);
                        state_rr[s]++;
                    }
                    nr++;
                }
            if (nr && (row[t].kind == A_SHIFT || row[t].kind == A_ACCEPT)) {
                for (int k = 0; k < st->nreds; k++)
                    if (BIT_TEST(la[k], t))
                        add_conflict(s, t, 0, -1, st->red_rule[k], row[t].arg);
                state_sr[s]++;
            } else if (nr) {
                row[t].kind = A_REDUCE;
                row[t].arg = first;
            }
            if (BIT_TEST(errset, t)) {
                row[t].kind = A_ERR;
                row[t].arg = 0;
            }
        }
        total_sr += state_sr[s];
        total_rr += state_rr[s];

        /* the default reduction */
        defred[s] = -1;
        if (byacc_defaults) {
            /* testing only: byacc's policy, a default only for a sole reduction */
            int rule = -1, ok = 1, nonerror = 0;
            for (int t = 0; t < ntokens && ok; t++) {
                if (row[t].kind == A_SHIFT || row[t].kind == A_ACCEPT)
                    ok = 0;
                else if (row[t].kind == A_REDUCE) {
                    if (rule >= 0 && row[t].arg != rule)
                        ok = 0;
                    rule = row[t].arg;
                    nonerror |= t != 1;
                }
            }
            if (ok && rule >= 0 && nonerror) {
                defred[s] = rule;
                for (int t = 0; t < ntokens; t++)
                    row[t].kind = A_NONE;
            }
        } else if (st->nreds >= 1 && !shifts_error) {
            if (st->nreds == 1 && !token_shifts)
                defred[s] = st->red_rule[0];
            else {
                int max = 0;
                for (int k = 0; k < st->nreds; k++)
                    count[st->red_rule[k]] = 0;
                for (int t = 0; t < ntokens; t++)
                    if (row[t].kind == A_REDUCE)
                        count[row[t].arg]++;
                for (int k = 0; k < st->nreds; k++) {
                    int r = st->red_rule[k];
                    if (count[r] > max) {
                        max = count[r];
                        defred[s] = r;
                    }
                }
            }
        }
        if (defred[s] >= 0) {
            for (int t = 0; t < ntokens; t++)
                if (row[t].kind == A_REDUCE && row[t].arg == defred[s])
                    row[t].kind = A_NONE;
        } else {
            /* without a default reduction, an explicit error is the same as none */
            for (int t = 0; t < ntokens; t++)
                if (row[t].kind == A_ERR)
                    row[t].kind = A_NONE;
        }
        /* explicit errors from %nonassoc stay: without them a state that
           only reduces would never detect the nonassociative use */
        consistent[s] = 1;
        for (int t = 0; t < ntokens; t++)
            if (row[t].kind != A_NONE) {
                consistent[s] = 0;
                break;
            }
    }

    remove_unreachable();

    /* rules never reduced */
    for (int s = 0; s < nstates; s++) {
        if (defred[s] >= 0)
            rules[defred[s]].used = 1;
        for (int t = 0; t < ntokens; t++)
            if (actions[s][t].kind == A_REDUCE)
                rules[actions[s][t].arg].used = 1;
    }
    rules[0].used = 1;
    free(errset);
    free(count);
}

void report_conflicts(void)
{
    int unused = 0;
    for (int r = 1; r < nrules; r++)
        if (!rules[r].used)
            unused++;
    if (unused) {
        for (int r = 1; r < nrules; r++)
            if (!rules[r].used)
                warning_at(rules[r].line, "rule %d is never reduced", r);
    }
    if (expect_sr >= 0 && total_sr == expect_sr && total_rr == (expect_rr >= 0 ? expect_rr : 0))
        return;
    if (expect_rr >= 0 && expect_sr < 0 && total_rr == expect_rr && total_sr == 0)
        return;
    if (total_sr || total_rr) {
        fprintf(stderr, "%s: %s: conflicts: ", progname, input_name);
        if (total_sr)
            fprintf(stderr, "%d shift/reduce", total_sr);
        if (total_sr && total_rr)
            fputs(", ", stderr);
        if (total_rr)
            fprintf(stderr, "%d reduce/reduce", total_rr);
        fputc('\n', stderr);
    }
    if (expect_sr >= 0 && total_sr != expect_sr)
        warning_at(0, "expected %d shift/reduce conflict%s", expect_sr, expect_sr == 1 ? "" : "s");
    if (expect_rr >= 0 && total_rr != expect_rr)
        warning_at(0, "expected %d reduce/reduce conflict%s", expect_rr, expect_rr == 1 ? "" : "s");
}
