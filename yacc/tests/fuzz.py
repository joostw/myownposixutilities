#!/usr/bin/env python3
"""Mutation fuzzing of the grammar reader and table construction.

usage: fuzz.py YACC SEED COUNT

The seeds are tests/grammars/*.y and the grammars in tests/cases.  Each
mutant (bytes inserted, deleted, duplicated or replaced, often with
grammar punctuation) is given to YACC (built with sanitizers); any
sanitizer report, signal or hang is a failure, and the mutant is saved
in /tmp/yfuzz-fail.
"""
import os, random, re, subprocess, sys, tempfile, shutil

yacc = os.path.abspath(sys.argv[1])
rng = random.Random(int(sys.argv[2]))
count = int(sys.argv[3])
here = os.path.dirname(os.path.abspath(__file__))
seeds = []
for f in os.listdir(os.path.join(here, 'grammars')):
    seeds.append(open(os.path.join(here, 'grammars', f), 'rb').read())
for f in os.listdir(os.path.join(here, 'cases')):
    t = open(os.path.join(here, 'cases', f), 'rb').read()
    seeds += re.findall(rb"<<'G'\n(.*?)\nG\n", t, re.S)
tokens = [b'%%', b'%{', b'%}', b'%token', b'%left', b'%right', b'%nonassoc', b'%type', b'%start',
          b'%union', b'%prec', b'<', b'>', b'{', b'}', b'$$', b'$1', b'$-1', b'$<x>2', b'|', b';', b':',
          b"'", b'"', b'/*', b'*/', b'\\', b'\n', b'0', b'99999', b'error', b'a', b'%expect']

def mutate(s):
    s = bytearray(s)
    for _ in range(rng.randint(1, 6)):
        op = rng.randrange(5)
        pos = rng.randint(0, len(s))
        if op == 0:
            s[pos:pos] = rng.choice(tokens)
        elif op == 1 and s:
            del s[pos:pos + rng.randint(1, 20)]
        elif op == 2 and s:
            a = rng.randrange(len(s)); b = min(len(s), a + rng.randint(1, 200))
            s[pos:pos] = s[a:b]
        elif op == 3 and s:
            s[min(pos, len(s) - 1)] = rng.randrange(256)
        else:
            s[pos:pos] = bytes([rng.randrange(256)])
    return bytes(s)

os.makedirs('/tmp/yfuzz-fail', exist_ok=True)
d = tempfile.mkdtemp(prefix='yfuzz.')
fails = accepted = 0
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=0', UBSAN_OPTIONS='print_stacktrace=1')
try:
    for i in range(count):
        m = mutate(rng.choice(seeds))
        open(os.path.join(d, 'g.y'), 'wb').write(m)
        opts = rng.choice([[], ['-d'], ['-v'], ['-dvt'], ['-l', '-p', 'xx']])
        try:
            r = subprocess.run([yacc] + opts + ['g.y'], cwd=d, capture_output=True, timeout=30, env=env)
            bad = r.returncode < 0 or r.returncode > 2 or b'Sanitizer' in r.stderr or b'runtime error' in r.stderr or b'internal error' in r.stderr
            why = r.stderr[-1500:].decode(errors='replace')
            accepted += r.returncode == 0
        except subprocess.TimeoutExpired:
            bad, why = True, 'timeout'
        if bad:
            fails += 1
            p = '/tmp/yfuzz-fail/f%d.y' % i
            open(p, 'wb').write(m)
            print('FAIL', p, why)
finally:
    shutil.rmtree(d, ignore_errors=True)
print('%d mutants, %d accepted, %d failures' % (count, accepted, fails))
