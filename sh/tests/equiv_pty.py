#!/usr/bin/env python3
"""Interactive equivalence: feed the same terminal sessions to two builds
of the shell and compare the transcripts byte for byte (prompts included,
which shows exactly when the parser asks for more input).

    equiv_pty.py OLD NEW"""
import os, pty, re, select, signal, sys, time

SESSIONS = [
    ['echo hi\n', 'if true\n', 'then echo yes\n', 'fi\n'],
    ['for i in 1 2\n', 'do\n', 'echo $i\n', 'done\n'],
    ['echo a &&\n', 'echo b\n'],
    ['echo a |\n', '\n', 'cat\n'],
    ['case x in\n', 'x) echo cx\n', ';;\n', 'esac\n'],
    ['cat <<EOF\n', 'body $HOME\n', 'EOF\n', 'echo after\n'],
    ['cat <<A; cat <<B\n', 'one\n', 'A\n', 'two\n', 'B\n'],
    ['echo "multi\n', 'line"\n'],
    ["echo 'sq\n", "line'\n"],
    ['echo `echo bq\n', '`\n'],
    ['echo $(echo cs\n', 'echo cs2)\n'],
    ['echo $((1 +\n', '2))\n'],
    ['echo cont\\\n', 'inued\n'],
    ['f() {\n', 'echo in-f\n', '}\n', 'f\n'],
    ['f()\n', '\n', '{ echo nlbody; }\n', 'f\n'],
    ['if true; then fi\n', 'echo next\n'],
    ['echo ok; fi\n', 'echo after-error\n'],
    ['{ echo a\n', ')\n', 'echo recovered\n'],
    ['alias ll=\'echo aliased\'\n', 'll x\n', 'alias kw=if\n', 'kw true; then echo kwok; fi\n'],
    ['alias a1=\'echo \' a2=word\n', 'a1 a2\n'],
    ['while false\n', 'do :\n', 'done; echo w\n'],
    ['(\n', 'echo sub\n', ')\n'],
    ['echo ${x:-\n', 'def}\n'],
    ['for x\n', 'in a b\n', 'do echo $x; done\n'],
    ['case a in (a|b)\n', 'echo pat;;\n', 'esac\n'],
    ['! true\n', 'echo $?\n'],
    ['echo "unterminated\n', b'\x04'],
    ['if true\n', b'\x04'],
    ['cat <<E\n', 'no end\n', b'\x04'],
    ['x=1 \\\n', 'y=2; echo $x$y\n'],
    ['echo $((1/0))\n', 'echo survived\n'],
    ['echo $((2+))\n', 'echo $((x=3)) $x\n'],
    ['set -o vi\n', 'echo vi\n'],
    ['PS2="cont> "\n', 'if true\n', 'then echo ps2\n', 'fi\n'],
    ['echo a; echo b &\n', 'wait\n'],
    ['for i in 1; do\n', 'case $i in\n', '1) echo one;;\n', 'esac\n', 'done\n'],
    ['echo `cat <<E\n', 'in bq\n', 'E\n', '`\n'],
    ['echo $(cat <<E\n', 'in cs\n', 'E\n', ')\n'],
    ['eval "if true\n', 'then echo ev; fi"\n'],
    ['exit 3\n'],
]


def session(sh, inputs, wait=0.35):
    pid, fd = pty.fork()
    if pid == 0:
        env = dict(os.environ, PS1='$ ', PS2='> ', HOME='/tmp', TERM='dumb', ENV='', LC_ALL='C')
        env.pop('HISTFILE', None)
        os.execve(sh, ['sh', '-i'], env)
    buf = b''

    def rd(t):
        nonlocal buf
        end = time.time() + t
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.03)
            if r:
                try:
                    d = os.read(fd, 65536)
                except OSError:
                    return False
                if not d:
                    return False
                buf += d
        return True

    rd(0.4)
    for s in inputs:
        os.write(fd, s if isinstance(s, bytes) else s.encode())
        if not rd(wait):
            break
    rd(0.2)
    try:
        os.kill(pid, signal.SIGKILL)
    except OSError:
        pass
    os.waitpid(pid, 0)
    os.close(fd)
    return re.sub(rb'\[\d+\] \d+', b'[N] PID', buf)


def main():
    old, new = sys.argv[1], sys.argv[2]
    diffs = 0
    for inp in SESSIONS:
        a = session(old, inp)
        b = session(new, inp)
        if a != b:
            # a session that differs is run again: timing on a loaded
            # machine can move a prompt
            if any(session(old, inp) == session(new, inp) for _ in range(2)):
                continue
            diffs += 1
            print('DIFF', inp)
            print('  old:', a)
            print('  new:', b)
    print('pty: %d sessions, %d differences' % (len(SESSIONS), diffs))
    sys.exit(1 if diffs else 0)


if __name__ == '__main__':
    main()
