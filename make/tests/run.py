#!/usr/bin/env python3
"""Regression suite for make.

usage: run.py MAKE [--freeze] [name...]

Each case is a shell script run by sh in a fresh empty directory with
$M pointing at the binary under test, TZ=UTC, LC_ALL=C and umask 022.
Its standard output and standard error (merged) are compared with
tests/expected/NAME.out.  --freeze writes the expected files instead.
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CASES = os.path.join(HERE, 'cases')
EXPECTED = os.path.join(HERE, 'expected')

def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__); sys.exit(2)
    pax = os.path.abspath(args[0])
    ref = '/usr/bin/gmake'
    freeze = '--freeze' in args
    names = [a for a in args[1:] if a != '--freeze']
    files = sorted(f[:-3] for f in os.listdir(CASES) if f.endswith('.sh'))
    if names:
        files = [f for f in files if f in names]
    os.makedirs(EXPECTED, exist_ok=True)
    fails = 0
    for name in files:
        tmp = tempfile.mkdtemp(prefix='paxt.')
        env = {k: v for k, v in os.environ.items() if k not in ('MAKEFLAGS', 'MAKELEVEL', 'MFLAGS', 'MAKE', 'CURDIR')}
        env.update(M=pax, TZ='UTC', LC_ALL='C', TMPDIR='/tmp', HOME=tmp, LIB=os.path.join(HERE, 'lib.sh'))
        try:
            r = subprocess.run(['sh', '-c', 'umask 022; set -u; . "$1"', 'case',
                                os.path.join(CASES, name + '.sh')],
                               cwd=tmp, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=60)
            out = r.stdout.replace(tmp.encode(), b'$TMP').replace(pax.encode(), b'$M')
        except subprocess.TimeoutExpired:
            out = b'TIMEOUT\n'
        finally:
            subprocess.run(['chmod', '-R', 'u+rwx', tmp])
            shutil.rmtree(tmp, ignore_errors=True)
        exp = os.path.join(EXPECTED, name + '.out')
        if freeze:
            open(exp, 'wb').write(out)
            continue
        want = open(exp, 'rb').read() if os.path.exists(exp) else None
        if out != want:
            fails += 1
            print('FAIL', name)
            sys.stdout.flush()
            if want is not None:
                import difflib
                for l in difflib.unified_diff(want.decode(errors='replace').splitlines(),
                                              out.decode(errors='replace').splitlines(),
                                              'expected', 'got', lineterm='', n=2):
                    print('   ', l)
            else:
                print(out.decode(errors='replace'))
    if not freeze:
        print('%d passed, %d failed' % (len(files) - fails, fails))
    sys.exit(1 if fails else 0)

main()
