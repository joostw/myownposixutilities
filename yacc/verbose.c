/*
 * verbose.c - the description file (y.output): the grammar, the
 * states with their items and actions, the conflicts, and a summary
 * with the table sizes.
 */
#include "yacc.h"

static int item_rule(int item)
{
    while (ritem[item] >= 0)
        item++;
    return -ritem[item] - 1;
}

static void print_item(FILE *out, int item)
{
    int r = item_rule(item);
    fprintf(out, "\t%s :", symbols[rules[r].lhs]->name);
    for (int i = rules[r].rhs; ; i++) {
        if (i == item)
            fputs(" .", out);
        if (ritem[i] < 0)
            break;
        fprintf(out, " %s", symbols[ritem[i]]->name);
    }
    fprintf(out, "  (%d)\n", r);
}

static void print_grammar(FILE *out)
{
    int prev = -1;
    for (int r = 0; r < nrules; r++) {
        int len;
        if (rules[r].lhs != prev) {
            if (r)
                fputc('\n', out);
            fprintf(out, "%4d  %s :", r, symbols[rules[r].lhs]->name);
            len = (int)strlen(symbols[rules[r].lhs]->name);
        } else {
            len = (int)strlen(symbols[rules[r].lhs]->name);
            fprintf(out, "%4d  %*s |", r, len, "");
        }
        prev = rules[r].lhs;
        if (rules[r].len == 0)
            fputs(" /* empty */", out);
        for (int i = rules[r].rhs; ritem[i] >= 0; i++)
            fprintf(out, " %s", symbols[ritem[i]]->name);
        fputc('\n', out);
    }
    fputc('\n', out);
}

void write_verbose(FILE *out)
{
    int nunused = 0;
    print_grammar(out);
    for (int r = 1; r < nrules; r++)
        if (!rules[r].used) {
            if (!nunused++)
                fputs("Rules never reduced:\n", out);
            fprintf(out, "\t(%d)  ", r);
            fprintf(out, "%s :", symbols[rules[r].lhs]->name);
            for (int i = rules[r].rhs; ritem[i] >= 0; i++)
                fprintf(out, " %s", symbols[ritem[i]]->name);
            fputc('\n', out);
        }
    if (nunused)
        fputc('\n', out);

    {
        int ci = 0, ri = 0;
        for (int s = 0; s < nstates; s++) {
            State *st = &states[s];
            Action *row = actions[s];
            int any_explicit_error = 0;
            /* conflicts first, as in historical listings */
            for (; ci < nconflicts && conflicts[ci].state < s; ci++)
                ;
            for (int k = ci; k < nconflicts && conflicts[k].state == s; k++) {
                Conflict *c = &conflicts[k];
                if (c->kind == 0)
                    fprintf(out, "%d: shift/reduce conflict (shift %d, reduce %d) on %s\n", s, c->shift_to,
                            c->rule2, symbols[c->token]->name);
                else
                    fprintf(out, "%d: reduce/reduce conflict (reduce %d, reduce %d) on %s\n", s, c->rule1,
                            c->rule2, symbols[c->token]->name);
            }
            fprintf(out, "state %d\n", s);
            for (int i = 0; i < st->nkernel; i++)
                print_item(out, st->kernel[i]);
            fputc('\n', out);
            for (int t = 0; t < ntokens; t++) {
                switch (row[t].kind) {
                case A_SHIFT:
                    fprintf(out, "\t%s  shift %d\n", symbols[t]->name, row[t].arg);
                    break;
                case A_ACCEPT:
                    fprintf(out, "\t%s  accept\n", symbols[t]->name);
                    break;
                case A_REDUCE:
                    fprintf(out, "\t%s  reduce %d\n", symbols[t]->name, row[t].arg);
                    break;
                case A_ERR:
                    fprintf(out, "\t%s  error (nonassociative)\n", symbols[t]->name);
                    any_explicit_error = 1;
                    break;
                }
            }
            (void)any_explicit_error;
            if (st->nreds == 1 && st->red_rule[0] == 0)
                fputs("\t.  accept\n", out);
            else if (defred[s] >= 0)
                fprintf(out, "\t.  reduce %d\n", defred[s]);
            else
                fputs("\t.  error\n", out);
            {
                int first = 1;
                for (int j = 0; j < st->nshifts; j++)
                    if (st->shift_sym[j] >= ntokens) {
                        if (first)
                            fputc('\n', out);
                        first = 0;
                        fprintf(out, "\t%s  goto %d\n", symbols[st->shift_sym[j]]->name, st->shift_to[j]);
                    }
            }
            for (; ri < nresolutions && resolutions[ri].state < s; ri++)
                ;
            if (ri < nresolutions && resolutions[ri].state == s) {
                fputc('\n', out);
                for (; ri < nresolutions && resolutions[ri].state == s; ri++) {
                    Resolution *r = &resolutions[ri];
                    fprintf(out, "\tconflict between rule %d and token %s resolved as %s\n", r->rule,
                            symbols[r->token]->name,
                            r->how == A_SHIFT ? "shift" : r->how == A_REDUCE ? "reduce" : "an error");
                }
            }
            fputs("\n\n", out);
        }
    }

    if (total_sr || total_rr) {
        int nst = 0;
        for (int s = 0; s < nstates; s++)
            if (state_sr[s] || state_rr[s])
                nst++;
        fprintf(out, "%d shift/reduce conflict%s, %d reduce/reduce conflict%s in %d state%s\n", total_sr,
                total_sr == 1 ? "" : "s", total_rr, total_rr == 1 ? "" : "s", nst, nst == 1 ? "" : "s");
        for (int s = 0; s < nstates; s++)
            if (state_sr[s] || state_rr[s])
                fprintf(out, "  state %d: %d shift/reduce, %d reduce/reduce\n", s, state_sr[s], state_rr[s]);
        fputc('\n', out);
    }

    {
        int nact = 0, ngoto = 0;
        for (int s = 0; s < nstates; s++) {
            for (int t = 0; t < ntokens; t++)
                if (actions[s][t].kind != A_NONE)
                    nact++;
            if (defred[s] >= 0)
                nact++;
            for (int j = 0; j < states[s].nshifts; j++)
                if (states[s].shift_sym[j] >= ntokens)
                    ngoto++;
        }
        fprintf(out, "%d terminals, %d nonterminals\n", ntokens, nnonterms);
        fprintf(out, "%d grammar rules, %d states\n", nrules, nstates);
        fprintf(out, "%d grammar items, %d parser actions, %d goto entries\n", nitems, nact, ngoto);
        fputs("Limits: none; all tables are allocated dynamically, so the number of\n"
              "terminals, nonterminals, rules, states, rule symbols and actions is\n"
              "limited only by available memory.\n\n", out);
    }

    fputs("symbol  value  name\n", out);
    for (int i = 0; i < nsyms; i++) {
        if (i < ntokens && i != 2)
            fprintf(out, "%6d %6d  %s\n", i, symbols[i]->value, symbols[i]->name);
        else
            fprintf(out, "%6d      -  %s%s\n", i, symbols[i]->name, symbols[i]->tag ? "" : "");
    }
}

void write_verbose_error(FILE *out)
{
    fprintf(out, "%d error%s in the grammar %s; no parser was generated.\n", nerrors, nerrors == 1 ? "" : "s",
            input_name);
    fputs("Limits: none; all tables are allocated dynamically.\n", out);
}
