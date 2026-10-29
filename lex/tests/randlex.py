#!/usr/bin/env python3
"""Random differential testing of lex against flex -X.

usage: randlex.py LEX SEED COUNT [--keep DIR] [-v]

Each round makes a random lex source: patterns built from characters,
escapes, quoted strings, bracket expressions, '.', grouping,
alternation, * + ? and intervals, with ^, $, trailing context and start
conditions (inclusive and exclusive), and actions that print the rule
and yytext and sometimes use BEGIN, REJECT, yymore, yyless, unput or
input.  Both scanners run on random inputs and on texts made from the
patterns; the outputs must be the same.

Trailing context is generated only where the split of the match is
determined (a fixed-length part, or r and x over disjoint characters),
since the standard leaves the other cases unspecified.
"""
import os, random, subprocess, sys, tempfile, shutil

lex = os.path.abspath(sys.argv[1])
rng = random.Random(int(sys.argv[2]))
count = int(sys.argv[3])
keep = sys.argv[sys.argv.index('--keep') + 1] if '--keep' in sys.argv else None
verbose = '-v' in sys.argv
libdir = os.path.dirname(lex)

class Pat:
    def __init__(self, text, sample, fixed, nullable, simple=True):
        self.text, self.sample, self.fixed, self.nullable = text, sample, fixed, nullable
        # flex recognizes the fixed length (no alternation or repetition)
        self.simple = simple and fixed is not None

def lit(chars):
    c = rng.choice(chars)
    # (not \x0a: a hex escape takes all the hex digits that follow, as the
    # standard says, where flex stops after two)
    forms = {'\n': ['\\n', '"\\n"', '\\012'], ' ': ['" "', '\\ ', '[ ]']}
    if c in forms:
        t = rng.choice(forms[c])
    else:
        t = rng.choice([c, c, '"%s"' % c, '\\%s' % c if c not in 'abfnrtvx0123456789' else c, '[%s]' % c])
    return Pat(t, lambda c=c: c, 1, False)

RESTRICTED = False   # only the given characters: no '.' or [^...]

def bracket(chars):
    k = rng.randint(1, len(chars))
    s = rng.sample(chars, k)
    neg = rng.random() < 0.25 and not RESTRICTED
    body = ''.join('\\n' if c == '\n' else c for c in s)
    if neg:
        allc = [c for c in 'abcx\n' if c not in s] or ['x']
        return Pat('[^%s]' % body, lambda: rng.choice(allc), 1, False)
    return Pat('[%s]' % body, lambda: rng.choice(s), 1, False)

def gen(chars, depth):
    r = rng.random()
    if depth > 3 or r < 0.3:
        return lit(chars)
    if r < 0.4:
        return bracket(chars)
    if r < 0.45 and '\n' not in chars and not RESTRICTED:
        nonl = [c for c in chars] or ['a']
        return Pat('.', lambda: rng.choice(nonl), 1, False)
    if r < 0.55:
        s = ''.join(rng.choice([c for c in chars if c != '\n'] or ['a']) for _ in range(rng.randint(1, 3)))
        return Pat('"%s"' % s, lambda: s, len(s), False)
    if r < 0.7:
        a, b = gen(chars, depth + 1), gen(chars, depth + 1)
        # an interval applies to the whole concatenation before it: group b
        bt = '(' + b.text + ')' if '{' in b.text else b.text
        return Pat(a.text + bt,
                   lambda: a.sample() + b.sample(),
                   a.fixed + b.fixed if a.fixed is not None and b.fixed is not None else None,
                   a.nullable and b.nullable, a.simple and b.simple)
    if r < 0.8:
        a, b = gen(chars, depth + 1), gen(chars, depth + 1)
        return Pat('(%s|%s)' % (a.text, b.text), lambda: rng.choice([a.sample, b.sample])(),
                   a.fixed if a.fixed == b.fixed else None, a.nullable or b.nullable, False)
    a = gen(chars, depth + 1)
    op = rng.choice(['*', '+', '?', '{2}', '{1,2}', '{2,}'])
    inner = '(' + a.text + ')'
    if op == '*':
        return Pat(inner + '*', lambda: ''.join(a.sample() for _ in range(rng.randint(0, 3))), None, True)
    if op == '+':
        return Pat(inner + '+', lambda: ''.join(a.sample() for _ in range(rng.randint(1, 3))), None, a.nullable)
    if op == '?':
        return Pat(inner + '?', lambda: rng.choice(['', a.sample()]), None, True)
    if op == '{2}':
        return Pat(inner + '{2}', lambda: a.sample() + a.sample(), a.fixed * 2 if a.fixed is not None else None,
                   a.nullable, False)
    if op == '{1,2}':
        return Pat(inner + '{1,2}', lambda: ''.join(a.sample() for _ in range(rng.randint(1, 2))), None, a.nullable)
    return Pat(inner + '{2,}', lambda: ''.join(a.sample() for _ in range(rng.randint(2, 3))), None, a.nullable)

def make_source():
    nrules = rng.randint(1, 7)
    conds = []
    if rng.random() < 0.4:
        conds = [('S%d' % i, rng.choice(['%s', '%x'])) for i in range(rng.randint(1, 2))]
    feature = rng.choice(['none', 'none', 'reject', 'yymore', 'yyless', 'unput', 'input', 'begin'])
    if conds:
        feature = rng.choice([feature, 'begin'])
    lines = ['%{', '#include <stdio.h>', '#include <string.h>', '%}']
    for name, kind in conds:
        lines.append('%s %s' % (kind, name))
    if rng.random() < 0.2:
        lines.append('%array')
    lines.append('%%')
    samples = []
    for i in range(nrules):
        chars = ['a', 'b', 'c'] + (['\n'] if rng.random() < 0.2 else [])
        # (no trailing context with REJECT, nor with yymore: flex drops the
        # yymore text when the next match has trailing context)
        tc = rng.random() < 0.25 and feature not in ('reject', 'yymore')
        bol = rng.random() < 0.15
        if tc:
            kind = rng.choice(['xfixed', 'rfixed', 'disjoint', 'dollar'])
            if kind == 'disjoint':
                global RESTRICTED
                RESTRICTED = True
                r = gen(['a', 'b'], 1)
                x = gen(['c'], 2)
                RESTRICTED = False
            else:
                r = gen(chars, 1)
                x = gen(chars, 2) if kind != 'dollar' else None
            if r.nullable or (x is not None and x.nullable):
                tc = False
            elif kind == 'xfixed' and not x.simple:
                tc = False
            elif kind == 'rfixed' and not r.simple:
                tc = False
        if tc:
            pat = r.text + ('$' if kind == 'dollar' else '/' + x.text)
            sample = (lambda r=r, x=x: r.sample() + (x.sample() if x else '\n'))
        else:
            p = gen(chars, 0)
            pat, sample = p.text, p.sample
        if bol:
            pat = '^' + pat
        if conds and rng.random() < 0.5:
            sel = rng.sample([c[0] for c in conds] + ['INITIAL'], rng.randint(1, 2))
            pat = '<%s>' % ','.join(sel) + pat
        act = 'printf("<%d:%%s>", yytext);' % i
        if feature == 'reject' and rng.random() < 0.5:
            act += ' REJECT;'
        elif feature == 'yymore' and rng.random() < 0.4:
            act += ' yymore();'
        elif feature == 'yyless' and rng.random() < 0.5:
            act = 'if (yyleng > 1) { yyless(yyleng - 1); printf("(less)"); } ' + act
        elif feature == 'unput' and rng.random() < 0.4:
            act = 'if (yyleng >= 2) { char c0 = yytext[0]; ' + act + ' unput(c0); } else ' + act
        elif feature == 'input' and rng.random() < 0.4:
            act += ' { int c = input(); printf("(%d)", c); }'
        if feature == 'begin' and conds and rng.random() < 0.6:
            act += ' BEGIN %s;' % rng.choice([c[0] for c in conds] + ['INITIAL'])
        lines.append('%s\t{ %s }' % (pat, act))
        samples.append(sample)
    if rng.random() < 0.5:
        lines.append('.|\\n\tprintf("{%s}", yytext[0] == \'\\n\' ? "NL" : yytext);')
    return '\n'.join(lines) + '\n', samples

def build(cmd, d, exe):
    r = subprocess.run(cmd + ['t.l'], cwd=d, capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr
    c = subprocess.run(['gcc', '-w'] + os.environ.get('SCANFLAGS', '').split() + ['-o', exe, 'lex.yy.c', '-L' + libdir, '-ll'], cwd=d,
                       capture_output=True, text=True)
    if c.returncode:
        return None, c.stderr
    return os.path.join(d, exe), r.stderr

def run(exe, inp):
    try:
        r = subprocess.run([exe], input=inp.encode(), capture_output=True, timeout=5)
        out = r.stdout.decode('latin-1')
        if len(out) > 20000:
            out = out[:20000] + '...'
        return out + ('' if r.returncode == 0 else '[status %d]' % r.returncode)
    except subprocess.TimeoutExpired:
        return 'TIMEOUT'

stats = dict(rounds=0, runs=0, diffs=0, flex_rejects=0, mine_rejects=0, loops=0)
d = tempfile.mkdtemp(prefix='randlex.')
try:
    for rnd in range(1, count + 1):
        src, samples = make_source()
        open(os.path.join(d, 't.l'), 'w').write(src)
        mine, merr = build([lex], d, 'mine')
        ref, ferr = build(['flex', '-X'], d, 'ref')
        if not ref:
            stats['flex_rejects'] += 1
            continue
        if not mine:
            stats['mine_rejects'] += 1
            print('MINE REJECTS (round %d): %s' % (rnd, merr.strip()))
            if keep:
                open(os.path.join(keep, 'rej%d.l' % rnd), 'w').write(src)
            continue
        stats['rounds'] += 1
        inputs = set()
        for _ in range(12):
            inputs.add(''.join(rng.choice('abcx\n') for _ in range(rng.randint(0, 25))))
        for _ in range(12):
            inputs.add(''.join(rng.choice(samples)() for _ in range(rng.randint(1, 6))))
        for inp in sorted(inputs):
            stats['runs'] += 1
            a, b = run(mine, inp), run(ref, inp)
            if a == 'TIMEOUT' and b == 'TIMEOUT':
                stats['loops'] += 1
                continue
            if a != b:
                stats['diffs'] += 1
                print('DIFF (round %d) input %r\n mine: %r\n flex: %r' % (rnd, inp, a[:300], b[:300]))
                if verbose:
                    print(src)
                if keep:
                    open(os.path.join(keep, 'diff%d.l' % rnd), 'w').write(src)
                break
        if rnd % 50 == 0:
            print('progress', rnd, stats, flush=True)
finally:
    shutil.rmtree(d, ignore_errors=True)
print(stats)
sys.exit(1 if stats['diffs'] or stats['mine_rejects'] else 0)
