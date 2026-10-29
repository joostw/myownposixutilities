#!/usr/bin/env python3
"""Run vi keystroke cases under this vi, nvi and vim (vi-compatible mode)
and compare the resulting files.  Usage: vidiff.py [cases-file] [name...]"""
import os, sys, re, shutil, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vipty import Term

HERE = os.path.dirname(os.path.abspath(__file__))
MINE = os.path.join(HERE, '..', 'vi')
EDITORS = {
    'mine': [MINE],
    'nvi': ['/usr/bin/nvi'],
    'vim': ['/usr/bin/vim', '-u', 'NONE', '-i', 'NONE', '--noplugin'],
}

def parse(path):
    cases, cur = [], None
    for line in open(path, encoding='latin-1'):
        line = line.rstrip('\n')
        if line.startswith('#### '):
            cur = {'name': line[5:].strip(), 'text': [], 'keys': [], 'part': 0}
            cases.append(cur)
        elif cur is not None:
            if line == '----' and cur['part'] == 0:
                cur['part'] = 1
            elif cur['part'] == 0:
                cur['text'].append(line)
            else:
                cur['keys'].append(line)
    return cases

def keys_decode(s):
    rep = {'<ESC>': '\x1b', '<CR>': '\r', '<NL>': '\n', '<TAB>': '\t', '<BS>': '\x08',
           '<SP>': ' ', '<NUL>': '\x00', '<DEL>': '\x7f'}
    for k, v in rep.items():
        s = s.replace(k, v)
    s = re.sub(r'\^([A-Z@\[\]^_\\])', lambda m: chr(ord(m.group(1)) - 64), s)
    return s

def run(editor, case, workdir):
    path = os.path.join(workdir, 'f.txt')
    with open(path, 'w', encoding='latin-1') as f:
        f.write(''.join(l + '\n' for l in case['text']))
    t = Term(EDITORS[editor] + [path], cwd=workdir, env={'HOME': workdir, 'SHELL': '/bin/sh'})
    try:
        t.settle(0.3)
        for k in case['keys']:
            t.keys(keys_decode(k))
        t.keys('\x1b')
        t.keys(':wq!\r')
        if t.wait(5) is None:
            return 'TIMEOUT'
    finally:
        t.kill()
    return open(path, encoding='latin-1').read()

def main():
    args = sys.argv[1:]
    cf = os.path.join(HERE, 'vicases.txt')
    if args and os.path.exists(args[0]):
        cf = args.pop(0)
    cases = parse(cf)
    if args and args[0] == '--from':
        names = [c['name'] for c in cases]
        cases = cases[names.index(args[1]):]
    elif args:
        cases = [c for c in cases if c['name'] in args]
    work = tempfile.mkdtemp()
    ndiff = 0
    for c in cases:
        res = {}
        for e in EDITORS:
            res[e] = run(e, c, work)
        others = [res['nvi'], res['vim']]
        if res['mine'] in others:
            tag = 'ok' if res['mine'] == res['nvi'] == res['vim'] else 'ok(%s)' % ('nvi' if res['mine'] == res['nvi'] else 'vim')
            print('%-30s %s' % (c['name'], tag))
        else:
            ndiff += 1
            print('%-30s DIFF' % c['name'])
            for e in EDITORS:
                print('   %-5s %r' % (e, res[e]))
    shutil.rmtree(work, ignore_errors=True)
    print('%d differ of %d' % (ndiff, len(cases)))

if __name__ == "__main__":
    main()
