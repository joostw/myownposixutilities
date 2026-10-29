#!/usr/bin/env python3
"""Drive a screen editor in a pseudo-terminal with a pyte screen model."""
import os, pty, select, time, signal, fcntl, termios, struct
import pyte

class _Screen(pyte.Screen):
    def select_graphic_rendition(self, *attrs, **kw):
        if kw.get('private'):
            return
        super().select_graphic_rendition(*attrs)


class Term:
    def __init__(self, argv, rows=24, cols=80, env=None, cwd=None):
        self.rows, self.cols = rows, cols
        e = dict(os.environ)
        e.update({'TERM': 'xterm', 'LC_ALL': 'C'})
        for k in ('LINES', 'COLUMNS', 'EXINIT', 'VIMINIT', 'NEXINIT'):
            e.pop(k, None)
        if env:
            e.update(env)
        pid, fd = pty.fork()
        if pid == 0:
            if cwd:
                os.chdir(cwd)
            os.execve(argv[0], argv, e)
        self.pid, self.fd = pid, fd
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
        self.screen = _Screen(cols, rows)
        self.stream = pyte.ByteStream(self.screen)
        self.raw = b''
        self.status = None
        self.settle(0.5)

    def settle(self, quiet=0.15, maxwait=5.0):
        end = time.time() + maxwait
        last = time.time()
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.02)
            if r:
                try:
                    d = os.read(self.fd, 65536)
                except OSError:
                    break
                if not d:
                    break
                self.raw += d
                try:
                    self.stream.feed(d)
                except Exception:
                    pass
                last = time.time()
            elif time.time() - last > quiet:
                break

    def send(self, s, quiet=0.12):
        if isinstance(s, str):
            s = s.encode('latin-1')
        # send in small chunks so that ESC sequences are not glued to keys
        for i in range(0, len(s), 64):
            os.write(self.fd, s[i:i+64])
            time.sleep(0.01)
        self.settle(quiet)

    def keys(self, s, quiet=0.12):
        """send keys one at a time with a pause after each ESC"""
        if isinstance(s, str):
            s = s.encode('latin-1')
        buf = b''
        for ch in s:
            buf += bytes([ch])
            if ch == 0x1b:
                os.write(self.fd, buf)
                buf = b''
                time.sleep(0.06)
        if buf:
            os.write(self.fd, buf)
        self.settle(quiet)

    def text(self):
        return '\n'.join(l.rstrip() for l in self.screen.display)

    def line(self, n):
        return self.screen.display[n].rstrip()

    def cursor(self):
        return (self.screen.cursor.y, self.screen.cursor.x)

    def wait(self, timeout=5):
        end = time.time() + timeout
        while time.time() < end:
            self.settle(0.05, 0.2)
            pid, st = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                self.status = st
                return os.waitstatus_to_exitcode(st)
        os.kill(self.pid, signal.SIGKILL)
        os.waitpid(self.pid, 0)
        return None

    def kill(self):
        try:
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
        except OSError:
            pass
