"""patchdiff.py - compare patch with GNU patch, and with diff.

    python3 tests/patchdiff.py ./patch

Random pairs of files are compared with diff in every output format
(normal, -c, -u, -e, with 0, 1 and 3 lines of context), and the
differences applied: to the old file (the result must be the new file,
and -R must bring the old one back), and to versions of the old file
with lines added at the top and context lines changed, where the
result and whether hunks were rejected must agree with GNU patch
(run with --posix).  Needs diff, and GNU patch as "patch" in PATH.
"""

import os
import random
import shutil
import subprocess
import sys
import tempfile


def write(name, lines, eol=True):
    with open(name, "w") as f:
        f.write("\n".join(lines) + ("\n" if eol and lines else ""))


def read(name):
    try:
        with open(name) as f:
            return f.read()
    except OSError:
        return None


def main():
    prog = os.path.abspath(sys.argv[1])
    v = subprocess.run(["patch", "--version"], capture_output=True, text=True).stdout
    if "GNU patch" not in v:
        print("patchdiff: GNU patch is needed")
        sys.exit(2)
    os.chdir(tempfile.mkdtemp())
    rng = random.Random(2024)
    passed = failed = 0

    def check(what, ok):
        nonlocal passed, failed
        if ok:
            passed += 1
        else:
            failed += 1
            print("FAIL", what)

    for it in range(250):
        n = rng.randint(0, 60)
        a = [f"line {rng.randint(0, 20)}" for _ in range(n)]
        b = list(a)
        for _ in range(rng.randint(1, 6)):
            op = rng.random()
            if op < 0.3 and b:
                del b[rng.randrange(len(b))]
            elif op < 0.6:
                b.insert(rng.randint(0, len(b)), f"new {rng.randint(0, 99)}")
            elif b:
                b[rng.randrange(len(b))] = f"chg {rng.randint(0, 99)}"
        ea, eb = rng.random() > 0.1, rng.random() > 0.1
        write("a", a, ea)
        write("b", b, eb)
        for fmt in ["", "-c", "-u", "-e", "-C1", "-U1", "-C0", "-U0"]:
            if fmt == "-e" and not (ea and eb):
                continue
            d = subprocess.run(["diff"] + ([fmt] if fmt else []) + ["a", "b"],
                               capture_output=True, text=True).stdout
            if not d:
                continue
            with open("p", "w") as f:
                f.write(d)
            name = f"{it} diff {fmt or '(normal)'}"
            shutil.copy("a", "t")
            r = subprocess.run([prog, "-i", "p", "t"], capture_output=True)
            check(f"{name}: the new file", r.returncode == 0 and read("t") == read("b"))
            if fmt != "-e":
                r = subprocess.run([prog, "-R", "-i", "p", "t"], capture_output=True)
                check(f"{name}: -R: the old file", r.returncode == 0 and read("t") == read("a"))
            if fmt not in ("-c", "-u", "-C1", "-U1") or not a:
                continue
            # a changed old file: lines at the top, a context line changed
            t = ["top"] * rng.choice([0, 2, 7]) + list(a)
            if rng.random() < 0.5 and len(t) > 2:
                t[rng.randrange(len(t))] = "changed context"
            write("t1", t, ea)
            shutil.copy("t1", "t2")
            r1 = subprocess.run([prog, "-i", "p", "t1"], capture_output=True)
            r2 = subprocess.run(["patch", "--posix", "-s", "-f", "-i", "p", "t2"], capture_output=True)
            same = r1.returncode == r2.returncode and read("t1") == read("t2")
            check(f"{name}, changed: the same as GNU patch (status {r1.returncode} and {r2.returncode})", same)
            keep = os.environ.get("PATCHDIFF_KEEP")
            if not same and keep:
                os.makedirs(keep, exist_ok=True)
                k = f"{keep}/{it}{fmt}"
                shutil.copy("p", k + ".patch")
                write(k + ".old", t, ea)
            for x in os.listdir("."):
                if x.endswith((".rej", ".orig")):
                    os.remove(x)
    print(f"patchdiff: {passed} passed, {failed} failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
