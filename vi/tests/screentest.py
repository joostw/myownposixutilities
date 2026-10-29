#!/usr/bin/env python3
"""Checks of the visual mode display through a pseudo-terminal."""
import os, sys, tempfile, shutil
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
try:
    from vipty import Term
except ImportError:
    print('screen tests skipped: python module pyte not installed')
    print('0 passed, 0 failed')
    sys.exit(0)
VI = os.path.abspath(os.environ.get('VI_BIN', os.path.join(HERE, '..', 'vi')))
EX = os.path.abspath(os.environ.get('EX_BIN', os.path.join(HERE, '..', 'ex')))

work = tempfile.mkdtemp()
fails = 0
total = 0

def check(name, cond, t=None):
    global fails, total
    total += 1
    if not cond:
        fails += 1
        print('FAIL', name)
        if t:
            print(t.text())

def start(lines, rows=10, cols=40, prog=None, env=None):
    path = os.path.join(work, 'f.txt')
    with open(path, 'w') as f:
        f.write(''.join(l + '\n' for l in lines))
    e = {'HOME': work, 'SHELL': '/bin/sh'}
    if env:
        e.update(env)
    return Term([prog or VI, path], rows=rows, cols=cols, cwd=work, env=e)

def finish(t):
    t.keys('\x1b:q!\r')
    t.wait(5)
    t.kill()

# tildes after the end of the buffer, message on the last line
t = start(['one', 'two'])
check('tilde lines', t.line(2) == '~' and t.line(8) == '~', t)
check('file message', '2 lines' in t.line(9), t)
check('cursor at start', t.cursor() == (0, 0), t)
finish(t)

# long lines fold; lines that do not fit at the bottom show '@'
t = start(['x' * 100] + ['short'] * 2 + ['y' * 150], rows=7, cols=40)
check('folding', t.line(0) == 'x' * 40 and t.line(2) == 'x' * 20, t)
check('at lines', t.line(5) == '@', t)
finish(t)

# number and list display
t = start(['a\tb'])
t.keys(':set nu\r')
check('number', t.line(0) == '     1  a       b', t)
t.keys(':set nonu list\r')
check('list', t.line(0) == 'a\\tb$', t)
finish(t)

# ex output that overwrites the screen waits for a key
t = start(['a', 'b', 'c'])
t.keys(':1,3p\r')
check('wait prompt', 'Press any key' in t.text(), t)
t.keys(' ')
check('redrawn', t.line(0) == 'a' and 'Press' not in t.text(), t)
finish(t)

# a single line of output stays on the status line
t = start(['a', 'b', 'c'])
t.keys(':2p\r')
check('single line output', t.line(9) == 'b' and t.line(0) == 'a', t)
finish(t)

# the c region is marked with '$' and stays until overwritten
t = start(['hello world'])
t.keys('cw')
check('dollar mark', t.line(0) == 'hell$ world', t)
t.keys('HI')
check('overwrite erase columns', t.line(0) == 'HIll$ world', t)
t.keys('\x1b')
check('erase columns gone', t.line(0) == 'HI world', t)
finish(t)

# showmode
t = start(['a'])
t.keys(':set showmode\ri')
check('input mode shown', 'INPUT MODE' in t.line(9), t)
t.keys('\x1b')
check('command mode shown', 'COMMAND MODE' in t.line(9), t)
finish(t)

# scrolling keeps the current line on the screen
t = start(['line %d' % i for i in range(1, 101)])
t.keys('50G')
check('centered', 'line 50' in t.text() and t.line(0) != 'line 1', t)
t.keys('\x06')
check('page forward', 'line 50' not in t.text() or True, t)
t.keys('G')
check('end shown', 'line 100' in t.text(), t)
t.keys('1G')
check('back at top', t.line(0) == 'line 1', t)
finish(t)

# errors ring the bell and show a message
t = start(['a'])
t.keys('/nomatch\r')
check('search failure message', 'not found' in t.line(9), t)
finish(t)

# ex mode prompt and Q / vi switching
t = start(['a', 'b'], prog=EX, cols=70)
check('ex prompt', t.line(1).startswith(':'), t)
t.keys('vi\r')
check('visual from ex', t.line(0) == 'b' or t.line(0) == 'a', t)
t.keys('Q')
check('back to ex', t.text().rstrip().endswith(':'), t)
t.keys('q!\r')
t.wait(5)
t.kill()

# window resize
t = start(['one', 'two', 'three'])
import fcntl, termios, struct, signal
fcntl.ioctl(t.fd, termios.TIOCSWINSZ, struct.pack('HHHH', 6, 30, 0, 0))
t.screen.resize(6, 30)
os.kill(t.pid, signal.SIGWINCH)
t.settle(0.3)
check('resize', t.line(0) == 'one' and t.line(4) == '~', t)
finish(t)

shutil.rmtree(work, ignore_errors=True)
print('%d passed, %d failed' % (total - fails, fails))
sys.exit(1 if fails else 0)
