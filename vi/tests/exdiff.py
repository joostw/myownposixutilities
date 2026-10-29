#!/usr/bin/env python3
"""Run ex script cases (tests/excases.txt) under this ex, nvi's ex and
vim's ex mode; show stdout, exit status and the resulting file where this
ex differs from both.  Usage: exdiff.py [-a] [name...]   (-a: show all)"""
import os, sys, subprocess, shutil, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
MINE = os.path.join(HERE, '..', 'ex')
EDITORS = {
    'mine': [MINE, '-s'],
    'nvi': ['/usr/bin/nex', '-s'],
    'vim': ['/usr/bin/vim', '-u', 'NONE', '-i', 'NONE', '-N', '-es'],
}

def parse(path):
    cases, cur = [], None
    for line in open(path, encoding='latin-1'):
        line = line.rstrip('\n')
        if line.startswith('#### '):
            cur = {'name': line[5:].strip(), 'text': [], 'cmds': [], 'part': 0}
            cases.append(cur)
        elif cur is not None:
            if line == '----' and cur['part'] == 0:
                cur['part'] = 1
            elif cur['part'] == 0:
                cur['text'].append(line)
            else:
                cur['cmds'].append(line)
    return cases

def run(argv, case, work):
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    path = os.path.join(work, 'f.txt')
    with open(path, 'w', encoding='latin-1') as f:
        f.write(''.join(l + '\n' for l in case['text']))
    script = ''.join(l + '\n' for l in case['cmds'])
    if not case['cmds'] or case['cmds'][-1].strip() not in ('q', 'q!', 'x', 'wq', 'wq!'):
        script += 'w! f.txt\nq!\n'
    env = dict(os.environ, LC_ALL='C', HOME=work, SHELL='/bin/sh', COLUMNS='80', LINES='24')
    for k in ('EXINIT', 'TERM'):
        env.pop(k, None)
    try:
        r = subprocess.run(argv + ['f.txt'], input=script.encode('latin-1'), cwd=work,
                           capture_output=True, timeout=10, env=env)
        out = r.stdout.decode('latin-1').replace(work, '$W')
        res = open(path, encoding='latin-1').read() if os.path.exists(path) else None
        return {'out': out, 'rc': r.returncode, 'file': res, 'err': r.stderr.decode('latin-1')}
    except subprocess.TimeoutExpired:
        return {'out': 'TIMEOUT', 'rc': -1, 'file': None, 'err': ''}

def main():
    args = sys.argv[1:]
    showall = '-a' in args
    args = [a for a in args if a != '-a']
    cases = parse(os.path.join(HERE, 'excases.txt'))
    if args:
        cases = [c for c in cases if c['name'] in args]
    work = tempfile.mkdtemp()
    nd = 0
    for c in cases:
        res = {e: run(EDITORS[e], c, work + '/' + e) for e in EDITORS}
        def key(r):
            return (r['out'], r['file'], r['rc'] != 0)
        m = key(res['mine'])
        if not showall and (m == key(res['nvi']) or m == key(res['vim'])):
            continue
        nd += 1
        print('==== %s' % c['name'])
        for e in EDITORS:
            r = res[e]
            print('  %-4s rc=%d out=%r file=%r' % (e, r['rc'], r['out'], r['file']))
            if e == 'mine' and r['err']:
                print('       err=%r' % r['err'])
    print('%d of %d differ from both' % (nd, len(cases)))
    shutil.rmtree(work, ignore_errors=True)

main()
