#!/usr/bin/env python3
"""Random differential testing of yacc against bison -y and byacc.

usage: randgram.py YACC SEED COUNT [--keep DIR]

Each round generates a random grammar (literals, named tokens,
precedence declarations, %prec, mid-rule actions, error rules), builds
the parser with each implementation, and runs it on sentences of the
grammar, mutated sentences and random strings.  The actions print the
rule numbers and computed values, so the output shows the sequence of
reductions, the semantic values, the calls of yyerror and the result
of yyparse.  Conflict counts are compared with bison's report.
"""
import os, random, re, subprocess, sys, tempfile, shutil

YACC = os.path.abspath(sys.argv[1])
SEED = int(sys.argv[2])
COUNT = int(sys.argv[3])
VERBOSE = '-v' in sys.argv
KEEP = sys.argv[sys.argv.index('--keep') + 1] if '--keep' in sys.argv else None

LITS = list('abcde+-*')
NAMED = ['TA', 'TB', 'TC']
NAMEDCH = {'TA': 'A', 'TB': 'B', 'TC': 'C'}

def gen_grammar(rng):
    nnt = rng.randint(1, 6)
    nts = ['s%d' % i for i in range(nnt)]
    use_err = rng.random() < 0.35
    terms = rng.sample(LITS, rng.randint(2, len(LITS))) + rng.sample(NAMED, rng.randint(0, 3))
    rules = []   # (lhs, [items]) item: ('sym', name) | ('mid',)
    for nt in nts:
        for _ in range(rng.randint(1, 4)):
            n = rng.choice([0, 1, 1, 2, 2, 3, 3, 4])
            body = []
            for _ in range(n):
                r = rng.random()
                if r < 0.45:
                    body.append(('sym', rng.choice(nts)))
                elif r < 0.93:
                    body.append(('sym', rng.choice(terms)))
                else:
                    body.append(('mid',))
            rules.append((nt, body))
    if use_err:
        for _ in range(rng.randint(1, 2)):
            nt = rng.choice(nts)
            body = [('sym', 'error')] + ([('sym', rng.choice(terms))] if rng.random() < 0.8 else [])
            rules.append((nt, body))
    # productivity of s0
    prod = set()
    changed = True
    while changed:
        changed = False
        for lhs, body in rules:
            if lhs not in prod and all(it[0] == 'mid' or not it[1].startswith('s') or it[1] in prod for it in body):
                prod.add(lhs)
                changed = True
    if 's0' not in prod or any(nt not in prod for nt in nts):
        return None
    prec = []
    if rng.random() < 0.6:
        pt = [t for t in terms if t not in ('error',)]
        rng.shuffle(pt)
        k = 0
        while k < len(pt) and rng.random() < 0.7:
            m = rng.randint(1, 2)
            prec.append((rng.choice(['%left', '%right', '%nonassoc']), pt[k:k + m]))
            k += m
    precs = {t for _, ts in prec for t in ts}
    return {'nts': nts, 'terms': terms, 'rules': rules, 'prec': prec, 'precs': precs,
            'use_yyerrok': rng.random() < 0.5, 'rng_prec': [rng.random() for _ in rules],
            'prec_choice': [rng.choice(sorted(precs)) if precs else None for _ in rules]}

def sym_text(s):
    if s in NAMED or s.startswith('s') or s == 'error':
        return s
    return "'%s'" % s

def render(g):
    out = ['%{', '#include <stdio.h>', '#include <stdlib.h>',
           'void yyerror(const char *s);', 'int yylex(void);', '%}']
    named = [t for t in g['terms'] if t in NAMED]
    if named:
        out.append('%token ' + ' '.join(named))
    for kind, ts in g['prec']:
        out.append(kind + ' ' + ' '.join(sym_text(t) for t in ts))
    out.append('%%')
    out.append('top : s0 { printf("=%d\\n", $1); $$ = 0; } ;')
    mid = 0
    for i, (lhs, body) in enumerate(g['rules']):
        parts = []
        pos = 0
        terms_v = []
        for it in body:
            if it[0] == 'mid':
                mid += 1
                pos += 1
                parts.append('{ printf("m%d ", %d); $$ = %d; }' % (mid, mid, mid * 7))
                terms_v.append(pos)
            else:
                pos += 1
                parts.append(sym_text(it[1]))
                if it[1] != 'error':
                    terms_v.append(pos)
        expr = ' + '.join(['%d' % (i + 1)] + ['$%d * %d' % (p, (k % 5) + 2) for k, p in enumerate(terms_v)])
        act = '{ $$ = (%s) %% 10007; printf("r%d ", %d);%s }' % (expr, i, i,
              ' yyerrok;' if body and body[0] == ('sym', 'error') and g['use_yyerrok'] else '')
        precpart = ''
        if g['prec_choice'][i] and g['rng_prec'][i] < 0.15:
            precpart = ' %prec ' + sym_text(g['prec_choice'][i])
        out.append('%s : %s%s %s ;' % (lhs, ' '.join(parts), precpart, act))
    out.append('%%')
    lexer = ['int yylex(void) {', '  int c;', '  do c = getchar(); while (c == \' \' || c == \'\\n\');',
             '  if (c == EOF) return 0;', '  yylval = c;']
    for t in named:
        lexer.append("  if (c == '%s') return %s;" % (NAMEDCH[t], t))
    lexer.append('  return c;')
    lexer.append('}')
    out += lexer
    out.append('void yyerror(const char *s) { printf("E:%s ", s); }')
    out.append('int main(void) { int r = yyparse(); printf(" ->%d\\n", r); return 0; }')
    return '\n'.join(out) + '\n'

def tokch(t):
    return NAMEDCH.get(t, t)

def derive(g, rng, sym, depth):
    if sym in g['nts']:
        alts = [b for l, b in g['rules'] if l == sym and not any(it == ('sym', 'error') for it in b)]
        if not alts:
            alts = [b for l, b in g['rules'] if l == sym]
        if depth > 7:
            alts = sorted(alts, key=lambda b: sum(1 for it in b if it[0] == 'sym' and it[1] in g['nts']))[:1]
        b = rng.choice(alts)
        s = ''
        for it in b:
            if it[0] == 'sym':
                if it[1] == 'error':
                    s += rng.choice('abcde+-*')
                else:
                    s += derive(g, rng, it[1], depth + 1)
        return s
    return tokch(sym)

def inputs(g, rng):
    res = set()
    alphabet = [tokch(t) for t in g['terms']] + ['x']
    for _ in range(25):
        try:
            s = derive(g, rng, 's0', 0)
        except RecursionError:
            continue
        if len(s) > 60:
            continue
        res.add(s)
        for _ in range(2):
            m = list(s)
            op = rng.randint(0, 2)
            pos = rng.randint(0, len(m))
            if op == 0:
                m.insert(pos, rng.choice(alphabet))
            elif op == 1 and m:
                del m[min(pos, len(m) - 1)]
            elif m:
                m[min(pos, len(m) - 1)] = rng.choice(alphabet)
            res.add(''.join(m))
    for _ in range(8):
        res.add(''.join(rng.choice(alphabet) for _ in range(rng.randint(0, 8))))
    return sorted(res)

def build(cmd, d, name, env=None):
    r = subprocess.run(cmd + ['g.y'], cwd=d, capture_output=True, text=True,
                       env=dict(os.environ, **(env or {})))
    if r.returncode != 0:
        return None, r.stderr
    c = subprocess.run(['gcc', '-w', '-o', name, 'y.tab.c'], cwd=d, capture_output=True, text=True)
    if c.returncode != 0:
        return None, c.stderr
    return os.path.join(d, name), r.stderr

def conflicts(stderr, kind):
    m = re.search(r'(\d+) shift/reduce', stderr)
    sr = int(m.group(1)) if m else 0
    m = re.search(r'(\d+) reduce/reduce', stderr)
    rr = int(m.group(1)) if m else 0
    return sr, rr

def run(exe, inp):
    try:
        r = subprocess.run([exe], input=inp, capture_output=True, text=True, timeout=2)
        out = r.stdout + ('' if r.returncode == 0 else ' [status %d]' % r.returncode)
        if 'E:yacc stack overflow' in out or 'E:memory exhausted' in out:
            return 'STACK OVERFLOW'   # the depth differs by one between implementations
        return out
    except subprocess.TimeoutExpired:
        return 'TIMEOUT'

rng = random.Random(SEED)
stats = {'grammars': 0, 'runs': 0, 'diff_bison': 0, 'diff_bison_errorrules': 0, 'diff_byacc': 0, 'byacc_accept_quirk': 0, 'conf_diff': 0, 'skipped': 0, 'loops': 0}
d = tempfile.mkdtemp(prefix='ygram.')
try:
    rounds = 0
    while rounds < COUNT:
        g = gen_grammar(rng)
        if not g:
            continue
        rounds += 1
        text = render(g)
        open(os.path.join(d, 'g.y'), 'w').write(text)
        mine, merr = build([YACC], d, 'mine')
        bis, berr = build(['bison', '-y', '-Wno-other', '-Wno-conflicts-sr', '-Wno-conflicts-rr', '-Wno-yacc', '-Wno-empty-rule', '-Wno-midrule-values', '-Wno-precedence'], d, 'bis')
        _, berr2 = None, subprocess.run(['bison', '-y', 'g.y', '-o', '/dev/null'], cwd=d, capture_output=True, text=True).stderr
        bya, yerr = build(['byacc'], d, 'bya')
        mineb, _ = build([YACC], d, 'mineb', {'YACC_TEST_DEFAULTS': 'byacc'})
        if not mine:
            print('MINE FAILED seed round', rounds, merr)
            if KEEP:
                shutil.copy(os.path.join(d, 'g.y'), os.path.join(KEEP, 'fail%d.y' % rounds))
            stats['skipped'] += 1
            continue
        if not bis:
            stats['skipped'] += 1
            continue
        stats['grammars'] += 1
        if rounds % 20 == 0:
            print('progress', rounds, stats, flush=True)
        if conflicts(merr, 0) != conflicts(berr2, 0):
            stats['conf_diff'] += 1
            print('conflicts differ (round %d): mine %s bison %s' % (rounds, conflicts(merr, 0), conflicts(berr2, 0)))
            if KEEP:
                shutil.copy(os.path.join(d, 'g.y'), os.path.join(KEEP, 'conf%d.y' % rounds))
        uses_error = 'error' in text.split('%%')[1]
        for inp in inputs(g, rng):
            stats['runs'] += 1
            a = run(mine, inp)
            b = run(bis, inp)
            if a in ('TIMEOUT', 'STACK OVERFLOW') and a == b:
                stats['loops'] += 1   # a cyclic grammar: both parsers loop
                continue
            if a != b:
                if uses_error:
                    stats['diff_bison_errorrules'] += 1
                    if VERBOSE:
                        print('diff bison, error rules (round %d) input %r\n mine:  %s\n bison: %s' % (rounds, inp, a, b))
                else:
                    stats['diff_bison'] += 1
                    print('DIFF bison (round %d) input %r\n mine:  %s\n bison: %s' % (rounds, inp, a, b))
                    if KEEP:
                        shutil.copy(os.path.join(d, 'g.y'), os.path.join(KEEP, 'diff%d.y' % rounds))
            if bya and mineb:
                a2 = run(mineb, inp)
                c = run(bya, inp)
                if a2 != c and a2.endswith(' ->0\n') and c == a2[:-5] + ' ->1\n' and 'E:' in a2:
                    # byacc accepts only right after the reduction to the start
                    # symbol, not after error recovery discards a token there
                    stats['byacc_accept_quirk'] += 1
                elif a2 != c:
                    stats['diff_byacc'] += 1
                    print('DIFF byacc (round %d) input %r\n mine:  %s\n byacc: %s' % (rounds, inp, a2, c))
                    if KEEP:
                        shutil.copy(os.path.join(d, 'g.y'), os.path.join(KEEP, 'bdiff%d.y' % rounds))
finally:
    shutil.rmtree(d, ignore_errors=True)
print(stats)
