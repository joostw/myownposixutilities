#!/usr/bin/env python3
"""Mutation fuzzing of the lex source reader and the automaton construction.

usage: fuzz.py LEX SEED COUNT

The seeds are the sources in tests/cases.py, tests/lexers and tests/cases.  Each
mutant (bytes inserted, deleted, duplicated or replaced, often with
lex syntax) is given to LEX (built with sanitizers); any
sanitizer report, signal or hang is a failure, and the mutant is saved
in /tmp/lfuzz-fail.
"""
import os, random, re, subprocess, sys, tempfile, shutil

lex = os.path.abspath(sys.argv[1])
rng = random.Random(int(sys.argv[2]))
count = int(sys.argv[3])
here = os.path.dirname(os.path.abspath(__file__))
seeds = []
for f in os.listdir(os.path.join(here, 'lexers')):
    seeds.append(open(os.path.join(here, 'lexers', f), 'rb').read())
for f in os.listdir(os.path.join(here, 'cases')):
    t = open(os.path.join(here, 'cases', f), 'rb').read()
    seeds += re.findall(rb"<<'L'\n(.*?)\nL\n", t, re.S)
sys.path.insert(0, here)
import cases
seeds += [c['src'].encode('latin-1') for c in cases.CASES]
tokens = [b'%%', b'%{', b'%}', b'%s', b'%x', b'%array', b'%pointer', b'%p 10', b'[', b']', b'[^', b'(',
          b')', b'{', b'}', b'{2,3}', b'{0}', b'{1000}', b'{D}', b'"', b'\\', b'\\x', b'\\0', b'/', b'$', b'^',
          b'<', b'>', b'<*>', b'|', b'*', b'+', b'?', b'.', b'\n', b'\t', b' ', b'[:alpha:]', b'[.a.]',
          b'REJECT;', b'yymore();', b'BEGIN 0;', b'/*', b'*/', b'D [0-9]\n']

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

os.makedirs('/tmp/lfuzz-fail', exist_ok=True)
d = tempfile.mkdtemp(prefix='lfuzz.')
fails = accepted = 0
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=0', UBSAN_OPTIONS='print_stacktrace=1')
try:
    for i in range(count):
        m = mutate(rng.choice(seeds))
        open(os.path.join(d, 'g.l'), 'wb').write(m)
        opts = rng.choice([[], ['-t'], ['-v'], ['-tv'], ['-n']])
        try:
            r = subprocess.run([lex] + opts + ['g.l'], cwd=d, capture_output=True, timeout=30, env=env)
            bad = r.returncode < 0 or r.returncode > 2 or b'Sanitizer' in r.stderr or b'runtime error' in r.stderr or b'internal error' in r.stderr
            why = r.stderr[-1500:].decode(errors='replace')
            accepted += r.returncode == 0
        except subprocess.TimeoutExpired:
            bad, why = True, 'timeout'
        if bad:
            fails += 1
            p = '/tmp/lfuzz-fail/f%d.y' % i
            open(p, 'wb').write(m)
            print('FAIL', p, why)
finally:
    shutil.rmtree(d, ignore_errors=True)
print('%d mutants, %d accepted, %d failures' % (count, accepted, fails))
