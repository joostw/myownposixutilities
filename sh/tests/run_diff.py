#!/usr/bin/env python3
"""Differential tester: run each case with our sh and reference shells.
Cases file format: lines '#### name' start a case; the rest is the script.
Compares stdout + exit status. Prints cases where ours differs from the
reference majority (dash, bash --posix, yash --posix, busybox ash, mksh)."""
import subprocess, sys, os, tempfile, collections
here = os.path.dirname(os.path.abspath(__file__))
OURS = os.path.join(here, '..', 'sh')
REFS = {'dash': ['dash'], 'bash': ['bash', '--posix'], 'yash': ['yash', '--posix'],
        'ash': ['busybox', 'ash'], 'mksh': ['mksh']}
def parse(path):
    cases, name, buf = [], None, []
    for line in open(path, encoding='utf-8', errors='surrogateescape'):
        if line.startswith('#### '):
            if name: cases.append((name, ''.join(buf)))
            name, buf = line[5:].strip(), []
        else: buf.append(line)
    if name: cases.append((name, ''.join(buf)))
    return cases
def run(cmd, script, tmp):
    p = os.path.join(tmp, 'script.sh')
    open(p, 'w', encoding='utf-8', errors='surrogateescape').write(script)
    env = dict(os.environ, LC_ALL='C', PATH='/usr/local/bin:/usr/bin:/bin', HOME=tmp, ENV='')
    env.pop('BASH_ENV', None)
    try:
        r = subprocess.run(cmd + [p], cwd=tmp, capture_output=True, timeout=10, env=env, stdin=subprocess.DEVNULL)
        return (r.stdout.decode('utf-8', 'replace'), r.returncode), r.stderr.decode('utf-8','replace')
    except subprocess.TimeoutExpired:
        return ('TIMEOUT', -1), ''
WORK = '/tmp/claude-0/shdiff/work'
def fresh():
    import shutil
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK)
    return WORK
def main():
    files = sys.argv[1:] or [os.path.join(here, 'cases.txt')]
    total = bad = 0
    for f in files:
        for name, script in parse(f):
            total += 1
            res = {}
            ours, oerr = run([OURS], script, fresh())
            for k, cmd in REFS.items():
                res[k] = run(cmd, script, fresh())[0]
            cnt = collections.Counter(res.values())
            majority, mc = cnt.most_common(1)[0]
            agree = [k for k, v in res.items() if v == ours]
            if ours != majority and len(agree) < 2:
                bad += 1
                print('=== %s   (agree: %s)' % (name, ','.join(agree) or 'none'))
                print('--- ours (rc=%d):\n%s' % (ours[1], ours[0]), end='')
                if oerr: print('--- ours stderr:\n' + oerr, end='')
                print('--- majority %s (rc=%d):\n%s' % ([k for k,v in res.items() if v==majority], majority[1], majority[0]), end='')
    print('total=%d differing=%d' % (total, bad))
main()
