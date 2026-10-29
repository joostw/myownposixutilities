#!/usr/bin/env python3
"""Random differential testing against GNU ed: random files and scripts
made of commands whose behaviour both implementations share.

usage: randdiff.py ED SEED COUNT
"""
import os, random, subprocess, sys, tempfile, shutil

ED = os.path.abspath(sys.argv[1])
REF = '/usr/bin/ed'
rnd = random.Random(int(sys.argv[2]))
COUNT = int(sys.argv[3])
# GNU ed forgets the undo after a failed command, so u is left out by default
UNDO = len(sys.argv) > 4 and sys.argv[4] == 'undo'

WORDS = ['a', 'b', 'ab', 'ba', 'abc', 'o', 'oy', 'foo', 'bar', 'a b', 'b a', '', '$', '\\', '.', 'a.b', 'A', 'B']
RES = ['a', 'b', 'ab', '^a', 'b$', 'a.', '[ab]', '[^a]', 'oy', 'foo', 'o\\{2\\}', '\\(a\\)\\(b\\)', '\\(.\\)\\1',
       'a\\|b', ' ', 'a*b', 'b[a-z]', '^.', '.$', '\\.', '\\$', 'A', '[[:upper:]]', 'oo*', 'o'] 
REPL = ['X', '', '&', '[&]', '\\1', '\\2\\1', '\\&', 'y&y', 'a\\\nb', '%']

def addr(n):
    k = rnd.random()
    if k < 0.3:
        return str(rnd.randint(0, n + 1))
    if k < 0.4:
        return '.'
    if k < 0.5:
        return '$'
    if k < 0.6:
        return rnd.choice(['+', '-', '+2', '-1', '.+1', '.-1', '$-1', '1+1'])
    if k < 0.7:
        return '/%s/' % rnd.choice(RES)
    if k < 0.75:
        return '?%s?' % rnd.choice(RES)
    if k < 0.8 and MARKS[0]:
        return "'" + rnd.choice('ab')
    return ''

def addrs(n):
    k = rnd.random()
    if k < 0.35:
        return ''
    if k < 0.65:
        return addr(n)
    sep = rnd.choice([',', ';'])
    return addr(n) + sep + addr(n)

def text():
    return ''.join(rnd.choice(WORDS) + '\n' for _ in range(rnd.randint(0, 3))) + '.\n'

def command(n, in_global=False):
    k = rnd.randrange(22)
    a = addrs(n)
    suf = rnd.choice(['', '', '', 'p', 'n', 'l'])
    if k == 0:
        return a + rnd.choice('pnl') + '\n'
    if k == 1:
        return a + 'd' + suf + '\n'
    if k == 2 and not in_global:
        return a + rnd.choice('ai') + suf + '\n' + text()
    if k == 3 and not in_global:
        return a + 'c' + suf + '\n' + text()
    if k == 4:
        return a + 'j' + suf + '\n'
    if k == 5:
        return a + rnd.choice('mt') + addr(n) + suf + '\n'
    if k in (6, 7, 8):
        re_ = rnd.choice(RES)
        rep = rnd.choice(REPL)
        if '\\' in rep and rep[1:2].isdigit() and '\\(' not in re_:
            rep = '&'
        if in_global and '\n' in rep:
            rep = 'Z'
        fl = rnd.choice(['', 'g', '2', 'p', 'gp', 'n', 'l', '3'])
        if in_global:
            # GNU ed prints the line for these flags even when nothing was substituted
            fl = fl.replace('p', '').replace('n', '').replace('l', '')
        if 'g' in fl and re_ in ('a*b',) :
            fl = fl.replace('g', '')
        return a + 's/%s/%s/%s\n' % (re_, rep, fl)
    if k == 9 and MARKS[0]:
        return a + 'k' + rnd.choice('ab') + '\n'
    if k == 10 and not in_global and UNDO:
        seen_u[0] = 1
        return 'u' + suf + '\n'
    if k == 11 and not in_global:
        re_ = rnd.choice(RES)
        cmd = command(n, True).rstrip('\n')
        if '\n' in cmd:
            cmd = 'p'
        return a + rnd.choice('gv') + '/%s/%s\n' % (re_, cmd)
    if k == 12:
        return a + '=\n'
    if k == 13:
        return a + '\n'
    if k == 14 and not in_global:
        return a + 'w out\n'
    if k == 15 and not in_global:
        return a + 'r f\n'
    if k == 16 and not in_global:
        return 'w\n'
    if k == 17 and not in_global:
        return a + 's/%s/%s\n' % (rnd.choice(RES), rnd.choice(['Q', 'R', '']))
    if k == 18 and not in_global:
        return a + 'g/%s/s/%s/%s/\\\n.p\n' % (rnd.choice(RES), rnd.choice(RES), rnd.choice(['1', '2', '&&']))
    if k == 19:
        return '/%s\n' % rnd.choice(RES)
    if k == 20 and not in_global and not seen_u[0]:
        return 'e\nE\n'
    return a + 'p\n'

def run(ed, d, script):
    r = subprocess.run([ed, '-s', 'f'], input=script.encode(), cwd=d, capture_output=True, timeout=20,
                       env=dict(os.environ, LC_ALL='C'))
    files = {n: open(os.path.join(d, n), 'rb').read() for n in sorted(os.listdir(d))}
    return r.stdout, r.returncode, files

seen_u = [0]
MARKS = [0]
bad = 0
base = tempfile.mkdtemp(prefix='edrand.')
for it in range(COUNT):
    n = rnd.randint(0, 8)
    content = ''.join(rnd.choice(WORDS) + '\n' for _ in range(n))
    seen_u[0] = 0
    # marks are set first: GNU ed treats an unset mark as line 0 in an empty buffer
    MARKS[0] = n > 0
    script = ('1ka\n$kb\n' if n else '') + ''.join(command(n) for _ in range(rnd.randint(1, 15))) + 'Q\n'
    res = []
    for ed in (ED, REF):
        d = os.path.join(base, 'x')
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
        open(os.path.join(d, 'f'), 'w').write(content)
        res.append(run(ed, d, script))
    if res[0] != res[1]:
        bad += 1
        print('=== DIFF %d' % it)
        print('file:', repr(content))
        print('script:', repr(script))
        print('mine:', res[0][0][:600], res[0][1])
        print('ref: ', res[1][0][:600], res[1][1])
        if res[0][2] != res[1][2]:
            print('files differ', res[0][2], res[1][2])
        if bad > 8:
            break
shutil.rmtree(base, ignore_errors=True)
print('done, %d differences in %d' % (bad, it + 1))
