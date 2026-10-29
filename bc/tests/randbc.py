#!/usr/bin/env python3
"""Random differential testing of bc against GNU bc.

usage: randbc.py BC SEED COUNT [-l] [-v]

Each round is a random program: settings of scale, ibase and obase,
expressions with + - * / % ^, unary minus, ++ and --, assignments
(also compound), sqrt, length, scale(), arrays, relational operators,
if/while/for with break, functions with parameters, auto variables
and arrays passed by value, recursion, strings, and with -l the math
library.  The standard output of both must be the same, and both must
report an error (on standard error) or neither.
"""
import os, random, re, subprocess, sys

bc = os.path.abspath(sys.argv[1])
rng = random.Random(int(sys.argv[2]))
count = int(sys.argv[3])
mathlib = '-l' in sys.argv
verbose = '-v' in sys.argv
show = int(sys.argv[sys.argv.index('--show') + 1]) if '--show' in sys.argv else 0

def number(base=10):
    digits = '0123456789ABCDEF'[:base]
    r = rng.random()
    if r < 0.3:
        return rng.choice(digits[1:] if base > 1 else '1')
    ip = ''.join(rng.choice(digits) for _ in range(rng.randint(1, 6))).lstrip('0') or '0'
    if rng.random() < 0.5:
        fp = ''.join(rng.choice(digits) for _ in range(rng.randint(1, 6)))
        if rng.random() < 0.2:
            return '.' + fp
        return ip + '.' + fp
    return ip

VARS = 'abcdxyz'

def expr(depth, base=10):
    r = rng.random()
    if depth > 3 or r < 0.25:
        return number(base)
    if r < 0.35:
        return rng.choice(VARS)
    if r < 0.4:
        return '%s[%d]' % (rng.choice('uv'), rng.randint(0, 5))
    if r < 0.47:
        e = expr(depth + 1, base)
        return '-' + ('(%s)' % e if e.startswith('-') else e)
    if r < 0.75:
        op = rng.choice(['+', '-', '*', '/', '%', '*', '+'])
        return '(%s %s %s)' % (expr(depth + 1, base), op, expr(depth + 1, base))
    if r < 0.8:
        return '(%s ^ %s)' % (expr(depth + 1, base), rng.choice(['0', '1', '2', '3', '5', '-1', '-2', '7']))
    if r < 0.84:
        return 'sqrt(%s)' % expr(depth + 1, base).lstrip('-')
    if r < 0.87:
        return 'length(%s)' % expr(depth + 1, base)
    if r < 0.9:
        return 'scale(%s)' % expr(depth + 1, base)
    if r < 0.93:
        v = rng.choice(VARS)
        return rng.choice(['++%s', '--%s', '%s++', '%s--']) % v
    if r < 0.96:
        return '(%s %s %s)' % (rng.choice(VARS), rng.choice(['=', '+=', '-=', '*=']), expr(depth + 1, base))
    if mathlib:
        f = rng.choice(['s', 'c', 'a', 'e', 'l', 'j'])
        if f == 'j':
            return 'j(%d, %s)' % (rng.randint(-3, 4), expr(depth + 1, base))
        if f == 'e':
            # (decimal digits only where ibase is 10: digits beyond ibase are undefined)
            return 'e(%s)' % (rng.choice(['1', '-2', '.5', '3.25', '-.75', '10']) if base == 10 else number(base))
        if f == 'l':
            return 'l(%s)' % number(base).lstrip('-')
        return '%s(%s)' % (f, expr(depth + 1, base))
    return number(base)

def rel():
    return '%s %s %s' % (expr(2), rng.choice(['<', '>', '<=', '>=', '==', '!=']), expr(2))

def statement(depth, infunc=False, inloop=False):
    r = rng.random()
    if depth > 2 or r < 0.45:
        return expr(0)
    if r < 0.55:
        return '%s = %s' % (rng.choice(VARS), expr(1))
    if r < 0.6:
        return '%s[%d] = %s' % (rng.choice('uv'), rng.randint(0, 5), expr(1))
    if r < 0.65:
        return 'if (%s) %s' % (rel(), statement(depth + 1, infunc, inloop))
    if r < 0.7:
        v = rng.choice('ijk')
        return 'for (%s = 0; %s < %d; %s++) { %s }' % (v, v, rng.randint(0, 4), v,
                                                       statement(depth + 1, infunc, True))
    if r < 0.74:
        v = rng.choice('ijk')
        return '%s = %d; while (%s > 0) { %s--; %s }' % (v, rng.randint(0, 3), v, v,
                                                        statement(depth + 1, infunc, True))
    if r < 0.77 and inloop:
        return 'break'
    if r < 0.82:
        return '"str%d"' % rng.randint(0, 9)
    if r < 0.86 and infunc:
        return 'return (%s)' % expr(1)
    if r < 0.9:
        return 'f(%s, %s)' % (expr(1), expr(1))
    if r < 0.93:
        return 'g(u[])'
    return '{ %s; %s }' % (statement(depth + 1, infunc, inloop), statement(depth + 1, infunc, inloop))

def program():
    lines = []
    if mathlib:
        # (GNU bc's math library loses accuracy at small scales: s(588) at
        # scale 3 is -.504, not -.498)
        lines.append('scale = %d' % rng.choice([20, 25, 30, 40]))
    elif rng.random() < 0.7:
        lines.append('scale = %d' % rng.choice([0, 0, 1, 2, 3, 5, 10, 20, 30]))
    lines.append('define f(x, y) {\n  auto a, t[]\n  a = x * 2\n  t[1] = y\n  %s\n  return (a + t[1] - b)\n}'
                 % statement(1, True))
    lines.append('define g(w[]) {\n  w[0] = w[0] + 1\n  return (w[0] + w[1])\n}')
    lines.append('define r(n) {\n  if (n <= 0) return (1)\n  return (n * r(n - 1))\n}')
    for _ in range(rng.randint(3, 10)):
        r = rng.random()
        if r < 0.1:
            lines.append('obase = %d' % rng.choice([2, 3, 8, 10, 16, 10, 10, 17, 25, 100, 1000]))
        elif r < 0.15:
            base = rng.choice([2, 8, 16, 10])
            lines.append('ibase = %s; %s; ibase = A' % ({2: '2', 8: '8', 16: '16', 10: 'A'}[base] if base != 16 else 'F+1',
                                                       expr(1, base)))
        elif r < 0.2:
            lines.append('r(%d)' % rng.randint(0, 25))
        else:
            lines.append('; '.join(statement(0) for _ in range(rng.randint(1, 3))))
    lines.append('a; b; c; u[1]; v[2]; obase=10; g(v[]); v[0]')
    return '\n'.join(lines) + '\n'

def run(cmd, prog, env=None):
    try:
        r = subprocess.run(cmd, input=prog.encode(), capture_output=True, timeout=10,
                           env=dict(os.environ, **(env or {})))
        out = r.stdout.decode('latin-1')
        # GNU bc can print a zero as "-0" (from a negative operand that
        # truncates to zero); the standard says a zero is written as 0
        out = re.sub(r'(?m)^-0$', '0', out)
        return out, bool(r.stderr.strip())
    except subprocess.TimeoutExpired:
        return 'TIMEOUT', False

stats = dict(rounds=0, diffs=0, errors=0, timeouts=0)
for rnd in range(1, count + 1):
    prog = program()
    if show:
        if rnd == show:
            print(prog)
            sys.exit(0)
        continue
    flags = ['-l'] if mathlib else []
    a, ea = run([bc] + flags, prog, {'BC_TEST_GNU_SQRT': '1'})
    b, eb = run(['bc'] + flags, prog)
    stats['rounds'] += 1
    if b == 'TIMEOUT':
        # (GNU bc is much slower on some programs, such as big powers)
        stats['timeouts'] += 1
        continue
    if ea or eb:
        stats['errors'] += 1
    if a != b or ea != eb:
        stats['diffs'] += 1
        print('DIFF (round %d) error mine %s gnu %s' % (rnd, ea, eb))
        if verbose:
            print(prog)
        la, lb = a.split('\n'), b.split('\n')
        for i in range(max(len(la), len(lb))):
            x = la[i] if i < len(la) else '<none>'
            y = lb[i] if i < len(lb) else '<none>'
            if x != y:
                print('  line %d\n   mine: %r\n   gnu:  %r' % (i + 1, x[:200], y[:200]))
                break
    if rnd % 100 == 0:
        print('progress', rnd, stats, flush=True)
print(stats)
sys.exit(1 if stats['diffs'] else 0)
