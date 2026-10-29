#!/usr/bin/env python3
"""Interactive and signal tests for ed through a pseudo-terminal.

usage: pty_tests.py ED
"""
import os, pty, re, select, shutil, signal, sys, tempfile, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'testlib'))
import testlib

ED = os.path.abspath(sys.argv[1])
FAILS = 0
PASSES = 0

class Session:
    def __init__(self, args, d):
        self.out = b''
        self.pos = 0
        env = dict(os.environ, HOME=d, LC_ALL=testlib.utf8_locale() or 'C', TERM='dumb')
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(d)
            os.execve(ED, [ED] + args, env)

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
        raise AssertionError('expected %r, got %r' % (pattern, self.out[self.pos:][-400:]))

    def send(self, s):
        os.write(self.fd, s.encode())

    def kill(self, sig):
        os.kill(self.pid, sig)

    def wait(self, timeout=5):
        end = time.time() + timeout
        while time.time() < end:
            self.read(0.05)
            p, st = os.waitpid(self.pid, os.WNOHANG)
            if p:
                return st
        os.kill(self.pid, signal.SIGKILL)
        os.waitpid(self.pid, 0)
        raise AssertionError('ed did not exit')

def test(fn):
    global FAILS, PASSES
    d = tempfile.mkdtemp(prefix='edpty.')
    try:
        fn(d)
        print('ok  ', fn.__name__)
        PASSES += 1
    except AssertionError as e:
        FAILS += 1
        print('FAIL', fn.__name__, '-', e)
    finally:
        shutil.rmtree(d, ignore_errors=True)

def big(d, n=300000):
    with open(os.path.join(d, 'f'), 'w') as fp:
        for i in range(n):
            fp.write('line %d of a long file\n' % i)

def interrupt_printing(d):
    big(d)
    s = Session(['-p', '*', 'f'], d)
    s.expect(r'\*')
    s.send(',p\n')
    s.expect('line 1000 ')
    s.kill(signal.SIGINT)
    s.expect(r'\?\r?\n')
    s.send('.=\n')
    s.expect(r'\d+\r?\n\*')
    s.send('Q\n')
    s.wait()

def interrupt_input(d):
    open(os.path.join(d, 'f'), 'w').write('one\n')
    s = Session(['-s', 'f'], d)
    s.send('a\ntyped text\n')
    time.sleep(0.2)
    s.kill(signal.SIGINT)
    s.expect(r'\?')
    s.send(',p\n')
    s.expect('one')
    s.send('$=\n')
    s.expect(r'\n1\r?\n')
    s.send('Q\n')
    s.wait()

def interrupt_global(d):
    big(d, 400000)
    s = Session(['-s', 'f'], d)
    time.sleep(0.3)
    s.send('g/line/s/a/A/\n')
    time.sleep(0.15)
    s.kill(signal.SIGINT)
    s.expect(r'\?')
    s.send('Q\n')
    s.wait()

def interrupt_at_prompt(d):
    s = Session(['-p', '> '], d)
    s.expect('> ')
    s.kill(signal.SIGINT)
    s.expect(r'\?\r?\n> ')
    s.send('q\n')
    st = s.wait()
    assert os.WIFEXITED(st), st

def hangup_saves(d):
    open(os.path.join(d, 'f'), 'w').write('original\n')
    s = Session(['-s', 'f'], d)
    s.send('a\nadded\n.\n')
    time.sleep(0.2)
    s.kill(signal.SIGHUP)
    s.wait()
    t = open(os.path.join(d, 'ed.hup')).read()
    assert t == 'original\nadded\n', repr(t)
    assert open(os.path.join(d, 'f')).read() == 'original\n'

def hangup_unmodified(d):
    open(os.path.join(d, 'f'), 'w').write('original\n')
    s = Session(['-s', 'f'], d)
    time.sleep(0.2)
    s.kill(signal.SIGHUP)
    s.wait()
    assert not os.path.exists(os.path.join(d, 'ed.hup'))

def hangup_home(d):
    sub = os.path.join(d, 'ro')
    os.mkdir(sub)
    open(os.path.join(sub, 'f'), 'w').write('x\n')
    os.chmod(sub, 0o555)
    # start ed inside the unwritable directory
    env_d = sub
    os.chmod(sub, 0o555)
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(env_d)
        os.execve(ED, [ED, '-s', 'f'], dict(os.environ, HOME=d))
    time.sleep(0.2)
    os.write(fd, b'a\nmore\n.\n')
    time.sleep(0.2)
    os.kill(pid, signal.SIGHUP)
    os.waitpid(pid, 0)
    os.chmod(sub, 0o755)
    if os.geteuid() == 0:
        # root can write anywhere: the file is in the working directory
        assert os.path.exists(os.path.join(sub, 'ed.hup'))
    else:
        assert open(os.path.join(d, 'ed.hup')).read() == 'x\nmore\n'

def error_flushes_typeahead(d):
    open(os.path.join(d, 'f'), 'w').write('one\ntwo\n')
    s = Session(['-s', 'f'], d)
    time.sleep(0.2)
    # an error, then input already typed: it is discarded
    s.send('nonsense\n1p\n')
    s.expect(r'\?')
    s.read(0.3)
    assert b'one' not in s.out, s.out
    s.send('2p\n')
    s.expect('two')
    s.send('Q\n')
    s.wait()

def quit_ignored(d):
    open(os.path.join(d, 'f'), 'w').write('one\n')
    s = Session(['-s', 'f'], d)
    time.sleep(0.2)
    s.kill(signal.SIGQUIT)
    s.send('p\n')
    s.expect('one')
    s.send('Q\n')
    st = s.wait()
    assert os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0, st

def prompt_toggle(d):
    s = Session([], d)
    time.sleep(0.2)
    s.send('P\n')
    s.expect(r'\*')
    s.send('P\n')
    s.read(0.3)
    s.send('a\nx\n.\nP\n')
    s.expect(r'\*')
    s.send('Q\n')
    s.wait()

def interactive_global(d):
    open(os.path.join(d, 'f'), 'w').write('a1\nb\na2\na3\n')
    s = Session(['-s', 'f'], d)
    s.send('G/a/\n')
    s.expect('a1')
    s.send('s/a/A/\n')
    s.expect('a2')
    s.send('&\n')
    s.expect('a3')
    s.send('\n')
    s.send(',p\n')
    s.expect(r'A1\r?\nb\r?\nA2\r?\na3')
    s.send('Q\n')
    s.wait()

def interactive_global_interrupt(d):
    open(os.path.join(d, 'f'), 'w').write('a1\na2\na3\n')
    s = Session(['-s', 'f'], d)
    s.send('G/a/\n')
    s.expect('a1')
    s.kill(signal.SIGINT)
    s.expect(r'\?')
    s.send('.=\n')
    s.expect(r'\n1\r?\n')
    s.send('Q\n')
    s.wait()

def shell_command(d):
    open(os.path.join(d, 'f'), 'w').write('x\n')
    s = Session(['f'], d)
    s.expect(r'2\r?\n')
    s.send('!echo hello\n')
    s.expect(r'hello\r?\n!\r?\n')
    s.send('!!\n')
    s.expect(r'echo hello\r?\nhello\r?\n!')
    s.send('Q\n')
    s.wait()

for t in [interrupt_printing, interrupt_input, interrupt_global, interrupt_at_prompt, hangup_saves,
          hangup_unmodified, hangup_home, error_flushes_typeahead, quit_ignored, prompt_toggle, interactive_global,
          interactive_global_interrupt, shell_command]:
    test(t)
print('%d passed, %d failed' % (PASSES, FAILS))
sys.exit(1 if FAILS else 0)
