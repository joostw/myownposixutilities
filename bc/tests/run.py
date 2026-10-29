#!/usr/bin/env python3
"""Regression suite: each tests/cases/NAME.sh runs in an empty temporary
directory with $BC (the bc under test), $SRCDIR (the directory of the
sources) and $CC (from $TESTCC or $CC, else c17, c99 or cc).  Its standard
output and standard error are compared with tests/expected/NAME.out.

usage: run.py BC [--freeze] [NAME...]
"""
import os, subprocess, sys, tempfile, shutil

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, '..', '..', 'testlib'))
import testlib
bc = os.path.abspath(sys.argv[1])
freeze = '--freeze' in sys.argv
names = [a for a in sys.argv[2:] if not a.startswith('--')]
cases = sorted(f[:-3] for f in os.listdir(os.path.join(here, 'cases')) if f.endswith('.sh'))
if names:
    cases = [c for c in cases if c in names]
fails = 0
for c in cases:
    d = tempfile.mkdtemp(prefix='brun.')
    env = dict(os.environ, BC=bc, LC_ALL='C', CC=testlib.compiler(),
               SRCDIR=os.path.dirname(here))
    try:
        r = subprocess.run(['sh', os.path.join(here, 'cases', c + '.sh')], cwd=d, env=env,
                           capture_output=True, timeout=300)
        out = r.stdout + r.stderr + ('exit %d\n' % r.returncode).encode()
    except subprocess.TimeoutExpired:
        out = b'TIMEOUT\n'
    finally:
        shutil.rmtree(d, ignore_errors=True)
    exp = os.path.join(here, 'expected', c + '.out')
    if freeze:
        open(exp, 'wb').write(out)
        print('froze', c)
        continue
    want = open(exp, 'rb').read() if os.path.exists(exp) else None
    if want == out:
        print('ok  ', c)
    else:
        fails += 1
        print('FAIL', c)
        sys.stdout.write(out.decode(errors='replace')[-3000:])
if not freeze:
    print('%d passed, %d failed' % (len(cases) - fails, fails))
sys.exit(1 if fails else 0)
