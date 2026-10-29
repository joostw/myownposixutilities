"""ptyrun.py - run a command with some of its descriptors on a terminal.

    python3 ptyrun.py FDS COMMAND...

FDS names the descriptors (of 0, 1 and 2) that are the slave side of a new
pseudo-terminal, which is the controlling terminal of the command (it runs
in a new session); the others are inherited.  What reaches the terminal is
written to the standard output (CR LF as LF); the exit status is the
command's, or 128 plus the number of the signal that ended it.  If
PTYRUN_INPUT is set, it is typed on the terminal first.
"""

import fcntl
import os
import pty
import subprocess
import sys
import termios


def main():
    fds, cmd = sys.argv[1], sys.argv[2:]
    master, slave = pty.openpty()
    kw = {}
    for n, k in ((0, "stdin"), (1, "stdout"), (2, "stderr")):
        if str(n) in fds:
            kw[k] = slave
    p = subprocess.Popen(cmd, start_new_session=True,
                         preexec_fn=lambda: fcntl.ioctl(slave, termios.TIOCSCTTY, 0),
                         **kw)
    os.close(slave)
    if os.environ.get("PTYRUN_INPUT"):
        os.write(master, os.environ["PTYRUN_INPUT"].encode("latin-1"))
    out = b""
    while True:
        try:
            b = os.read(master, 4096)
        except OSError:
            break
        if not b:
            break
        out += b
    sys.stdout.write(out.decode("latin-1").replace("\r\n", "\n"))
    sys.stdout.flush()
    r = p.wait()
    sys.exit(r if r >= 0 else 128 - r)


if __name__ == "__main__":
    main()
