#!/usr/bin/env python3
"""Interactive tests for mailx through a pseudo-terminal.

usage: pty_tests.py MAILX
Each test starts mailx on a pty in a fresh directory, types input and
waits for expected output.  Prints ok/FAIL per test.
"""
import os, pty, re, select, shutil, signal, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'testlib'))
import testlib

MAILX = os.path.abspath(sys.argv[1])
FAILS = 0
PASSES = 0

FAKESEND = '#!/bin/sh\n{ echo "== sendmail $*"; cat; } >> "$HOME/sent"\n'

class Session:
    def __init__(self, args, home, env=None):
        self.out = b''
        e = {k: v for k, v in os.environ.items() if k not in ('MAILRC', 'MAIL', 'MBOX', 'DEAD', 'PAGER', 'EDITOR')}
        e.update(HOME=home, MAIL=os.path.join(home, 'sys'), MAILRC=os.path.join(home, 'rc'), TERM='dumb',
                 LC_ALL=testlib.utf8_locale() or 'C', TZ='UTC')
        if env:
            e.update(env)
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(home)
            os.execve(MAILX, [MAILX] + args, e)
        self.pos = 0

    def read(self, t=0.05):
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.02)
            if r:
                try:
                    d = os.read(self.fd, 4096)
                except OSError:
                    return
                if not d:
                    return
                self.out += d

    def expect(self, pattern, timeout=3):
        end = time.time() + timeout
        rx = re.compile(pattern.encode() if isinstance(pattern, str) else pattern)
        while time.time() < end:
            m = rx.search(self.out, self.pos)
            if m:
                self.pos = m.end()
                return True
            self.read(0.05)
        raise AssertionError('expected %r, got %r' % (pattern, self.out[self.pos:].decode(errors='replace')))

    def send(self, s):
        os.write(self.fd, s.encode())

    def intr(self):
        os.write(self.fd, b'\x03')

    def eof(self):
        os.write(self.fd, b'\x04')

    def wait(self, timeout=3):
        end = time.time() + timeout
        while time.time() < end:
            self.read(0.05)
            p, st = os.waitpid(self.pid, os.WNOHANG)
            if p:
                self.read(0.1)
                return os.waitstatus_to_exitcode(st)
        os.kill(self.pid, signal.SIGKILL)
        os.waitpid(self.pid, 0)
        raise AssertionError('mailx did not exit; output %r' % self.out.decode(errors='replace'))

def mkhome():
    home = tempfile.mkdtemp(prefix='mailxpty.')
    with open(os.path.join(home, 'fakesend'), 'w') as f:
        f.write(FAKESEND)
    os.chmod(os.path.join(home, 'fakesend'), 0o755)
    with open(os.path.join(home, 'rc'), 'w') as f:
        f.write('set sendmail=%s/fakesend sendwait quiet\n' % home)
    return home

def mkbox(home, n):
    with open(os.path.join(home, 'sys'), 'w') as f:
        for i in range(1, n + 1):
            f.write('From u%d@x Sat Oct 24 10:00:00 2026\nFrom: u%d@x\nSubject: subject %d\n\n' % (i, i, i))
            f.write(''.join('line %d of message %d\n' % (k, i) for k in range(1, 41)))
            f.write('\n')

def sent(home):
    try:
        return open(os.path.join(home, 'sent')).read()
    except FileNotFoundError:
        return ''

def test(fn):
    global FAILS, PASSES
    home = mkhome()
    try:
        fn(home)
        print('ok  ', fn.__name__)
        PASSES += 1
    except AssertionError as e:
        FAILS += 1
        print('FAIL', fn.__name__, '-', str(e)[:600])
    finally:
        shutil.rmtree(home, ignore_errors=True)

# ---------------------------------------------------------------------

def send_subject_prompt(home):
    s = Session(['a@b'], home)
    s.expect('Subject: ')
    s.send('hello there\r')
    s.send('body line\r')
    s.eof()
    s.expect('EOT')
    assert s.wait() == 0
    t = sent(home)
    assert 'Subject: hello there' in t and 'body line' in t, t

def send_askcc_askbcc(home):
    with open(os.path.join(home, 'rc'), 'a') as f:
        f.write('set askcc askbcc\n')
    s = Session(['-s', 'x', 'a@b'], home)
    s.send('text\r')
    s.eof()
    s.expect('Cc: ')
    s.send('c@d\r')
    s.expect('Bcc: ')
    s.send('e@f\r')
    assert s.wait() == 0
    t = sent(home)
    assert 'Cc: c@d' in t and 'e@f' in t.split('\n')[0] and 'Bcc' not in t, t

def send_two_interrupts(home):
    s = Session(['-s', 'x', 'a@b'], home)
    time.sleep(0.2)
    s.send('partial text\r')
    time.sleep(0.2)
    s.intr()
    s.expect(r'Interrupt -- one more to kill letter')
    s.intr()
    rc = s.wait()
    assert rc != 0, rc
    dl = open(os.path.join(home, 'dead.letter')).read()
    assert 'partial text' in dl and dl.startswith('From '), dl
    assert sent(home) == ''

def send_interrupt_then_typing(home):
    s = Session(['-s', 'x', 'a@b'], home)
    time.sleep(0.2)
    s.send('one\r')
    time.sleep(0.1)
    s.intr()
    s.expect(r'one more to kill letter')
    s.send('two\r')
    time.sleep(0.1)
    s.intr()
    s.expect(r'one more to kill letter')
    s.send('three\r')
    s.eof()
    assert s.wait() == 0
    t = sent(home)
    assert 'one\ntwo\nthree' in t, t

def send_ignore_at(home):
    with open(os.path.join(home, 'rc'), 'a') as f:
        f.write('set ignore\n')
    s = Session(['-s', 'x', 'a@b'], home)
    time.sleep(0.2)
    s.send('kept\r')
    s.send('discar')
    time.sleep(0.1)
    s.intr()
    s.expect('@')
    s.send('after\r')
    s.eof()
    assert s.wait() == 0
    t = sent(home)
    assert 'kept\nafter' in t and 'discar' not in t, t

def send_nosave(home):
    with open(os.path.join(home, 'rc'), 'a') as f:
        f.write('unset save\n')
    s = Session(['-s', 'x', 'a@b'], home)
    time.sleep(0.2)
    s.send('text\r')
    time.sleep(0.1)
    s.intr()
    s.expect('one more')
    s.intr()
    s.wait()
    assert not os.path.exists(os.path.join(home, 'dead.letter'))

def send_dot_and_ignoreeof(home):
    with open(os.path.join(home, 'rc'), 'a') as f:
        f.write('set ignoreeof\n')
    s = Session(['-s', 'x', 'a@b'], home)
    time.sleep(0.2)
    s.send('text\r')
    s.eof()
    s.expect('Use "." to terminate letter')
    s.send('more\r.\r')
    assert s.wait() == 0
    assert 'text\nmore\n' in sent(home)

def send_dot(home):
    with open(os.path.join(home, 'rc'), 'a') as f:
        f.write('set dot\n')
    s = Session(['-s', 'x', 'a@b'], home)
    time.sleep(0.2)
    s.send('text\r.\r')
    assert s.wait() == 0
    assert 'text\n' in sent(home) and '\n.\n' not in sent(home)

def receive_prompt_and_interrupt(home):
    mkbox(home, 3)
    s = Session([], home)
    s.expect(r'3 messages 3 new')
    s.expect(r'\? $')
    s.send('p 1\r')
    s.expect(r'line 40 of message 1')
    s.expect(r'\? ')
    s.send('pipe 2 "sleep 5"\r')
    time.sleep(0.4)
    s.intr()
    s.expect(r'\? ', timeout=2)
    s.send('=\r')
    s.expect(r'\r\n2\r\n')
    s.send('q\r')
    assert s.wait() == 0

def receive_reply_interrupted(home):
    mkbox(home, 1)
    s = Session([], home)
    s.expect(r'\? ')
    s.send('r\r')
    s.expect('To: u1@x')
    s.send('draft reply\r')
    time.sleep(0.1)
    s.intr()
    s.expect('one more')
    s.intr()
    s.expect(r'\? ')
    s.send('x\r')
    s.wait()
    assert 'draft reply' in open(os.path.join(home, 'dead.letter')).read()
    assert sent(home) == ''

def receive_no_prompt(home):
    with open(os.path.join(home, 'rc'), 'a') as f:
        f.write('set noprompt\n')
    mkbox(home, 1)
    s = Session([], home)
    s.expect('1 message')
    s.read(0.3)
    assert b'? ' not in s.out, s.out
    s.send('x\r')
    s.wait()

def tilde_h_and_p(home):
    s = Session(['-s', 'subj', 'a@b'], home)
    time.sleep(0.2)
    s.send('body\r~h\r')
    s.expect('To: ')
    s.send('\r' if True else '')
    s.expect('Subject: ')
    s.send('\r')
    s.expect('Cc: ')
    s.send('cc@x\r')
    s.expect('Bcc: ')
    s.send('\r')
    s.send('~p\r')
    s.expect('Message contains:')
    s.expect('Cc: ')
    s.expect(r'\(continue\)')
    s.eof()
    assert s.wait() == 0
    t = sent(home)
    assert 'cc@x' in t and 'Subject: subj' in t and 'To: a@b' in t, t

def tilde_e_editor(home):
    ed = os.path.join(home, 'ed.sh')
    with open(ed, 'w') as f:
        f.write('#!/bin/sh\necho "edited line" >> "$1"\n')
    os.chmod(ed, 0o755)
    s = Session(['-s', 'x', 'a@b'], home, env={'EDITOR': ed})
    time.sleep(0.2)
    s.send('original\r~e\r')
    s.expect(r'\(continue\)')
    s.eof()
    assert s.wait() == 0
    assert 'original\nedited line\n' in sent(home), sent(home)

def pager_with_crt(home):
    mkbox(home, 2)
    pg = os.path.join(home, 'pg.sh')
    with open(pg, 'w') as f:
        f.write('#!/bin/sh\necho "[paged]"; cat\n')
    os.chmod(pg, 0o755)
    with open(os.path.join(home, 'rc'), 'a') as f:
        f.write('set crt=10\n')
    s = Session([], home, env={'PAGER': pg})
    s.expect(r'\? ')
    s.send('p 1\r')
    s.expect(r'\[paged\]')
    s.expect('line 40 of message 1')
    s.send('top 2\r')
    s.read(0.3)
    s.send('x\r')
    s.wait()
    assert s.out.count(b'[paged]') == 1, s.out

def edit_command(home):
    mkbox(home, 1)
    ed = os.path.join(home, 'ed.sh')
    with open(ed, 'w') as f:
        f.write('#!/bin/sh\ngrep -c line "$1"; echo junk >> "$1"\n')
    os.chmod(ed, 0o755)
    s = Session([], home, env={'EDITOR': ed})
    s.expect(r'\? ')
    s.send('edit 1\r')
    s.expect('40')
    s.send('x\r')
    s.wait()
    assert 'junk' not in open(os.path.join(home, 'sys')).read()

def shell_bang_tty(home):
    mkbox(home, 1)
    s = Session([], home)
    s.expect(r'\? ')
    s.send('!echo hi from shell\r')
    s.expect('hi from shell')
    s.expect('!')
    s.send('x\r')
    s.wait()

for t in [send_subject_prompt, send_askcc_askbcc, send_two_interrupts, send_interrupt_then_typing,
          send_ignore_at, send_nosave, send_dot_and_ignoreeof, send_dot, receive_prompt_and_interrupt,
          receive_reply_interrupted, receive_no_prompt, tilde_h_and_p, tilde_e_editor, pager_with_crt,
          edit_command, shell_bang_tty]:
    test(t)
print('%d passed, %d failed' % (PASSES, FAILS))
sys.exit(1 if FAILS else 0)
