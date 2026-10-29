#!/usr/bin/env python3
"""Differential tests against GNU ed: each case is a file, an ed script and
options; standard output, the exit status and the written files are
compared.

usage: difftest.py ED [--ref=/usr/bin/ed] [-v] [name...]
"""
import os, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cases import CASES
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'testlib'))
import testlib
U8 = testlib.utf8_locale()


def locale_of(case):
    """as in run.py: a UTF-8 locale unless the case names another; None if
    the case needs one and there is none"""
    loc = case.get('locale', 'C.UTF-8')
    if loc.lower().replace('-', '') == 'c.utf8':
        if U8:
            return U8
        texts = [case['script']] + list(case.get('files', {}).values())
        return None if testlib.non_ascii(*texts) else 'C'
    return loc

def run(ed, case, d):
    os.makedirs(d)
    for name, text in case.get('files', {'f': 'one\ntwo\nthree\nfour\nfive\n'}).items():
        with open(os.path.join(d, name), 'w') as fp:
            fp.write(text)
    args = case.get('args', ['-s', 'f'])
    env = dict(os.environ, LC_ALL=locale_of(case), HOME=d)
    r = subprocess.run([ed] + args, input=case['script'].encode(), cwd=d, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
    out = r.stdout.decode(errors='replace')
    files = {}
    for name in sorted(os.listdir(d)):
        p = os.path.join(d, name)
        if os.path.isfile(p):
            files[name] = open(p, 'rb').read()
    err = r.stderr.decode(errors='replace') if case.get('stderr') else ''
    return out, r.returncode, files, err

def main():
    args = sys.argv[1:]
    ref = '/usr/bin/ed'
    verbose = False
    names = []
    for a in args[1:]:
        if a.startswith('--ref='):
            ref = a[6:]
        elif a == '-v':
            verbose = True
        else:
            names.append(a)
    mine = os.path.abspath(args[0])
    base = tempfile.mkdtemp(prefix='eddiff.')
    fails = n = skipped = 0
    for i, case in enumerate(CASES):
        name = case.get('name', 'case%d' % i)
        if names and name not in names:
            continue
        if case.get('differs') or locale_of(case) is None:
            skipped += 1
            continue
        n += 1
        a = run(mine, case, os.path.join(base, '%d.mine' % i))
        b = run(ref, case, os.path.join(base, '%d.ref' % i))
        if a != b:
            fails += 1
            print('DIFF', name, repr(case['script'][:200]))
            if a[0] != b[0]:
                import difflib
                for l in difflib.unified_diff(b[0].splitlines(), a[0].splitlines(), 'ref', 'mine', lineterm='', n=1):
                    print('   ', l)
            if a[1] != b[1]:
                print('    status: ref %d mine %d' % (b[1], a[1]))
            if a[2] != b[2]:
                for k in sorted(set(a[2]) | set(b[2])):
                    if a[2].get(k) != b[2].get(k):
                        print('    file %s: ref %r mine %r' % (k, (b[2].get(k) or b'')[:200], (a[2].get(k) or b'')[:200]))
            if a[3] != b[3]:
                print('    stderr: ref %r mine %r' % (b[3], a[3]))
        elif verbose:
            print('same', name)
    shutil.rmtree(base, ignore_errors=True)
    print('%d/%d same as %s (%d skipped)' % (n - fails, n, ref, skipped))
    sys.exit(1 if fails else 0)

main()
