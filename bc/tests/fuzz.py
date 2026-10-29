#!/usr/bin/env python3
"""Mutation fuzzing of bc: the scanner, the parser and the interpreter.

usage: fuzz.py BC SEED COUNT

The seeds are the programs of tests/cases.py.  Each mutant (bytes
inserted, deleted, duplicated or replaced, often with bc tokens) is run
by BC (built with sanitizers), with and without -l; a sanitizer report
or a crash is a failure, and the mutant is saved in /tmp/bcfuzz-fail.
Mutants that run longer than the time limit are only counted.
"""
import os, random, re, subprocess, sys

bc = os.path.abspath(sys.argv[1])
rng = random.Random(int(sys.argv[2]))
count = int(sys.argv[3])
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import cases
seeds = [c['prog'].encode() for c in cases.CASES]
tokens = [b'\n', b';', b'{', b'}', b'(', b')', b'[', b']', b'define f(x) {\n', b'auto a, b[]\n', b'return (',
          b'for (i = 0; i < 3; i++) ', b'while (', b'if (', b'else ', b'break', b'quit', b'scale = ', b'ibase = ',
          b'obase = ', b'sqrt(', b'length(', b'scale(', b'^', b'%', b'/', b'*', b'-', b'+', b'++', b'--', b'=',
          b'+=', b'^=', b'"', b'/*', b'*/', b'\\\n', b'.', b'0', b'99999999999', b'A', b'F', b'a[', b'[]', b',',
          b'<', b'==', b'!', b'&&', b'e(', b'l(', b'j(', b'print ', b'#']

def mutate(s):
    s = bytearray(s)
    for _ in range(rng.randint(1, 6)):
        op = rng.randrange(5)
        pos = rng.randint(0, len(s))
        if op == 0:
            s[pos:pos] = rng.choice(tokens)
        elif op == 1 and s:
            del s[pos:pos + rng.randint(1, 10)]
        elif op == 2 and s:
            a = rng.randrange(len(s)); b = min(len(s), a + rng.randint(1, 100))
            s[pos:pos] = s[a:b]
        elif op == 3 and s:
            s[min(pos, len(s) - 1)] = rng.randrange(256)
        else:
            s[pos:pos] = bytes([rng.randrange(256)])
    return bytes(s)

os.makedirs('/tmp/bcfuzz-fail', exist_ok=True)
fails = slow = 0
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='print_stacktrace=1')
for i in range(count):
    m = mutate(rng.choice(seeds))
    args = rng.choice([[], ['-l']])
    try:
        r = subprocess.run([bc] + args, input=m, capture_output=True, timeout=10, env=env)
        bad = r.returncode < 0 or r.returncode > 2 or b'Sanitizer' in r.stderr or \
            re.search(rb'\.c:\d+:\d+: runtime error', r.stderr) is not None
        why = r.stderr[-1500:].decode(errors='replace')
    except subprocess.TimeoutExpired:
        slow += 1
        continue
    if bad:
        fails += 1
        p = '/tmp/bcfuzz-fail/f%d.bc' % i
        open(p, 'wb').write(m)
        print('FAIL', p, args, why)
print('%d mutants, %d over the time limit, %d failures' % (count, slow, fails))
