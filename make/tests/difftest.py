#!/usr/bin/env python3
"""Differential tests: run each case with this make and with a reference
make (GNU make by default) in fresh directories and compare the output
and exit status after normalising the program name and the parts of
diagnostics that are implementation-specific.

usage: difftest.py MAKE [--ref=/usr/bin/gmake] [-v] [name...]
"""
import os, re, shutil, subprocess, sys, tempfile
MINE = REF = None

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cases import CASES

def normalise(out, tmp):
    out = out.replace(tmp, '$TMP')
    lines = []
    for l in out.splitlines():
        l = re.sub(r'^(g?make|bmake)(\[\d+\])?:', 'make:', l)
        if re.match(r'^make: (Entering|Leaving) directory', l):
            continue
        l = l.replace(MINE, '$MAKE').replace(REF, '$MAKE')
        # GNU make puts the makefile and line in error reports
        l = re.sub(r'\[[^\]\s]*:\d+: ([^\]]*)\]', r'[\1]', l)
        l = re.sub(r'^make: \S+:\d+: \*\*\*', 'make: ***', l)
        l = re.sub(r'^make: \S+:\d+: ', 'make: ', l)
        # the signal of a killed command
        l = re.sub(r'\] (Killed|Terminated|Interrupt)$', '] Signal', l)
        lines.append(l)
    return '\n'.join(lines)

def run_case(make, case, tmp):
    os.makedirs(tmp)
    for name, text in case.get('files', {}).items():
        p = os.path.join(tmp, name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'w') as f:
            f.write(text)
    script = case.get('setup', '') + '\n' + case['run']
    env = {k: v for k, v in os.environ.items() if k not in ('MAKEFLAGS', 'MAKELEVEL', 'MFLAGS', 'MAKE')}
    env.update(M=make, LC_ALL='C', TZ='UTC')
    env.update(case.get('env', {}))
    try:
        r = subprocess.run(['sh', '-c', 'set -u\n' + script], cwd=tmp, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        out = r.stdout.decode(errors='replace')
    except subprocess.TimeoutExpired:
        out = 'TIMEOUT'
    return normalise(out, tmp)

def main():
    args = sys.argv[1:]
    ref = '/usr/bin/gmake'
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
    global MINE, REF
    MINE, REF = mine, ref
    fails = skipped = n = 0
    base = tempfile.mkdtemp(prefix='mkdiff.')
    for case in CASES:
        if names and case['name'] not in names:
            continue
        if 'gnu' in case.get('differs', ()) and 'gmake' in ref:
            skipped += 1
            continue
        if 'bmake' in case.get('differs', ()) and 'bmake' in ref:
            skipped += 1
            continue
        n += 1
        a = run_case(mine, case, os.path.join(base, case['name'] + '.mine'))
        b = run_case(ref, case, os.path.join(base, case['name'] + '.ref'))
        if a != b:
            fails += 1
            print('DIFF', case['name'])
            import difflib
            for l in difflib.unified_diff(b.splitlines(), a.splitlines(), 'reference', 'mine', lineterm='', n=1):
                print('   ', l)
        elif verbose:
            print('same', case['name'])
    shutil.rmtree(base, ignore_errors=True)
    print('%d/%d same as %s (%d skipped as known differences)' % (n - fails, n, ref, skipped))
    sys.exit(1 if fails else 0)

main()
