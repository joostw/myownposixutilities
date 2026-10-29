#!/usr/bin/env python3
"""Equivalence tests: run programs under two builds of this shell (for
example the hand-written-parser release and the yacc/lex build) and
report every difference in standard output, standard error or exit
status.

    equiv.py OLD NEW cases            the regression cases, three ways
    equiv.py OLD NEW gen SEED N       N random (and mutated) programs
    equiv.py OLD NEW arith SEED N     N random arithmetic expansions
    equiv.py OLD NEW files F...       the given script files
    equiv.py OLD NEW stdin            scripts that read their own input
    equiv.py OLD NEW grid K N         part K of N of the error grid

Each case is run as a script file, with -c and on standard input (except
where noted).  Both shells are invoked with argv[0] "sh"."""
import os, random, shutil, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import gensh

WORK = os.path.join(os.environ.get('TMPDIR', '/tmp'), 'sh-equiv-work')
ENV = dict(os.environ, LC_ALL='C', PATH='/usr/local/bin:/usr/bin:/bin', ENV='')
for k in ('PS1', 'PS2', 'PS4', 'IFS', 'CDPATH', 'OPTIND'):
    ENV.pop(k, None)


def run(sh, script, how, stdin_data=b'', timeout=10):
    w = WORK + '-' + str(os.getpid())
    shutil.rmtree(w, ignore_errors=True)
    os.makedirs(w)
    env = dict(ENV, HOME=w)
    p = os.path.join(w, 'script.sh')
    data = script.encode('utf-8', 'surrogateescape') if isinstance(script, str) else script
    with open(p, 'wb') as f:
        f.write(data)
    if how == 'file':
        args, inp = ['sh', p], stdin_data
    elif how == 'c':
        args, inp = ['sh', '-c', data.decode('utf-8', 'surrogateescape'), 'arg0'], stdin_data
    else:  # stdin
        args, inp = ['sh'], data + stdin_data
    try:
        r = subprocess.run(args, executable=sh, cwd=w, input=inp, capture_output=True,
                           timeout=timeout, env=env)
        res = (r.stdout, r.stderr, r.returncode)
    except subprocess.TimeoutExpired as e:
        res = ('TIMEOUT', b'', None)
    except ValueError:  # NUL in -c argument
        res = ('SKIP', b'', None)
    norm = lambda b: b.replace(w.encode(), b'$WORK') if isinstance(b, bytes) else b
    shutil.rmtree(w, ignore_errors=True)
    return tuple(norm(x) for x in res)


class Report:
    def __init__(self):
        self.n = self.diffs = self.flaky = 0

    def compare(self, name, a, b, script, rerun=None, rerun_new=None):
        """rerun(), when given, runs the old shell again: a difference is
        reported only if the old shell is consistent with itself."""
        self.n += 1
        if a == b:
            return True
        if rerun:
            for _ in range(3):
                if rerun() != a or (rerun_new and rerun_new() == a):
                    self.flaky += 1
                    return True
        self.diffs += 1
        print('DIFF', name)
        print('  script:', repr(script)[:600])
        for label, x, y in zip(('stdout', 'stderr', 'status'), a, b):
            if x != y:
                print('  %s old: %r' % (label, x if not isinstance(x, bytes) else x[:400]))
                print('  %s new: %r' % (label, y if not isinstance(y, bytes) else y[:400]))
        sys.stdout.flush()
        return False

    def done(self, what):
        print('%s: %d runs, %d differences, %d nondeterministic' % (what, self.n, self.diffs, self.flaky))
        return self.diffs == 0


def parse_cases(path):
    cases, name, buf = [], None, []
    for line in open(path, encoding='utf-8', errors='surrogateescape'):
        if line.startswith('#### '):
            if name:
                cases.append((name, ''.join(buf)))
            name, buf = line[5:].strip(), []
        else:
            buf.append(line)
    if name:
        cases.append((name, ''.join(buf)))
    return cases


def cmd_cases(old, new, rep):
    for fn in sorted(os.listdir(here)):
        if fn.startswith('cases') and fn.endswith('.txt'):
            for name, script in parse_cases(os.path.join(here, fn)):
                for how in ('file', 'c', 'stdin'):
                    a = run(old, script, how)
                    b = run(new, script, how)
                    rep.compare('%s [%s]' % (name, how), a, b, script,
                                lambda: run(old, script, how), lambda: run(new, script, how))


def cmd_gen(old, new, rep, seed, n):
    rnd = random.Random(seed)
    for i in range(n):
        script = gensh.program(rnd)
        if rnd.random() < 0.5:
            script = gensh.mutate(rnd, script)
        how = rnd.choice(('file', 'file', 'c', 'stdin'))
        a = run(old, script, how, timeout=5)
        b = run(new, script, how, timeout=5)
        rep.compare('gen %d/%d [%s]' % (seed, i, how), a, b, script,
                    lambda: run(old, script, how, timeout=5),
                    lambda: run(new, script, how, timeout=5))


def cmd_arith(old, new, rep, seed, n):
    rnd = random.Random(seed)
    for i in range(n):
        script = gensh.arith_program(rnd)
        a = run(old, script, 'file', timeout=5)
        b = run(new, script, 'file', timeout=5)
        rep.compare('arith %d/%d' % (seed, i), a, b, script,
                    lambda: run(old, script, 'file', timeout=5),
                    lambda: run(new, script, 'file', timeout=5))


def cmd_grid(old, new, rep, part, parts):
    """the error grid (gensh.grid), split into parts for parallel runs"""
    for k, (name, script) in enumerate(gensh.grid()):
        if k % parts != part:
            continue
        how = 'c' if k % 5 else 'file'
        a = run(old, script, how, timeout=5)
        b = run(new, script, how, timeout=5)
        rep.compare('grid %s [%s]' % (name, how), a, b, script,
                    lambda: run(old, script, how, timeout=5),
                    lambda: run(new, script, how, timeout=5))


def cmd_files(old, new, rep, files):
    for fn in files:
        script = open(fn, 'rb').read()
        for how in ('file', 'c', 'stdin'):
            a = run(old, script, how)
            b = run(new, script, how)
            rep.compare('%s [%s]' % (fn, how), a, b, script)


def cmd_stdin(old, new, rep):
    """The shell must not read past the current command: the rest of the
    input belongs to the commands it runs."""
    for script, data in gensh.STDIN_CASES:
        a = run(old, script, 'stdin', stdin_data=data)
        b = run(new, script, 'stdin', stdin_data=data)
        rep.compare('stdin', a, b, script)


def main():
    old, new, what = sys.argv[1], sys.argv[2], sys.argv[3]
    rep = Report()
    if what == 'cases':
        cmd_cases(old, new, rep)
    elif what == 'gen':
        cmd_gen(old, new, rep, int(sys.argv[4]), int(sys.argv[5]))
    elif what == 'arith':
        cmd_arith(old, new, rep, int(sys.argv[4]), int(sys.argv[5]))
    elif what == 'grid':
        cmd_grid(old, new, rep, int(sys.argv[4]), int(sys.argv[5]))
    elif what == 'files':
        cmd_files(old, new, rep, sys.argv[4:])
    elif what == 'stdin':
        cmd_stdin(old, new, rep)
    sys.exit(0 if rep.done(what) else 1)


if __name__ == '__main__':
    main()
