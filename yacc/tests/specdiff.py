#!/usr/bin/env python3
"""Compare parsers for real grammars (tests/grammars/*.y) with bison -y
and byacc (the latter with byacc's choice of default reductions).

usage: specdiff.py YACC [N]

For each grammar, the rules are read from the description file, random
sentences are derived from them (and mutated), and both parsers, built
with debugging code, are run on the token sequences.  The sequence of
rules reduced, the yyerror calls and the result must be the same.
"""
import os, random, re, subprocess, sys, tempfile, shutil

YACC = os.path.abspath(sys.argv[1])
N = int(sys.argv[2]) if len(sys.argv) > 2 else 200
here = os.path.dirname(os.path.abspath(__file__))
gdir = os.path.join(here, 'grammars')

def rules_of(output):
    rules = []
    lhs = None
    for line in output.split('\n'):
        m = re.match(r'^\s*(\d+)\s+(\S+) :(.*)$', line)
        m2 = re.match(r'^\s*(\d+)\s+\|(.*)$', line)
        if m:
            lhs = m.group(2); body = m.group(3)
        elif m2:
            body = m2.group(2)
        else:
            if line.startswith('state'):
                break
            continue
        body = body.replace('/* empty */', '').split()
        rules.append((lhs, body))
    return rules

DRIVER = r'''
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
%s
static struct { const char *n; int v; } names[] = { %s { 0, 0 } };
int yylex(void)
{
    char buf[256];
    int i;
    if (!fgets(buf, sizeof buf, stdin)) return 0;
    buf[strcspn(buf, "\n")] = 0;
    if (buf[0] == '\'') return (unsigned char)buf[1] == '\\' ? (buf[2] == 'n' ? '\n' : buf[2] == 't' ? '\t' : buf[2]) : (unsigned char)buf[1];
    for (i = 0; names[i].n; i++) if (!strcmp(names[i].n, buf)) return names[i].v;
    return 255;
}
void yyerror(const char *s) { printf("E:%%s\n", s); }
int main(void) { extern int yydebug; int r; yydebug = 1; r = yyparse(); printf("-> %%d\n", r); return 0; }
'''

def build(cmd, d, g, exe, names, env=None):
    r = subprocess.run(cmd + [g], cwd=d, capture_output=True, text=True, env=dict(os.environ, **(env or {})))
    if r.returncode:
        return None
    hdr = open(os.path.join(d, 'y.tab.h')).read()
    defined = [n for n in names if re.search(r'#define %s ' % re.escape(n), hdr)]
    src = open(os.path.join(d, 'y.tab.c')).read()
    src += DRIVER % ('', ' '.join('{ "%s", %s },' % (n, n) for n in defined))
    open(os.path.join(d, exe + '.c'), 'w').write(src)
    c = subprocess.run(['gcc', '-w', '-DYYDEBUG=1', '-o', exe, exe + '.c'], cwd=d, capture_output=True, text=True)
    if c.returncode:
        print(c.stderr[:2000])
        return None
    return os.path.join(d, exe)

def trace(exe, inp, kind):
    r = subprocess.run([exe], input=inp, capture_output=True, text=True, timeout=10)
    if kind == 'mine':
        red = re.findall(r'reducing by rule (\d+)', r.stderr)
    else:
        red = re.findall(r'Reducing stack by rule (\d+)', r.stderr)
    return ' '.join(red) + ' | ' + r.stdout.replace('\n', ' ')

total = diffs = 0
rng = random.Random(1)
for f in sorted(os.listdir(gdir)):
    if not f.endswith('.y'):
        continue
    d = tempfile.mkdtemp(prefix='yspec.')
    try:
        shutil.copy(os.path.join(gdir, f), d)
        subprocess.run([YACC, '-v', f], cwd=d, capture_output=True)
        rules = rules_of(open(os.path.join(d, 'y.output')).read())
        nts = {l for l, _ in rules}
        terms = sorted({s for _, b in rules for s in b if s not in nts and s not in ('$end',)})
        names = [t for t in terms if re.match(r'^[A-Za-z_][A-Za-z0-9_]*$', t) and t != 'error']
        mine = build([YACC, '-d', '-t'], d, f, 'mine', names)
        # a one-character string literal is the character literal here,
        # a separate token in bison
        src = open(os.path.join(d, f)).read()
        open(os.path.join(d, 'b_' + f), 'w').write(re.sub(r'"([^"\\])"', r"'\1'", src))
        bis = build(['bison', '-y', '-d', '-t', '-Wnone'], d, 'b_' + f, 'bis', names)
        mineb = build([YACC, '-d', '-t'], d, f, 'mineb', names, {'YACC_TEST_DEFAULTS': 'byacc'})
        # byacc makes a string literal "name" the same token as name
        bya = None if re.search(r'"[^"\n]{2,}"', src) else build(['byacc', '-d', '-t'], d, 'b_' + f, 'bya', names)
        user = [(l, b) for l, b in rules if l != '$accept']
        start = rules[0][1][0]
        def derive(sym, depth):
            if sym not in nts:
                return [] if sym == 'error' else [sym]
            alts = [b for l, b in user if l == sym]
            if depth > 12:
                alts.sort(key=lambda b: sum(1 for s in b if s in nts))
                alts = alts[:1]
            out = []
            for s in rng.choice(alts):
                out += derive(s, depth + 1)
                if len(out) > 300:
                    raise RecursionError
            return out
        n = nd = 0
        for i in range(N):
            try:
                toks = derive(start, 0)
            except RecursionError:
                continue
            if i % 3 == 1 and toks:
                k = rng.randrange(len(toks))
                op = rng.randrange(3)
                if op == 0: del toks[k]
                elif op == 1: toks.insert(k, rng.choice(terms))
                else: toks[k] = rng.choice(terms)
            inp = ''.join(t + '\n' for t in toks)
            n += 1
            if bis:
                a, b = trace(mine, inp, 'mine'), trace(bis, inp, 'bison')
            else:
                a = b = ''
            if mineb and bya and a == b:
                a, b = trace(mineb, inp, 'mine'), trace(bya, inp, 'mine')
            if a != b:
                nd += 1
                if nd <= 3:
                    print('%s: DIFF on %r\n mine:  %s\n other: %s' % (f, toks, a[-300:], b[-300:]))
        print('%s: %d inputs, %d differences' % (f, n, nd))
        total += n; diffs += nd
    finally:
        shutil.rmtree(d, ignore_errors=True)
print('total %d inputs, %d differences' % (total, diffs))
sys.exit(1 if diffs else 0)
