"""fuserdiff.py - compare fuser with psmisc fuser ("fuser" in PATH).

    python3 tests/fuserdiff.py ./fuser

For files the running processes use (/, the working directories, the
programs, the libraries they map and their open files), with no option,
-u and -c: each process ID with its use characters (both run with the
standard error on the standard output, so that each process ID is
followed by its characters).  psmisc pads with blanks (not compared),
writes "(unknown)" for a user ID without a name where POSIX wants the
number, and exits 1 when no process uses a file (not compared either);
-c is used with directories only.
"""

import os
import re
import subprocess
import sys

ours = os.path.realpath(sys.argv[1] if len(sys.argv) > 1 else "./fuser")


def uses(prog, args):
    p = subprocess.run([prog] + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    body = p.stdout.split(":", 1)[1] if ":" in p.stdout else ""
    r = {}
    for pid, chars, user in re.findall(r"(\d+)([a-zA-Z]*)\s*(\([^)]*\))?", body):
        # a user ID without a name: psmisc writes "(unknown)", POSIX the
        # number
        r[pid] = chars + re.sub(r"^\(\d+\)$", "(unknown)", user)
    return r


files = {"/", "/etc/passwd"}
for pid in os.listdir("/proc"):
    if not pid.isdigit():
        continue
    for link in ("exe", "cwd"):
        try:
            files.add(os.readlink("/proc/%s/%s" % (pid, link)))
        except OSError:
            pass
    try:
        for fd in os.listdir("/proc/%s/fd" % pid):
            try:
                files.add(os.readlink("/proc/%s/fd/%s" % (pid, fd)))
            except OSError:
                pass
        for line in open("/proc/%s/maps" % pid):
            parts = line.split()
            if len(parts) >= 6 and parts[5].startswith("/"):
                files.add(parts[5])
    except OSError:
        pass
files = sorted(f for f in files if f.startswith("/") and os.path.exists(f))[:150]

passed = failed = 0
for f in files:
    # -c with a file that is not a directory is unspecified: not compared
    for opt in ([], ["-u"], ["-c"]) if os.path.isdir(f) else ([], ["-u"]):
        a, b = uses(ours, opt + [f]), uses("fuser", opt + [f])
        # the processes of the two runs (each fuser, and what came and
        # went) are not compared
        common = set(a) & set(b)
        if all(a[p] == b[p] for p in common) and len(set(a) ^ set(b)) <= 2:
            passed += 1
        else:
            failed += 1
            diff = {p: (b.get(p), a.get(p)) for p in set(a) | set(b) if a.get(p) != b.get(p)}
            print("DIFF: fuser %s %s: %s" % (" ".join(opt), f, diff))
print("%d passed, %d failed" % (passed, failed))
sys.exit(1 if failed else 0)
