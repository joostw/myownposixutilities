#!/usr/bin/env python3
"""Interactive tests of bc on a pseudo-terminal.

usage: pty_tests.py BC
"""
import os, pty, re, select, signal, sys, time

BC = os.path.abspath(sys.argv[1])
FAILS = 0
PASSES = 0

class Session:
    def __init__(self, args=()):
        self.out = b''
        self.pos = 0
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execv(BC, [BC] + list(args))

    def read(self, t=0.05):
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.02)
            if r:
                try:
                    data = os.read(self.fd, 65536)
                except OSError:
                    return
                if not data:
                    return
                self.out += data

    def expect(self, pattern, timeout=5):
        rx = re.compile(pattern.encode())
        end = time.time() + timeout
        while time.time() < end:
            m = rx.search(self.out, self.pos)
            if m:
                self.pos = m.end()
                return
            self.read(0.05)
        raise AssertionError('expected %r, got %r' % (pattern, self.out[self.pos:][-300:]))

    def send(self, s):
        os.write(self.fd, s.encode())

    def wait(self, timeout=5):
        end = time.time() + timeout
        while time.time() < end:
            self.read(0.05)
            p, st = os.waitpid(self.pid, os.WNOHANG)
            if p:
                return st
        os.kill(self.pid, signal.SIGKILL)
        os.waitpid(self.pid, 0)
        raise AssertionError('bc did not exit')

def test(fn):
    global FAILS, PASSES
    try:
        fn()
        print('ok  ', fn.__name__)
        PASSES += 1
    except AssertionError as e:
        FAILS += 1
        print('FAIL', fn.__name__, '-', e)

def immediate_output():
    s = Session()
    s.send('2+3\n')
    s.expect(r'\n5\r?\n')
    s.send('x = 4; x * x\n')
    s.expect(r'\n16\r?\n')
    s.send('quit\n')
    s.wait()

def interrupt_loop():
    s = Session()
    s.send('i = 0; while (1) { i = i + 1 }\n')
    time.sleep(0.5)
    os.kill(s.pid, signal.SIGINT)
    s.read(0.5)
    s.send('7 * 6\n')
    s.expect(r'\n42\r?\n')
    s.send('quit\n')
    st = s.wait()
    assert os.WIFEXITED(st), st

def error_recovery():
    s = Session()
    s.send('1 +\n')
    s.read(0.3)
    s.send('1/0\n')
    s.read(0.3)
    s.send('8 + 1\n')
    s.expect(r'\n9\r?\n')
    s.send('quit\n')
    s.wait()

def eof_exits():
    s = Session()
    s.send('5\n')
    s.expect(r'\n5\r?\n')
    s.send('\x04')
    st = s.wait()
    assert os.WIFEXITED(st), st

def function_across_lines():
    s = Session(['-l'])
    s.send('define f(x) {\n')
    s.send('  return (x * 10)\n')
    s.send('}\n')
    s.send('f(4); scale\n')
    s.expect(r'\n40\r?\n20\r?\n')
    s.send('quit\n')
    s.wait()

for t in [immediate_output, interrupt_loop, error_recovery, eof_exits, function_across_lines]:
    test(t)
print('%d passed, %d failed' % (PASSES, FAILS))
sys.exit(1 if FAILS else 0)
