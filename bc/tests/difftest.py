#!/usr/bin/env python3
"""Differential tests of bc against GNU bc.

usage: difftest.py BC [NAME...]

Each case in cases.py is a program for the standard input, with
options, files and environment variables; the standard output must be
the same, and both must write to standard error or neither.  Cases
with a 'differs' reason are known differences.
"""
import os, subprocess, sys, tempfile, shutil

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import cases

bc = os.path.abspath(sys.argv[1])
names = sys.argv[2:]

def run(cmd, c, d):
    env = dict(os.environ, **c.get('env', {}))
    env['BC_TEST_GNU_SQRT'] = '1'
    files = list(c.get('files', {}))
    r = subprocess.run(cmd + c.get('args', []) + files, input=c['prog'].encode(), capture_output=True,
                       timeout=60, cwd=d, env=env)
    return r.stdout.decode('latin-1'), bool(r.stderr.strip())

ok = bad = known = 0
for c in cases.CASES:
    if names and c['name'] not in names:
        continue
    d = tempfile.mkdtemp(prefix='bcdiff.')
    try:
        for f, text in c.get('files', {}).items():
            open(os.path.join(d, f), 'w').write(text)
        a, ea = run([bc], c, d)
        b, eb = run(['bc'], c, d)
        same = a == b and ea == eb
        if c.get('differs'):
            if same:
                print('NOW SAME', c['name'])
                bad += 1
            else:
                known += 1
        elif same:
            ok += 1
        else:
            bad += 1
            print('DIFF', c['name'], '(stderr: mine %s, gnu %s)' % (ea, eb))
            la, lb = a.split('\n'), b.split('\n')
            for i in range(max(len(la), len(lb))):
                x = la[i] if i < len(la) else '<none>'
                y = lb[i] if i < len(lb) else '<none>'
                if x != y:
                    print('  line %d\n   mine: %r\n   gnu:  %r' % (i + 1, x[:200], y[:200]))
                    break
    finally:
        shutil.rmtree(d, ignore_errors=True)
print('%d same, %d known differences, %d failures' % (ok, known, bad))
sys.exit(1 if bad else 0)
