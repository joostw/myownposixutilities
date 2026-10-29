"""tputdiff.py - compare tput with ncurses tput ("tput" in PATH).

    python3 tests/tputdiff.py ./tput

For every terminal entry in the system's terminfo directories and every
capability name tput knows (from terminfo.c), the output and exit status
of both: booleans and numbers, strings as they are, and the strings that
take parameters with as many numbers as they use.  clear, init and reset
are left out (ncurses tput wants a terminal for init and reset, and adds
the extended capability E3 to clear), and so are lines and cols (ncurses
asks the terminal).
"""

import os
import re
import subprocess
import sys

ours = os.path.realpath(sys.argv[1] if len(sys.argv) > 1 else "./tput")
src = open(os.path.join(os.path.dirname(ours), "terminfo.c")).read()
names = []
for arr in ("boolnames", "numnames", "strnames"):
    body = re.search(r"%s\[\] = \{(.*?)\};" % arr, src, re.S).group(1)
    names += re.findall(r'"([^"]+)"', body)
names = [n for n in names if n not in ("lines", "cols", "clear")]

terms = set()
for d in ("/etc/terminfo", "/lib/terminfo", "/usr/share/terminfo"):
    if os.path.isdir(d):
        for sub in os.listdir(d):
            p = os.path.join(d, sub)
            if os.path.isdir(p):
                terms.update(os.listdir(p))

env = dict(os.environ)
env.pop("LINES", None)
env.pop("COLUMNS", None)

def run(prog, args):
    p = subprocess.run([prog] + args, capture_output=True, env=env, stdin=subprocess.DEVNULL)
    return p.stdout, p.returncode

passed = failed = 0
for t in sorted(terms):
    for n in names:
        cases = [[n]]
        a, ra = run(ours, ["-T", t, n])
        # a string with parameters: as many numbers as it uses
        np = max([int(x) for x in re.findall(rb"%p([1-9])", a)] or [0])
        if np:
            cases.append([n] + [str(3 + 2 * i) for i in range(np)])
        for args in cases:
            a, ra = run(ours, ["-T", t] + args)
            b, rb = run("tput", ["-T", t] + args)
            if (a, ra) == (b, rb):
                passed += 1
            else:
                failed += 1
                print("DIFF: tput -T %s %s: %r %d, ours %r %d" % (t, " ".join(args), b, rb, a, ra))
print("%d passed, %d failed (%d terminals)" % (passed, failed, len(terms)))
sys.exit(1 if failed else 0)
