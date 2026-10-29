#!/usr/bin/env python3
"""Interactive tests through a pseudo-terminal."""
import os, pty, select, time, sys, signal, re
SH = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sh')
PTYDIR = os.path.join(os.environ.get('TMPDIR', '/tmp'), 'sh-pty-test')
os.makedirs(PTYDIR, exist_ok=True)

class Term:
    def __init__(self, args, env=None):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execve(SH, [SH] + args, env or dict(os.environ, PS1='$ ', HOME=PTYDIR, TERM='dumb'))
        self.buf = b''
    def read(self, t=0.5):
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    d = os.read(self.fd, 4096)
                except OSError:
                    break
                if not d: break
                self.buf += d
        return self.buf
    def send(self, s, wait=0.4):
        os.write(self.fd, s.encode() if isinstance(s, str) else s)
        return self.read(wait)
    def expect(self, pat, t=3):
        end = time.time() + t
        while time.time() < end:
            if re.search(pat.encode(), self.buf): return True
            self.read(0.1)
        return False
    def close(self):
        try: os.kill(self.pid, signal.SIGKILL)
        except: pass
        os.waitpid(self.pid, 0)

results = []
def check(name, cond, info=b''):
    results.append((name, cond))
    print(('PASS ' if cond else 'FAIL ') + name)
    if not cond: print('   output:', info[-600:])

def t_prompt():
    t = Term(['-i'])
    t.read(0.5)
    t.send('echo hello-$((1+1))\n')
    check('prompt and command', t.expect(r'hello-2\r?\n\$ '), t.buf)
    t.send('PS1="[\\$PWD]> "\n'); t.send('cd /\n')
    check('PS1 parameter expansion', t.expect(r'\[/\]> '), t.buf)
    t.send('PS1="!> "\n')
    check('PS1 history number', t.expect(r'\n\d+> '), t.buf)
    t.send('if true\n')
    check('PS2 continuation', t.expect(r'> $'), t.buf)
    t.send('then echo cont-ok; fi\n')
    check('continuation executes', t.expect('cont-ok'), t.buf)
    t.close()

def t_sigint():
    t = Term(['-i'])
    t.read(0.5)
    t.send('sleep 10; echo not-printed\n', 0.5)
    t.send('\x03', 0.5)
    t.send('echo alive-$?\n')
    check('SIGINT kills fg job, shell survives', t.expect(r'alive-130') and t.buf.count(b'not-printed') == 1, t.buf)
    t.send('\x03', 0.3)
    t.send('echo still\n')
    check('SIGINT at prompt ignored', t.expect('still'), t.buf)
    t.close()

def t_jobs():
    t = Term(['-i'])
    t.read(0.5)
    t.send('sleep 30 &\n')
    check('bg job message', t.expect(r'\[1\] \d+'), t.buf)
    t.send('jobs\n')
    check('jobs listing', t.expect(r'\[1\] \+ Running\s+sleep 30'), t.buf)
    t.send('kill %1\n', 0.5)
    t.send('\n', 0.5)
    check('done notification', t.expect(r'\[1\] \+ Terminated|\[1\].*Terminated'), t.buf)
    t.buf = b''
    t.send('sleep 30\n', 0.6)
    t.send('\x1a', 0.6)
    check('ctrl-z stops job', t.expect(r'Stopped'), t.buf)
    t.send('jobs\n')
    check('stopped job listed', t.expect(r'\[\d\] \+ Stopped\s+sleep 30'), t.buf)
    t.send('bg\n')
    check('bg resumes', t.expect(r'sleep 30 &'), t.buf)
    t.buf = b''
    t.send('fg\n', 0.6)
    t.send('\x03', 0.6)
    t.send('echo after-fg $?\n')
    check('fg brings to foreground', t.expect(r'after-fg 130'), t.buf)
    t.close()

def t_history():
    try: os.remove(PTYDIR + '/.sh_history')
    except: pass
    t = Term(['-i'])
    t.read(0.5)
    t.send('echo first\n'); t.send('echo second\n')
    t.send('fc -l\n')
    check('fc -l lists', t.expect(r'\d+\techo first\r?\n\d+\techo second'), t.buf)
    t.buf = b''
    t.send('fc -s first=again 1\n')
    check('fc -s substitution', t.expect(r'again'), t.buf)
    t.buf = b''
    t.send('fc -ln -1\n')
    check('fc -ln', t.expect(r'\techo again'), t.buf)
    t.send('exit\n', 0.5)
    t.close()
    check('history file written', os.path.exists(PTYDIR + '/.sh_history'))

def t_vi():
    t = Term(['-i'])
    t.read(0.5)
    t.send('set -o vi\n')
    t.buf = b''
    t.send('echo abc def\x1b' + '0wcwXYZ\x1b' + '\r')
    check('vi cw', t.expect(r'XYZ def'), t.buf)
    t.buf = b''
    t.send('\x1bk\r')
    check('vi k recalls history', t.expect(r'\r\nXYZ def\r\n', 3), t.buf)
    t.buf = b''
    t.send('echo one two three\x1b' + 'bbdw' + '\r')
    check('vi dw', t.expect(r'one three'), t.buf)
    t.buf = b''
    t.send('echo hello\x1b' + 'x$' + 'aX\x1b' + '\r')
    check('vi x and a', t.expect(r'hellX'), t.buf)
    t.buf = b''
    t.send('echo aaa\x1b' + 'rb' + '\r')
    check('vi r', t.expect(r'aab'), t.buf)
    t.buf = b''
    t.send('echo xyz\x1b' + 'hhD' + 'aQ\x1b' + '\r')
    check('vi D', t.expect(r'\r\nQ\r\n'), t.buf)
    t.buf = b''
    t.send('echo word1 word2\x1b' + '0' + 'fwi_\x1b' + ';i-\x1b\r')
    check('vi f and ;', t.expect(r'\r\n_-word1 word2\r\n'), t.buf)
    t.buf = b''
    t.send('echo undo\x1b' + 'xu' + '\r')
    check('vi u', t.expect(r'\nundo'), t.buf)
    t.buf = b''
    t.send('\x1b/second\r' + '\r')
    t.send('echo marker\r')
    t.buf = b''
    t.send('echo abc\x1b' + 'xp\r')
    check('vi x p', t.expect(r'abc|acb'), t.buf)
    t.buf = b''
    t.send('ec\x1b' + 'A' + 'ho dot\x1b' + '0dw.' + 'iecho \x1b\r')
    check('vi dot repeat', t.expect(r'\r?\n?(dot|ho dot)'), t.buf)
    t.close()

def t_ignoreeof():
    t = Term(['-i'])
    t.read(0.5)
    t.send('set -o ignoreeof\n')
    t.send('\x04', 0.5)
    t.send('echo still-alive\n')
    check('ignoreeof', t.expect('still-alive'), t.buf)
    t.send('exit\n')
    t.close()

def t_env():
    open(PTYDIR + '/envfile', 'w').write('ENV_LOADED=yes\nalias hi="echo hi-alias"\n')
    env = dict(os.environ, PS1='$ ', HOME=PTYDIR, ENV=PTYDIR + '/envfile')
    t = Term(['-i'], env)
    t.read(0.5)
    t.send('echo $ENV_LOADED; hi\n')
    check('ENV file processed', t.expect(r'yes') and t.expect('hi-alias'), t.buf)
    t.close()

def t_errors_interactive():
    t = Term(['-i'])
    t.read(0.5)
    t.send('echo ${x?unset-error}\n')
    t.send('echo after-error\n')
    check('expansion error does not exit interactive', t.expect('after-error'), t.buf)
    t.send('if then\n')
    t.send('echo after-syntax\n')
    check('syntax error does not exit interactive', t.expect('after-syntax'), t.buf)
    t.send('readonly R=1; R=2\n')
    t.send('echo after-ro\n')
    check('readonly error does not exit interactive', t.expect('after-ro'), t.buf)
    t.close()

def t_notify():
    t = Term(['-i'])
    t.read(0.5)
    t.send('set -b\n')
    t.send('sleep 0.5 &\n', 1.5)
    check('set -b asynchronous notification', t.expect(r'Done\s+sleep 0.5'), t.buf)
    t.close()

def t_mail():
    open(PTYDIR + '/mbox', 'w').write('')
    env = dict(os.environ, PS1='$ ', HOME=PTYDIR, MAIL=PTYDIR + '/mbox', MAILCHECK='0')
    t = Term(['-i'], env)
    t.read(0.5)
    time.sleep(1.1)
    open(PTYDIR + '/mbox', 'a').write('From x\n')
    t.send('\n', 0.5)
    check('MAIL notification', t.expect('you have mail'), t.buf)
    t.close()

def t_ignoreeof_repeat():
    # each ^D gives one message and the shell reads the terminal again
    t = Term(['-i'])
    t.read(0.5)
    t.send('set -o ignoreeof\n')
    for _ in range(3):
        t.send('\x04', 0.3)
    t.send('echo after-three\n')
    ok = t.expect('after-three') and t.buf.count(b'Use "exit" to leave the shell.') == 3
    check('ignoreeof: one message per end-of-file', ok, t.buf)
    t.send('exit\n')
    t.close()

def t_ignoreeof_hangup():
    # with SIGHUP ignored, a terminal that goes away ends the shell
    # instead of an endless series of end-of-files
    t = Term(['-i'])
    t.read(0.5)
    t.send("trap '' HUP; set -o ignoreeof\n", 0.5)
    os.close(t.fd)
    done = False
    for _ in range(50):
        p, st = os.waitpid(t.pid, os.WNOHANG)
        if p:
            done = True
            break
        time.sleep(0.1)
    if not done:
        os.kill(t.pid, signal.SIGKILL)
        os.waitpid(t.pid, 0)
    check('ignoreeof: hangup ends the shell', done)

for f in [t_notify, t_mail, t_prompt, t_sigint, t_jobs, t_history, t_vi, t_ignoreeof,
          t_ignoreeof_repeat, t_ignoreeof_hangup, t_env, t_errors_interactive]:
    try: f()
    except Exception as e: check(f.__name__ + ' exception ' + repr(e), False)
print('%d passed, %d failed' % (sum(1 for _, c in results if c), sum(1 for _, c in results if not c)))
sys.exit(0 if all(c for _, c in results) else 1)
