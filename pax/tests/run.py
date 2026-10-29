#!/usr/bin/env python3
"""Regression suite for pax.

usage: run.py PAX [--freeze] [name...]

Each case is a shell script run by sh in a fresh empty directory with
$PAX pointing at the binary under test, TZ=UTC, LC_ALL=C and umask 022.
Its standard output and standard error (merged) are compared with
tests/expected/NAME.out.  --freeze writes the expected files instead.
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'testlib'))
import testlib
CASES = os.path.join(HERE, 'cases')
EXPECTED = os.path.join(HERE, 'expected')

def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__); sys.exit(2)
    pax = os.path.abspath(args[0])
    freeze = '--freeze' in args
    names = [a for a in args[1:] if a != '--freeze']
    files = sorted(f[:-3] for f in os.listdir(CASES) if f.endswith('.sh'))
    if names:
        files = [f for f in files if f in names]
    os.makedirs(EXPECTED, exist_ok=True)
    helpers = tempfile.mkdtemp(prefix='paxh.')
    fstat = testlib.build('fstat', helpers)
    ltouch = testlib.build('ltouch', helpers)
    u8 = testlib.utf8_locale()
    fails = skipped = 0
    for name in files:
        tmp = tempfile.mkdtemp(prefix='paxt.')
        env = dict(os.environ, PAX=pax, TZ='UTC', LC_ALL='C', TMPDIR='/tmp', HOME=tmp,
                   TREECMP=os.path.join(HERE, 'treecmp.py'), LIB=os.path.join(HERE, 'lib.sh'),
                   FSTAT=fstat, LTOUCH=ltouch, U8=u8)
        try:
            r = subprocess.run(['sh', '-c', 'umask 022; set -u; . "$1"', 'case',
                                os.path.join(CASES, name + '.sh')],
                               cwd=tmp, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=60)
            out = r.stdout.replace(tmp.encode(), b'$TMP')
        except subprocess.TimeoutExpired:
            out = b'TIMEOUT\n'
        finally:
            subprocess.run(['chmod', '-R', 'u+rwx', tmp])
            shutil.rmtree(tmp, ignore_errors=True)
        if out == b'SKIP\n':
            skipped += 1        # the case needs what this system lacks (a UTF-8 locale)
            continue
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
    shutil.rmtree(helpers, ignore_errors=True)
    if not freeze:
        print('%d passed, %d failed' % (len(files) - fails - skipped, fails))
    sys.exit(1 if fails else 0)

main()
