"""csscdiff.py - compare the SCCS utilities with CSSC.

    python3 tests/csscdiff.py DIR [SEED [RUNS]]

DIR holds the programs (admin, delta, get, ... as links to sccs).  Random
histories are made twice, with these utilities and with CSSC (its
programs in $CSSC_DIR, by default /usr/lib/x86_64-linux-gnu/cssc), in two
directories: admin -i, then edits (get -e with -r and -b, changes to the
g-file, delta with comments and -g), rmdel of leaves, unget, flags set
and cleared, and between them get -p (-m, -n, -k, -i, -x, -r of every
kind, -l, -L), prs (-d with every data keyword, -e, -l, -a), sact and val
(its status only: CSSC reports on standard error).  The l-file is
not compared with -i or -x: CSSC leaves those deltas out of it.  delta -g is not
tried: CSSC does not have it.
The standard output, the exit status (zero or not), the g-, p- and
l-files, and the SCCS files up to the body (without the checksum and the
dates and times) must be the same; and every version must be the same,
by CSSC's get and by ours from either file.  (The bodies themselves may
differ where two shortest edit scripts are equally good: CSSC takes
GNU diff's.)  A step where CSSC is killed by a signal (it crashes on
some -x lists) is skipped.  CSSC's informative messages about
missing id keywords go to standard error and are not compared.  (Operands in other directories, no operands, print and sact
are not tried: CSSC's sccs handles them otherwise than the standard
says.)
"""

import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

DATE = re.compile(rb"\d\d/\d\d/\d\d \d\d:\d\d:\d\d")


def norm(b):
    b = DATE.sub(b"DATE", b)
    return b


def head(b):
    """the SCCS file up to the body, without the checksum and the dates"""
    lines = b.split(b"\n")
    out = []
    for l in lines[1:]:
        out.append(l)
        if l == b"\x01T":
            break
    return norm(b"\n".join(out))


def main():
    ours = os.path.abspath(sys.argv[1])
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    runs = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    cssc = os.environ.get("CSSC_DIR", "/usr/lib/x86_64-linux-gnu/cssc")
    if not os.path.exists(os.path.join(cssc, "get")):
        print("csscdiff: CSSC is needed (set CSSC_DIR)")
        sys.exit(2)
    rng = random.Random(seed)
    top = tempfile.mkdtemp()
    passed = failed = skipped = 0
    env = dict(os.environ, LC_ALL="C", TZ="UTC0")

    def versions(d):
        """the text of every delta, by CSSC's get (so also checking that
        CSSC reads the file), and by ours"""
        o = subprocess.run([os.path.join(ours, "prs"), "-a", "-d:I: :DT:", "s.f"], cwd=d,
                           capture_output=True, text=True, env=env).stdout.split("\n")
        res = []
        for l in o:
            if l.strip() and l.split()[1] == "D":
                for prog in (cssc, ours):
                    r = subprocess.run([os.path.join(prog, "get"), "-p", "-k", "-s", "-r" + l.split()[0],
                                        "s.f"], cwd=d, capture_output=True, env=env)
                    res.append((l, r.returncode, r.stdout))
        return res

    def run(cmd, d, inp=None):
        r = subprocess.run(cmd, cwd=d, input=inp, capture_output=True, env=env)
        return (None if r.returncode < 0 else r.returncode != 0), r.stdout

    for run_no in range(runs):
        da, db = os.path.join(top, "a%d" % run_no), os.path.join(top, "b%d" % run_no)
        os.makedirs(da)
        os.makedirs(db)
        log = []

        def both(args, inp=None, files=()):
            nonlocal passed, failed, skipped
            log.append(" ".join(args))
            sa, oa = run([os.path.join(ours, args[0])] + args[1:], da, inp)
            sb, ob = run([os.path.join(cssc, args[0])] + args[1:], db, inp)
            if sb is None:
                skipped += 1    # CSSC was killed by a signal (it crashes now and then)
                return True
            if args[0] == "val":
                oa = ob = b""   # CSSC's val reports on standard error
            same = sa == sb and norm(oa) == norm(ob)
            what = "output"
            if same:
                for f in ("s.f",) + tuple(files):
                    pa, pb = os.path.join(da, f), os.path.join(db, f)
                    ea, eb = os.path.exists(pa), os.path.exists(pb)
                    if ea != eb:
                        same, what = False, f
                        break
                    if ea:
                        ca, cb = open(pa, "rb").read(), open(pb, "rb").read()
                        if f == "s.f":
                            ca, cb = head(ca), head(cb)
                            if ca == cb:
                                ca, cb = versions(da), versions(db)
                        else:
                            ca, cb = norm(ca), norm(cb)
                        if ca != cb:
                            same, what = False, f
                            break
            if same:
                passed += 1
            else:
                failed += 1
                print(f"FAIL run {run_no} ({what}): {' '.join(args)}")
                print("  history: " + " ; ".join(log[-8:]))
                if what == "output":
                    print("  status", sa, sb)
                    print("  ours:", oa[:400])
                    print("  cssc:", ob[:400])
                else:
                    pa, pb = os.path.join(da, what), os.path.join(db, what)
                    print("  ours:", open(pa, "rb").read()[:600] if os.path.exists(pa) else None)
                    print("  cssc:", open(pb, "rb").read()[:600] if os.path.exists(pb) else None)
            return same

        counter = [0]

        def text(n):
            # lines that differ (a number each), so that the shortest edit
            # scripts are not ambiguous
            words = ["alpha", "beta", "gamma", "delta", "x", "y", "%I%", "%M%", "%R%.%L%", "%W%",
                     "%Z%", "%Y%", "%Q%", "%F%"]
            out = []
            for _ in range(n):
                counter[0] += 1
                out.append(" ".join([rng.choice(words) for _ in range(rng.randint(0, 3))]
                                    + [str(counter[0])]))
            return out

        def write(lines):
            data = ("\n".join(lines) + "\n") if lines else ""
            for d in (da, db):
                p = os.path.join(d, "f")
                if os.path.exists(p):
                    os.chmod(p, 0o644)
                with open(p, "w") as fh:
                    fh.write(data)

        def gfile():
            p = os.path.join(da, "f")
            return open(p).read().split("\n")[:-1] if os.path.exists(p) else None

        def sids():
            o = subprocess.run([os.path.join(ours, "prs"), "-a", "-d:I: :DT:", "s.f"], cwd=da,
                               capture_output=True, text=True).stdout.split("\n")
            return [l.split()[0] for l in o if l.strip() and l.split()[1] == "D"]

        # the SCCS file
        for d in (da, db):
            with open(os.path.join(d, "desc"), "w") as fh:
                fh.write("descriptive\ntext\n")
        write(text(rng.randint(0, 8)))
        opts = []
        if rng.random() < 0.4:
            opts.append("-fb")
        if rng.random() < 0.2:
            opts.append("-fj")
        if rng.random() < 0.2:
            opts.append("-fttype")
        if rng.random() < 0.2:
            opts.append("-fqqq")
        if rng.random() < 0.2:
            opts.append("-r%d" % rng.randint(1, 3))
        both(["admin", "-if", "-ycreated"] + opts + ["s.f"])
        for d in (da, db):
            os.remove(os.path.join(d, "f"))

        for step in range(rng.randint(3, 14)):
            k = rng.random()
            ss = sids()
            if k < 0.45:
                # an edit
                args = ["get", "-e"]
                if rng.random() < 0.4 and ss:
                    s = rng.choice(ss)
                    parts = s.split(".")
                    s = ".".join(parts[:rng.choice([1, 2, len(parts)])])
                    args.append("-r" + s)
                if rng.random() < 0.3:
                    args.append("-b")
                if not both(args + ["s.f"], files=("f", "p.f")):
                    break
                g = gfile()
                if g is None:
                    continue
                for _ in range(rng.randint(0, 4)):
                    c = rng.random()
                    if c < 0.4 and g:
                        del g[rng.randrange(len(g))]
                    elif c < 0.8:
                        g.insert(rng.randint(0, len(g)), rng.choice(text(1)))
                    elif g:
                        g[rng.randrange(len(g))] = rng.choice(text(1))
                write(g)
                if rng.random() < 0.15:
                    if not both(["unget", "s.f"], files=("f", "p.f")):
                        break
                    continue
                dargs = ["delta", "-y" + rng.choice(["fix", "a\nb", ""])]
                if not both(dargs + ["s.f"], files=("f", "p.f")):
                    break
            elif k < 0.55 and ss:
                leaf = rng.choice(ss)
                both(["rmdel", "-r" + leaf, "s.f"])
            elif k < 0.75:
                args = ["get", "-p"]
                for o in ("-m", "-n", "-k", "-s"):
                    if rng.random() < 0.25:
                        args.append(o)
                if ss and rng.random() < 0.5:
                    s = rng.choice(ss)
                    parts = s.split(".")
                    args.append("-r" + ".".join(parts[:rng.randint(1, len(parts))]))
                if ss and rng.random() < 0.2:
                    args.append("-t")
                # CSSC is not consistent about the SID of %I% when the
                # delta got is excluded: -x only without -r, of older deltas
                if ss and rng.random() < 0.15 and not any(a.startswith("-r") for a in args) \
                        and len(ss) > 1:
                    args.append("-x" + rng.choice(ss[1:]))
                if ss and rng.random() < 0.15:
                    args.append("-i" + rng.choice(ss))
                # CSSC leaves excluded and included deltas out of the l-file
                if rng.random() < 0.2 and not any(a[:2] in ("-i", "-x") for a in args):
                    args.append("-l")
                both(args + ["s.f"], files=("l.f",))
                for d in (da, db):
                    if os.path.exists(os.path.join(d, "l.f")):
                        os.remove(os.path.join(d, "l.f"))
            elif k < 0.9:
                specs = [None, ":I: :R: :L: :B: :S: :DT: :DS: :DP: :P:", ":Dt:", ":DL: :Li: :Ld: :Lu:",
                         ":D: :Dy: :Dm: :Dd: :T: :Th: :Tm: :Ts:",
                         ":C::MR:", ":FL:", ":Y: :MF: :MP: :KF: :BF: :J: :LK: :Q: :M: :FB: :CB: :Ds: :ND:",
                         ":UN:", ":FD:", ":BD:", ":GB:", ":W: :A: :Z: :F:", ":Dn: :Dx: :Dg: :DI:",
                         "text\\tand\\n:I:"]
                args = ["prs"]
                spec = rng.choice(specs)
                if spec:
                    args.append("-d" + spec)
                r = rng.random()
                if ss and r < 0.3:
                    args += [rng.choice(["-e", "-l"]), "-r" + rng.choice(ss)]
                elif ss and r < 0.5:
                    args.append("-r" + rng.choice(ss))
                if rng.random() < 0.3:
                    args.append("-a")
                both(args + ["s.f"])
            else:
                c = rng.random()
                if c < 0.3:
                    both(["sact", "s.f"])
                elif c < 0.5 and ss:
                    both(["val", "-r" + rng.choice(ss + ["1", "9.9", "1.0"]), "s.f"])
                elif c < 0.7:
                    f = rng.choice(["-fb", "-db", "-fj", "-dj", "-fttype", "-dt", "-fqtext", "-dq",
                                    "-fmmod", "-dm", "-fd1.1", "-dd", "-fn", "-dn", "-fl1", "-dl1",
                                    "-auser1", "-a!user2", "-euser1", "-a123", "-tdesc", "-t",
                                    "-fc5", "-ff1", "-dc", "-df", "-fla", "-dla", "-fi", "-di"])
                    both(["admin", f, "s.f"])
                elif c < 0.8:
                    both(["get", "-p", "-s", "-c" + rng.choice(["990101", "680101", "20", "991231235959"]),
                          "s.f"])
                else:
                    both(["get", "-g", "-L", "s.f"])
        shutil.rmtree(da)
        shutil.rmtree(db)
    # the front end: the same commands with sccs, in a directory with SCCS
    front = os.environ.get("CSSC_SCCS", "/usr/bin/sccs")
    if os.path.exists(front):
        da, db = os.path.join(top, "fa"), os.path.join(top, "fb")
        for d in (da, db):
            os.makedirs(os.path.join(d, "SCCS"))
            for name, body in (("one.c", "int one;\n/* %W% */\n"), ("two", "a\nb\nc\n")):
                with open(os.path.join(d, name), "w") as fh:
                    fh.write(body)
        steps = [["create", "one.c", "two"], ["info"], ["check"],
                 ["tell"], ["edit", "one.c"], ["info"], ["check"], ["tell"], ["tell", "-u", "nobody"],
                 ["info", "-U"], ["edit", "two"], ["unedit", "two"], ["unedit", "two"],
                 ["delget", "-yfirst", "one.c"], ["deledit", "-ysecond", "one.c"],
                 ["diffs", "one.c"], ["delta", "-ythird", "one.c"], ["get", "one.c", "two"],
                 ["prs", "-d:I:", "one.c"], ["edit", "-r1.1", "two"], ["unget", "two"], ["fix", "-r1.3", "one.c"],
                 ["delta", "-yfixed", "one.c"], ["val", "one.c"], ["what", "one.c"], ["clean"],
                 ["get", "-k", "two"], ["clean"], ["get", "one.c"], ["check"], ["rmdel", "-r1.3", "one.c"],
                 ["prs", "-d:I: :DT:", "-a", "one.c"]]

        def files(d):
            out = []
            for root, _, fs in os.walk(d):
                for f in sorted(fs):
                    p = os.path.join(root, f)
                    data = open(p, "rb").read()
                    if os.path.basename(p).startswith("s."):
                        data = head(data)
                    out.append((os.path.relpath(p, d), norm(data)))
            return sorted(out)

        for st in steps:
            sa, oa = run([os.path.join(ours, "sccs")] + st, da)
            sb, ob = run([front] + st, db)
            if sb is None:
                skipped += 1
                continue
            fa, fb = files(da), files(db)
            if sa == sb and norm(oa) == norm(ob) and fa == fb:
                passed += 1
            else:
                failed += 1
                print("FAIL sccs " + " ".join(st))
                print("  status", sa, sb)
                print("  ours:", oa[:300])
                print("  cssc:", ob[:300])
                if fa != fb:
                    print("  files:", [x for x in fa if x not in fb][:3], [x for x in fb if x not in fa][:3])
    shutil.rmtree(top)
    print(f"csscdiff: {passed} passed, {failed} failed" + (f" ({skipped} skipped: CSSC crashed)" if skipped else ""))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
