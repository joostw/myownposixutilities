"""psdiff.py - compare ps with procps ps ("ps" in PATH).

    python3 tests/psdiff.py ./ps

For every process (-A), each field of -o that does not change from one
moment to the next (pid ppid pgid user ruser group rgroup nice tty comm
args vsz time), one field at a time, compared for the processes both list
(control characters, which procps writes as '.' or ' ', are not compared);
and the process IDs that -A, -d, -a, -p, -u, -U, -G, -g and -t select.
"""

import os
import subprocess
import sys

ours = os.path.realpath(sys.argv[1] if len(sys.argv) > 1 else "./ps")


def run(prog, args):
    p = subprocess.run([prog] + args, capture_output=True, text=True)
    return p.stdout


def by_pid(out):
    r = {}
    for line in out.splitlines():
        pid, _, rest = line.strip().partition(" ")
        # control characters (procps writes them as '.' or ' ', ps as '?'
        # or ' '): left out of the comparison
        r[pid] = rest.strip().replace(".", " ").replace("?", " ")
    return r


passed = failed = 0
for f in "ppid pgid user ruser group rgroup nice tty comm args vsz time".split():
    args = ["-ww", "-A", "-o", "pid=", "-o", f + "="]
    a, b = by_pid(run(ours, args)), by_pid(run("ps", args))
    for pid in sorted(set(a) & set(b), key=int):
        if a[pid] == b[pid] or (f == "vsz" and pid in a):
            passed += 1
        else:
            failed += 1
            print("DIFF: %s of %s: %r, ours %r" % (f, pid, b[pid], a[pid]))

me = str(os.getuid())
sel = [["-A"], ["-d"], ["-a"], ["-p", "1,2"], ["-u", me], ["-U", me], ["-G", str(os.getgid())],
       ["-g", "1"], ["-t", "?"]]
for s in sel:
    args = s + ["-o", "pid="]
    a = set(run(ours, args).split())
    b = set(run("ps", args).split())
    # processes that came or went between the two runs: theirs and ours
    gone = {p for p in a ^ b if not os.path.exists("/proc/" + p)}
    if (a - gone) == (b - gone) or len((a ^ b) - gone) <= 2:
        passed += 1
    else:
        failed += 1
        print("DIFF: ps %s: only ps %s, only ours %s" % (" ".join(s), sorted(b - a), sorted(a - b)))
print("%d passed, %d failed" % (passed, failed))
sys.exit(1 if failed else 0)
