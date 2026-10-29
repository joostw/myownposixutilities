#!/usr/bin/env python3
"""Regression tests: run every case in cases*.txt with ../sh and compare
stdout and exit status with tests/expected.json.  Use --update to record."""
import json, os, shutil, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
SH = os.path.join(here, '..', 'sh')
WORK = os.path.join(os.environ.get('TMPDIR', '/tmp'), 'sh-regress-work')

def parse(path):
    cases, name, buf = [], None, []
    for line in open(path, encoding='utf-8', errors='surrogateescape'):
        if line.startswith('#### '):
            if name: cases.append((name, ''.join(buf)))
            name, buf = line[5:].strip(), []
        else:
            buf.append(line)
    if name: cases.append((name, ''.join(buf)))
    return cases

# the standard utilities: getconf PATH (os.confstr), as POSIX gives them
try:
    STDPATH = os.confstr('CS_PATH') or '/usr/bin:/bin'
except (ValueError, OSError):
    STDPATH = '/usr/bin:/bin'


def run(script):
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK)
    p = os.path.join(WORK, 'script.sh')
    open(p, 'w', encoding='utf-8', errors='surrogateescape').write(script)
    env = dict(os.environ, LC_ALL='C', PATH=STDPATH, HOME=WORK, ENV='')
    try:
        r = subprocess.run([SH, p], cwd=WORK, capture_output=True, timeout=20, env=env,
                           stdin=subprocess.DEVNULL)
        out = r.stdout.decode('utf-8', 'replace').replace(WORK, '$WORK')
        return [out, r.returncode]
    except subprocess.TimeoutExpired:
        return ['TIMEOUT', -1]

def main():
    update = '--update' in sys.argv
    expfile = os.path.join(here, 'expected.json')
    expected = {} if update or not os.path.exists(expfile) else json.load(open(expfile))
    got, fails, n = {}, 0, 0
    for f in sorted(x for x in os.listdir(here) if x.startswith('cases') and x.endswith('.txt')):
        for name, script in parse(os.path.join(here, f)):
            key = f + ':' + name
            n += 1
            got[key] = run(script)
            if not update and expected.get(key) != got[key]:
                fails += 1
                print('FAIL', key)
                print('  expected:', expected.get(key))
                print('  got:     ', got[key])
    if update:
        json.dump(got, open(expfile, 'w'), indent=1, sort_keys=True)
        print('recorded %d cases' % n)
    else:
        print('%d passed, %d failed' % (n - fails, fails))
    shutil.rmtree(WORK, ignore_errors=True)
    sys.exit(1 if fails else 0)

main()
