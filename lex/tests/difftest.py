#!/usr/bin/env python3
"""Differential tests of lex against flex -X (POSIX mode).

usage: difftest.py LEX [NAME...]

Each case in cases.py is a lex source and a list of inputs; both
scanners are compiled and run on each input, and the outputs (stdout
and the exit status) must be the same.  Cases with a 'differs' reason
are known differences: they are run, and reported only if they stop
differing.
"""
import os, subprocess, sys, tempfile, shutil

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import cases

lex = os.path.abspath(sys.argv[1])
libdir = os.path.dirname(lex)
names = sys.argv[2:]

def build(cmd, d, exe, src):
    open(os.path.join(d, 't.l'), 'w').write(src)
    r = subprocess.run(cmd + ['t.l'], cwd=d, capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr
    c = subprocess.run(['gcc', '-w'] + os.environ.get('SCANFLAGS', '').split() + ['-o', exe, 'lex.yy.c', '-L' + libdir, '-ll'], cwd=d,
                       capture_output=True, text=True)
    if c.returncode:
        return None, c.stderr
    return os.path.join(d, exe), r.stderr

def run(exe, inp, args):
    try:
        r = subprocess.run([exe] + args, input=inp.encode('latin-1'), capture_output=True, timeout=10)
        return r.stdout.decode('latin-1') + ('' if r.returncode == 0 else '[status %d]' % r.returncode)
    except subprocess.TimeoutExpired:
        return 'TIMEOUT'

ok = bad = known = 0
for c in cases.CASES:
    if names and c['name'] not in names:
        continue
    d = tempfile.mkdtemp(prefix='lexdiff.')
    try:
        mine, merr = build([lex], d, 'mine', c['src'])
        ref, ferr = build(['flex', '-X'], d, 'ref', c['src'])
        if mine and not ref and c.get('differs'):
            known += 1
            continue
        if not mine or not ref:
            print('BUILD', c['name'], 'mine:' if not mine else '', merr if not mine else '',
                  'flex:' if not ref else '', ferr if not ref else '')
            bad += 1
            continue
        diffs = []
        for inp in c['inputs']:
            a, b = run(mine, inp, c.get('args', [])), run(ref, inp, c.get('args', []))
            if a != b:
                diffs.append((inp, a, b))
        if c.get('differs'):
            if diffs:
                known += 1
            else:
                print('NOW SAME', c['name'], '(listed as differing:', c['differs'] + ')')
                bad += 1
        elif diffs:
            bad += 1
            print('DIFF', c['name'])
            for inp, a, b in diffs[:3]:
                print('  input %r\n   mine: %r\n   flex: %r' % (inp, a[:400], b[:400]))
        else:
            ok += 1
    finally:
        shutil.rmtree(d, ignore_errors=True)
print('%d same, %d known differences, %d failures' % (ok, known, bad))
sys.exit(1 if bad else 0)
