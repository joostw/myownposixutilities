#!/bin/sh
# test_more.sh - tests for more (POSIX.1-2024).  Run: sh test_more.sh
# The terminal tests drive more on a pseudo-terminal and need python3 with pyte.
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
# tb NAME EXPECTED-OD-C COMMAND...  (exact bytes of stdout, as od -c)
tb() {
    _n=$1 _e=$2; shift 2
    _o=$("$@" 2>/dev/null | od -An -c | tr -s ' \n' ' ' | sed 's/^ //;s/ $//')
    if [ "$_o" = "$_e" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  %s\n  want: %s\n' "$_n" "$_o" "$_e"; fi
}
# ok NAME CONDITION-COMMAND...
ok() {
    _n=$1; shift
    if "$@"; then pass=$((pass+1)); else fail=$((fail+1)); printf 'FAIL %s\n' "$_n"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/more}
# a UTF-8 locale for the multibyte tests, which are skipped without one (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any UTF-8 locale that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
export U8
T=${TMPDIR:-/tmp}/test_more.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
i=1; while [ $i -le 30 ]; do printf 'l%02d\n' $i; i=$((i+1)); done > f30
printf 'a\n\n\n\nb\n' > blanks

# ---- standard output not a terminal: a copy ----
t "filter copy" "$(cat f30)" 0 "$U" f30
t "filter two files" "$(cat f30 blanks)" 0 "$U" f30 blanks
t "filter -s" "a

b" 0 "$U" -s blanks
t "filter other options ignored" "$(cat f30)" 0 "$U" -n 3 -e -c f30
t "filter stdin" "x" 0 sh -c 'echo x | "$1"' sh "$U"
t "filter -" "a
x
a" 0 sh -c 'echo x | "$1" blanks - blanks | grep -v "^$" | grep -v b' sh "$U"
tb "filter keeps bytes" 'a \b b \r \n 001' sh -c 'printf "a\bb\r\n\001" > raw; "$1" raw' sh "$U"
t "filter -s: a 1-byte last line is not blank" "x" 0 sh -c 'printf "\n\nx" > lb; "$1" -s lb | tail -n 1' sh "$U"
t "filter missing file" "x" 1 sh -c 'echo x > ok; "$1" nofile ok' sh "$U"
[ -c /dev/full ] && t "filter write error" "" 1 sh -c '"$1" f30 > /dev/full' sh "$U"
t "bad -n" "" 1 "$U" -n 0 f30
t "bad option" "" 1 "$U" -z f30

# ---- on a terminal ----
if ! python3 -c 'import pyte' 2>/dev/null; then
    echo "more: terminal tests skipped (python3 pyte not available)"
else
cat > pt.py <<'PY'
import os, pty, sys, time, select, struct, fcntl, termios, signal, pyte
MORE = os.environ['U']

class Run:
    def __init__(self, args, rows=10, cols=40, env=None, shell=None):
        self.rows, self.cols = rows, cols
        pid, fd = pty.fork()
        if pid == 0:
            fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
            for k in ('LINES', 'COLUMNS', 'MORE'):
                os.environ.pop(k, None)
            os.environ['TERM'] = 'xterm'
            os.environ.update(env or {})
            if shell:
                os.execv('/bin/sh', ['/bin/sh', '-c', shell])
            os.execv(MORE, [MORE] + args)
        self.pid, self.fd = pid, fd
        self.screen = pyte.Screen(cols, rows)
        self.stream = pyte.ByteStream(self.screen)
        self.raw = b''
        self.status = None
        self.drain()
    def drain(self, quiet=0.25, limit=3.0):
        end = time.time() + limit
        last = time.time()
        while time.time() < end and time.time() - last < quiet:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                try:
                    d = os.read(self.fd, 65536)
                except OSError:
                    break
                if not d:
                    break
                self.raw += d
                self.stream.feed(d)
                last = time.time()
    def key(self, k):
        self.mark = len(self.raw)
        os.write(self.fd, k.encode() if isinstance(k, str) else k)
        self.drain()
        return self
    def since(self):
        return self.raw[self.mark:]
    def lines(self):
        return [l.rstrip() for l in self.screen.display]
    def text(self, n=None):
        l = self.lines()
        return l[:n] if n else l
    def prompt(self):
        return self.lines()[self.screen.cursor.y]
    def done(self, timeout=2.0):
        end = time.time() + timeout
        while time.time() < end:
            p, st = os.waitpid(self.pid, os.WNOHANG)
            if p:
                self.status = st
                self.drain(0.1, 0.5)
                return True
            self.drain(0.05, 0.1)
        return False
    def kill(self):
        try:
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
        except OSError:
            pass

fails = 0
passes = 0
def check(name, cond, info=''):
    global fails, passes
    if cond:
        passes += 1
    else:
        fails += 1
        print('FAIL', name, info)

def L(a, b):
    return ['l%02d' % i for i in range(a, b + 1)]

# first screen: rows-1 lines and a prompt with the name
r = Run(['f30'])
check('first screen', r.text(9) == L(1, 9), r.text())
check('prompt names the file', 'f30' in r.prompt(), r.prompt())
r.key(' ')
check('space: next screenful', r.text(9) == L(10, 18), r.text())
check('space scrolls (no clear)', b'\x1b[2J' not in r.since() and b'\x1b[H' not in r.since(), r.since()[:80])
r.key('b')
check('b: back a screenful', r.text(9) == L(1, 9), r.text())
r.key('j')
check('j: one line', r.text(9) == L(2, 10), r.text())
r.key('k')
check('k: back one line', r.text(9) == L(1, 9), r.text())
r.key('k')
check('k at the top: alert, no change', r.text(9) == L(1, 9) and b'\a' in r.since())
r.key('3j')
check('3j', r.text(9) == L(4, 12), r.text())
r.key('2k')
check('2k (reverse scroll)', r.text(9) == L(2, 10), r.text())
r.key('15g')
check('15g', r.text(9) == L(15, 23), r.text())
r.key('g')
check('g', r.text(9) == L(1, 9), r.text())
r.key('G')
check('G: last screenful', r.text(9) == L(22, 30), r.text())
check('prompt at EOF', 'END' in r.prompt(), r.prompt())
r.key('q'); r.done(); r.kill()
r = Run(['f30'])
r.key('d')
check('d: half a screen', r.text(9) == L(5, 13), r.text())
r.key('u')
check('u', r.text(9) == L(1, 9), r.text())
r.key('2d')
check('2d', r.text(9) == L(3, 11), r.text())
r.key('d')
check('d: count is the new default', r.text(9) == L(5, 13), r.text())
r.key('g').key('s')
check('s: the screen count lines after the last', r.text(9) == L(10, 18), r.text())
r.key('g').key('/l2\n')
check('/pattern', r.text(9) == L(20, 28), r.text())
r.key('n')
check('n', r.text(9) == L(21, 29), r.text())
r.key('N')
check('N', r.text(9) == L(20, 28), r.text())
r.key('?l1\n')
check('?pattern', r.text(9) == L(19, 27), r.text())
r.key('g').key('/!l0\n')
check('/!pattern', r.text(9) == L(10, 18), r.text())
r.key('g').key('/zzz\n')
check('not found: message', 'not found' in r.prompt(), r.prompt())
r.key('x')
check('not found: screen unchanged', r.text(9) == L(1, 9), r.text())
r.key('12g').key('ma').key('g').key("'a")
check("mark and return", r.text(9) == L(12, 20), r.text())
r.key('g').key('15g').key("''")
check("'' returns before the large move", r.text(9) == L(1, 9), r.text())
r.key('=')
check('= shows the position', 'f30' in r.prompt() and 'line 10' in r.prompt(), r.prompt())
r.key('x').key('q')
check('q exits', r.done() and os.waitstatus_to_exitcode(r.status) == 0)
check('terminal restored', termios.tcgetattr(r.fd)[3] & termios.ICANON != 0)
r.kill()

# -n: lines per screenful, one of them the prompt
r = Run(['-n', '5', 'f30'])
check('-n 5: 4 lines', r.text(4) == L(1, 4), r.text())
check('-n 5: prompt below them', r.screen.cursor.y == 4, r.screen.cursor.y)
r.key('q'); r.done(); r.kill()

# MORE and LINES
r = Run(['f30'], env={'MORE': '-n 4'})
check('MORE=-n 4', r.text(3) == L(1, 3) and r.screen.cursor.y == 3, r.text())
r.key('q'); r.done(); r.kill()
r = Run(['f30'], env={'LINES': '6'})
check('LINES=6', r.text(5) == L(1, 5) and r.screen.cursor.y == 5, r.text())
r.key('q'); r.done(); r.kill()
r = Run(['-n', '4', 'f30'], env={'LINES': '6'})
check('-n over LINES', r.text(3) == L(1, 3) and r.screen.cursor.y == 3, r.text())
r.key('q'); r.done(); r.kill()

# -e: exit after the last line of the last file
r = Run(['-e', 'blanks'])
check('-e exits after a short file', r.done() and os.waitstatus_to_exitcode(r.status) == 0)
r.kill()
r = Run(['-e', 'f30'])
r.key('G')
check('-e exits at the end', r.done())
r.kill()

# the end of the last file: any other command exits
r = Run(['blanks'])
check('short file: END prompt', 'END' in r.prompt(), r.prompt())
r.key('b')
check('at the end of the last file, b exits', r.done())
r.kill()

# two files
open('two', 'w').write('second\n')
r = Run(['blanks', 'two'])
check('EOF prompt names the next file', 'two' in r.prompt() and 'END' in r.prompt(), r.prompt())
r.key(' ')
check('space goes to the next file', r.text(1) == ['second'], r.text())
r.kill()
import shutil
shutil.copy('f30', 'g30')
r = Run(['f30', 'g30'])
r.key(':n')
check(':n', r.text(1) == ['l01'] and 'g30' in r.prompt(), r.prompt())
r.key(':p')
check(':p', 'f30' in r.prompt(), r.prompt())
r.key('2:n')
check('2:n beyond the last: message', 'no next' in r.prompt(), r.prompt())
r.key('x')
r.key(':e blanks\n')
check(':e file', r.text(1) == ['a'], r.text())
r.kill()
r = Run(['f30', 'g30'])
r.key(':n').key(':e #\n')
check(':e #', 'f30' in r.prompt(), r.prompt())
r.key('5g').key(':e nosuch\n')
check(':e inaccessible: message', 'nosuch' in r.prompt(), r.prompt())
r.key('x')
check(':e inaccessible: file and screen unchanged', r.text(9) == L(5, 13) and 'f30' in r.prompt(), r.text())
r.key('q'); r.done(); r.kill()

# a missing file among others: the status shows it
r = Run(['nosuch', 'two'])
check('missing file skipped', r.text(3)[-1] == 'second' or 'second' in r.text(), r.text())
r.key('q')
check('missing file: status 1', r.done() and os.waitstatus_to_exitcode(r.status) == 1, r.status)
r.kill()

# characters
open('chars', 'wb').write(b'a\x01b\x7fc\n\xff\nx\ty\n' + b'b\x08bold\n' + b'_\x08u\n'
                          + 'é日本\n'.encode() + b'q' * 50 + b'\nend\r\n')
U8 = os.environ.get('U8', '')
r = Run(['chars'], env={'LC_ALL': U8 or 'C'})
t = r.text()
check('control chars as ^X', t[0] == 'a^Ab^?c', t[0])
check('a byte that is not a character as \\ooo', t[1] == '\\377', t[1])
check('tab to column 8', t[2] == 'x       y', repr(t[2]))
check('overstrike bold', t[3] == 'bold' and r.screen.buffer[3][0].bold, t[3])
check('overstrike underline', t[4] == 'u' and r.screen.buffer[4][0].underscore, t[4])
if U8:
    check('wide characters', t[5] == 'é日本', t[5])
check('long line folded', t[6] == 'q' * 40 and t[7] == 'q' * 10, t[6:8])
check('CR at the end ignored', t[8] == 'end', repr(t[8]))
r.key('q'); r.done(); r.kill()
r = Run(['-u', 'chars'], env={'LC_ALL': U8 or 'C'})
t = r.text()
check('-u: backspace shown', t[3] == 'b^Hbold', t[3])
check('-u: CR shown', t[8] == 'end^M', t[8])
r.key('q'); r.done(); r.kill()

# -s on a terminal
r = Run(['-s', 'blanks'])
check('-s squeezes', r.text(3) == ['a', '', 'b'], r.text())
r.key('q'); r.done(); r.kill()

# a pipe that does not end is paged as it arrives
r = Run([], shell='yes | "$U"', env={'U': MORE})
check('yes | more: first screen', r.text(9) == ['y'] * 9, r.text())
r.key(' ')
check('yes | more: forward', r.text(9) == ['y'] * 9 and 'END' not in r.prompt())
r.key('q')
check('yes | more: q', r.done())
r.kill()

# -p and -t
r = Run(['-p', 'G', 'f30'])
check('-p G', r.text(9) == L(22, 30), r.text())
check('-p: no intermediate screen', b'l01' not in r.raw, r.raw[:60])
r.key('q'); r.done(); r.kill()
r = Run(['-p', '/l15\n', 'f30'])
check('-p with a pattern', r.text(9) == L(15, 23), r.text())
r.key('q'); r.done(); r.kill()
open('tags', 'w').write('mytag\tf30\t/^l12$/\nnum\tf30\t20\n')
r = Run(['-t', 'mytag'])
check('-t pattern tag', r.text(9) == L(12, 20), r.text())
r.key(':tnum\n')
check(':t line tag', r.text(9) == L(20, 28), r.text())
r.key('q'); r.done(); r.kill()
r = Run(['-t', 'nosuchtag'])
check('-t missing tag: error', r.done() and os.waitstatus_to_exitcode(r.status) == 1)
r.kill()

# v: the editor with -c line
open('vi', 'w').write('#!/bin/sh\necho "$@" > edargs\n')
os.chmod('vi', 0o755)
r = Run(['f30'], env={'EDITOR': os.path.abspath('vi')})
r.key('5g').key('v')
time.sleep(0.3)
check('v runs vi -c line file', open('edargs').read().strip() == '-c 5 f30', open('edargs').read() if os.path.exists('edargs') else 'none')
check('v: same screen after', r.text(9) == L(5, 13), r.text())
r.key('q'); r.done(); r.kill()

# h: help, then back
r = Run(['f30'], rows=30, cols=80)
r.key('h')
check('h shows help', any('search' in l for l in r.text()), r.text())
r.key(' ')
check('h: back to the file', r.text(3) == L(1, 3), r.text())
r.key('q'); r.done(); r.kill()

# SIGWINCH: the new size is used
r = Run(['f30'])
fcntl.ioctl(r.fd, termios.TIOCSWINSZ, struct.pack('HHHH', 6, 40, 0, 0))
r.screen.resize(6, 40)
os.kill(r.pid, signal.SIGWINCH)
r.drain()
r.key('r')
check('SIGWINCH: smaller screen', r.text(5) == L(1, 5), r.text())
r.key('q'); r.done(); r.kill()

# SIGINT: default action, terminal restored first
r = Run(['f30'])
os.write(r.fd, b'\x03')
r.done()
check('^C: killed by SIGINT', r.status is not None and os.WIFSIGNALED(r.status) and os.WTERMSIG(r.status) == signal.SIGINT, r.status)
check('^C: terminal restored', termios.tcgetattr(r.fd)[3] & termios.ECHO != 0)
r.kill()

print('PASSES %d FAILS %d' % (passes, fails))
sys.exit(1 if fails else 0)
PY
    out=$(U="$U" python3 pt.py 2>&1); rc=$?
    np=$(printf '%s\n' "$out" | sed -n 's/^PASSES \([0-9]*\) FAILS \([0-9]*\)$/\1/p')
    nf=$(printf '%s\n' "$out" | sed -n 's/^PASSES \([0-9]*\) FAILS \([0-9]*\)$/\2/p')
    printf '%s\n' "$out" | grep -v '^PASSES '
    pass=$((pass + ${np:-0})); fail=$((fail + ${nf:-1}))
    [ "$rc" = 0 ] || [ -n "$nf" ] || fail=$((fail + 1))
fi
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: more f -c: -c is a file" "x" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f -c; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests more
