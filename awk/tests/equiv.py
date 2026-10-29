#!/usr/bin/env python3
"""Equivalence tests: run programs under two builds of this awk (for
example the hand-written-parser release and the yacc/lex build) and
report every difference in standard output, standard error and exit
status.

    equiv.py OLD NEW cases                the regression cases (tests/cases*.sh)
    equiv.py OLDD NEWD ast SEED N         random programs, parse trees compared
    equiv.py OLDD NEWD mut SEED N         mutated random programs
    equiv.py OLDD NEWD grid PART PARTS    the error grid
    equiv.py OLD NEW run SEED N           random programs run on input

OLDD/NEWD are builds with tests/astdump.c linked in (see Makefile target
equiv-builds); the parse is printed (AWK_AST_DUMP) instead of run.  Both
binaries should be called "awk", since messages start with the name.
"""
import os, random, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import genawk
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'testlib'))
import testlib

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = tempfile.mkdtemp(prefix='awk-equiv-')
DATA = b'a b c\n1 2 3\nx  y\tz\n\n10 20 abc\n'


def run(awk, prog, how='arg', dump=False, timeout=5, data=DATA):
    env = dict(os.environ, LC_ALL=os.environ.get('AWK_TEST_LC', 'C'))
    if dump:
        env['AWK_AST_DUMP'] = '1'
    if how == 'arg':
        args = [awk, prog if isinstance(prog, str) else os.fsdecode(prog)]
    else:
        fn = os.path.join(WORK, 'prog.awk')
        with open(fn, 'wb') as f:
            f.write(prog.encode('latin-1') if isinstance(prog, str) else prog)
        args = [awk, '-f', fn]
    datafn = os.path.join(WORK, 'data')
    with open(datafn, 'wb') as f:
        f.write(data)
    args.append(datafn)
    try:
        p = subprocess.run(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=timeout, cwd=WORK, env=env)
        return (p.stdout, p.stderr.replace(WORK.encode(), b'$WORK'), p.returncode)
    except subprocess.TimeoutExpired:
        return ('timeout',)


class Report:
    def __init__(self):
        self.n = self.diffs = self.timeouts = self.flaky = 0

    def compare(self, name, a, b, prog, rerun=None):
        self.n += 1
        if a == ('timeout',) and b == ('timeout',):
            self.timeouts += 1
            return
        if a == b:
            return
        if rerun:
            # a program whose output depends on timing (pipes to commands)
            # gives different results from the same binary: not counted
            for _ in range(3):
                if rerun() != a:
                    self.flaky += 1
                    return
        self.diffs += 1
        print('DIFF %s' % name)
        print('  prog: %r' % prog)
        if len(a) == 3 and len(b) == 3:
            for i, what in enumerate(('stdout', 'stderr', 'status')):
                if a[i] != b[i]:
                    sa, sb = a[i], b[i]
                    if isinstance(sa, bytes) and len(sa) > 600:
                        # show where they part
                        k = 0
                        while k < min(len(sa), len(sb)) and sa[k] == sb[k]:
                            k += 1
                        sa, sb = sa[max(0, k - 200):k + 200], sb[max(0, k - 200):k + 200]
                    print('  %s old: %r' % (what, sa))
                    print('  %s new: %r' % (what, sb))
        else:
            print('  old: %r\n  new: %r' % (a[:3], b[:3]))
        sys.stdout.flush()

    def summary(self, what):
        print('%s: %d runs, %d differences, %d both timed out, %d nondeterministic' %
              (what, self.n, self.diffs, self.timeouts, self.flaky))


def cmd_cases(old, new, rep):
    """every regression case, with $AWK set to each binary in turn"""
    cases = []
    for fn in ('cases.sh', 'cases2.sh'):
        for line in open(os.path.join(HERE, fn), encoding='latin-1'):
            m = re.match(r"^t\s+(\S+)\s+'(.*)'\s*$", line)
            if m:
                cases.append((m.group(1), m.group(2)))
    for name, cmd in cases:
        res = []
        for awk in (old, new):
            env = dict(os.environ, AWK=awk)
            try:
                p = subprocess.run(['sh', '-c', cmd], cwd=HERE, env=env, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
                res.append((p.stdout.replace(awk.encode(), b'AWK'),
                            p.stderr.replace(awk.encode(), b'AWK'), p.returncode))
            except subprocess.TimeoutExpired:
                res.append(('timeout',))
        rep.compare('case ' + name, res[0], res[1], cmd)


def cmd_ast(old, new, rep, seed, n, mutate=False):
    rnd = random.Random(seed)
    for k in range(n):
        toks = genawk.program(rnd, depth=rnd.randint(1, 4))
        if mutate:
            toks = genawk.mutate(toks, rnd)
        prog = genawk.render(toks, rnd)
        how = 'arg' if k % 3 else 'file'
        rep.compare('%s %d/%d [%s]' % ('mut' if mutate else 'ast', seed, k, how),
                    run(old, prog, how, dump=True), run(new, prog, how, dump=True), prog)


def cmd_grid(old, new, rep, part, parts):
    first = int(os.environ.get('AWK_GRID_FROM', '0'))      # skip templates
    for k, (name, prog) in enumerate(genawk.grid()):
        if k % parts != part or int(name[1:name.index('.')]) < first:
            continue
        how = 'arg' if k % 4 else 'file'
        rep.compare('grid %s [%s]' % (name, how), run(old, prog, how, dump=True),
                    run(new, prog, how, dump=True), prog)


def cmd_edge(old, new, rep, dump):
    """the hand-written corner cases, in three locales"""
    locs = [('C', {})] + ([(testlib.utf8_locale(), {})] if testlib.utf8_locale() else []) + [
            ('en_US.ISO-8859-1', {'LOCPATH': os.environ.get('AWK_TEST_LOCPATH', '')})]
    for i, prog in enumerate(genawk.EDGE):
        for loc, extra in locs:
            if loc.startswith('en_US') and not extra['LOCPATH']:
                continue
            for how in ('arg', 'file'):
                if how == 'arg' and b'\x00' in prog:
                    continue        # an argument cannot hold a NUL
                os.environ.update(extra)
                os.environ['AWK_TEST_LC'] = loc
                a = run(old, prog, how, dump=dump)
                b = run(new, prog, how, dump=dump)
                for k in extra:
                    os.environ.pop(k, None)
                rep.compare('edge %d %s [%s]' % (i, loc, how), a, b, prog)


def cmd_run(old, new, rep, seed, n):
    rnd = random.Random(seed)
    for k in range(n):
        toks = genawk.program(rnd, depth=rnd.randint(1, 3))
        prog = genawk.render(toks, rnd)
        rep.compare('run %d/%d' % (seed, k), run(old, prog, timeout=3), run(new, prog, timeout=3), prog,
                    lambda: run(old, prog, timeout=3))


def main():
    old, new, what = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]), sys.argv[3]
    rest = [int(x) for x in sys.argv[4:]]
    rep = Report()
    if what == 'cases':
        cmd_cases(old, new, rep)
    elif what == 'ast':
        cmd_ast(old, new, rep, rest[0], rest[1])
    elif what == 'mut':
        cmd_ast(old, new, rep, rest[0], rest[1], mutate=True)
    elif what == 'grid':
        cmd_grid(old, new, rep, rest[0], rest[1])
    elif what == 'edge':
        cmd_edge(old, new, rep, dump=True)
    elif what == 'edge-run':
        cmd_edge(old, new, rep, dump=False)
    elif what == 'run':
        cmd_run(old, new, rep, rest[0], rest[1])
    rep.summary(what)
    sys.exit(1 if rep.diffs else 0)


if __name__ == '__main__':
    main()
