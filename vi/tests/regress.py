#!/usr/bin/env python3
"""Regression tests for ex (tests/excases.txt) and vi (tests/vicases.txt).
Compares with tests/expected.json; --update records the current results.
--ex / --vi select one suite."""
import os, sys, json, shutil, tempfile, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
EX = os.path.abspath(os.environ.get('EX_BIN', os.path.join(HERE, '..', 'ex')))
VI = os.path.abspath(os.environ.get('VI_BIN', os.path.join(HERE, '..', 'vi')))

def parse(path, second):
    cases, cur = [], None
    for line in open(path, encoding='latin-1'):
        line = line.rstrip('\n')
        if line.startswith('#### '):
            cur = {'name': line[5:].strip(), 'text': [], second: [], 'part': 0}
            cases.append(cur)
        elif cur is not None:
            if line == '----' and cur['part'] == 0:
                cur['part'] = 1
            elif cur['part'] == 0:
                cur['text'].append(line)
            else:
                cur[second].append(line)
    return cases

def run_ex(case, work):
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    path = os.path.join(work, 'f.txt')
    open(path, 'w', encoding='latin-1').write(''.join(l + '\n' for l in case['text']))
    script = ''.join(l + '\n' for l in case['cmds'])
    if not case['cmds'] or case['cmds'][-1].strip() not in ('q', 'q!', 'x', 'wq', 'wq!'):
        script += 'w! f.txt\nq!\n'
    env = dict(os.environ, LC_ALL='C', HOME=work, SHELL='/bin/sh', COLUMNS='80', LINES='24')
    for k in ('EXINIT', 'TERM'):
        env.pop(k, None)
    try:
        r = subprocess.run([EX, '-s', 'f.txt'], input=script.encode('latin-1'), cwd=work,
                           capture_output=True, timeout=10, env=env)
    except subprocess.TimeoutExpired:
        return ['TIMEOUT']
    f = open(path, encoding='latin-1').read() if os.path.exists(path) else None
    return [r.stdout.decode('latin-1').replace(work, '$W'), r.stderr.decode('latin-1'), r.returncode, f]

def run_vi(case, work):
    from vidiff import keys_decode
    from vipty import Term
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    path = os.path.join(work, 'f.txt')
    open(path, 'w', encoding='latin-1').write(''.join(l + '\n' for l in case['text']))
    t = Term([VI, path], cwd=work, env={'HOME': work, 'SHELL': '/bin/sh'})
    try:
        for k in case['keys']:
            t.keys(keys_decode(k))
        t.keys('\x1b')
        t.keys(':wq!\r')
        if t.wait(5) is None:
            return ['TIMEOUT']
    finally:
        t.kill()
    return [open(path, encoding='latin-1').read()]

def main():
    update = '--update' in sys.argv
    suites = [s for s in ('ex', 'vi') if '--' + s in sys.argv] or ['ex', 'vi']
    expf = os.path.join(HERE, 'expected.json')
    exp = json.load(open(expf)) if os.path.exists(expf) else {}
    work = tempfile.mkdtemp()
    fails = n = 0
    for suite in suites:
        if suite == 'vi':
            try:
                import pyte  # noqa
            except ImportError:
                print('vi suite skipped: python module pyte not installed')
                continue
        cases = parse(os.path.join(HERE, suite + 'cases.txt'), 'cmds' if suite == 'ex' else 'keys')
        names = [a for a in sys.argv[1:] if not a.startswith('--')]
        if names:
            cases = [c for c in cases if c['name'] in names]
        for c in cases:
            key = suite + ':' + c['name']
            got = run_ex(c, work) if suite == 'ex' else run_vi(c, work)
            n += 1
            if update:
                exp[key] = got
            elif exp.get(key) != got:
                fails += 1
                print('FAIL', key)
                print('  expected:', exp.get(key))
                print('  got:     ', got)
    shutil.rmtree(work, ignore_errors=True)
    if update:
        json.dump(exp, open(expf, 'w'), indent=1, sort_keys=True)
        print('recorded %d cases' % n)
    else:
        print('%d passed, %d failed' % (n - fails, fails))
    sys.exit(1 if fails else 0)

main()
