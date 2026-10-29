#!/usr/bin/env python3
"""Regression suite for ed: each case in cases.py (a file, an ed script and
options) is run in a fresh directory; the standard output, the exit status,
the files written and (where the case asks) the standard error are compared
with tests/expected.json.  --freeze records the current results instead.

usage: run.py ED [--freeze] [name...]
The cases are shared with difftest.py, which compares with GNU ed instead.
"""
import base64, hashlib, json, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from cases import CASES
sys.path.insert(0, os.path.join(HERE, '..', '..', 'testlib'))
import testlib
U8 = testlib.utf8_locale()

EXPECTED = os.path.join(HERE, 'expected.json')


def locale_of(case):
    """the case's locale: a UTF-8 one unless it names another; None if the
    case needs a UTF-8 locale and there is none"""
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
    files = {}
    for name in sorted(os.listdir(d)):
        p = os.path.join(d, name)
        if os.path.isfile(p):
            data = open(p, 'rb').read()
            # small files as they are (base64), large ones by their digest
            if len(data) > 4096:
                files[name] = 'sha256:' + hashlib.sha256(data).hexdigest()
            else:
                files[name] = base64.b64encode(data).decode()
    return {
        'out': r.stdout.decode(errors='replace'),
        'status': r.returncode,
        'files': files,
        'err': r.stderr.decode(errors='replace') if case.get('stderr') else '',
    }


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(2)
    ed = os.path.abspath(args[0])
    freeze = '--freeze' in args
    names = [a for a in args[1:] if not a.startswith('--')]
    expected = {}
    if os.path.exists(EXPECTED) and not freeze:
        expected = json.load(open(EXPECTED))
    base = tempfile.mkdtemp(prefix='edrun.')
    results, n, fails = {}, 0, 0
    skipped = 0
    try:
        for i, case in enumerate(CASES):
            name = case.get('name', 'case%d' % i)
            if names and name not in names:
                continue
            if locale_of(case) is None:
                skipped += 1        # needs a UTF-8 locale, and there is none
                continue
            n += 1
            got = run(ed, case, os.path.join(base, str(i)))
            results[name] = got
            if freeze:
                continue
            want = expected.get(name)
            if got != want:
                fails += 1
                print('FAIL', name)
                if want is None:
                    print('    no expected result (run with --freeze)')
                    continue
                for k in ('status', 'out', 'err', 'files'):
                    if got[k] != want[k]:
                        print('    %s: want %r' % (k, want[k] if k != 'out' else want[k][:300]))
                        print('    %s: got  %r' % (k, got[k] if k != 'out' else got[k][:300]))
    finally:
        shutil.rmtree(base, ignore_errors=True)
    if freeze:
        if names and os.path.exists(EXPECTED):
            old = json.load(open(EXPECTED))
            old.update(results)
            results = old
        with open(EXPECTED, 'w') as fp:
            json.dump(results, fp, indent=1, sort_keys=True)
            fp.write('\n')
        print('froze %d cases' % n)
        return
    print('%d passed, %d failed' % (n - fails, fails))
    sys.exit(1 if fails else 0)


main()
