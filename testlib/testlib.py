"""testlib.py - what the Python test runners share.

    utf8_locale()        a UTF-8 locale: $UTF8_LOCALE, or C.UTF-8, or any one
                         that locale -a lists; '' if there is none (POSIX
                         names no such locale)
    compiler()           the C compiler: $TESTCC or $CC, or the first of c17,
                         c99, cc
    cc_env()             CC, and the optional flags the compiler accepts:
                         CWARN (-std=c99 -Wall -Werror), CPEDANTIC (the same
                         with -pedantic), CQUIET (-w), and CSTDS: those of
                         c99 c11 c17 that it takes with -std= and the strict
                         options; empty where not
    build(name, outdir)  compile testlib/NAME.c into outdir; the path of
                         the program
"""
import os, shutil, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))


def utf8_locale():
    loc = os.environ.get('UTF8_LOCALE')
    if loc:
        return loc
    try:
        names = subprocess.run(['locale', '-a'], stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, text=True).stdout.split()
    except OSError:
        return ''
    for n in names:
        if n.lower() in ('c.utf-8', 'c.utf8'):
            return n
    for n in names:
        if n.lower().endswith(('.utf-8', '.utf8')):
            return n
    return ''


def compiler():
    cc = os.environ.get('TESTCC') or os.environ.get('CC')
    if cc:
        return cc
    for c in ('c17', 'c99', 'cc'):
        if shutil.which(c):
            return c
    return 'cc'


def cc_env():
    import tempfile
    cc = compiler()
    d = tempfile.mkdtemp(prefix='ccenv.')
    src = os.path.join(d, 't.c')
    with open(src, 'w') as f:
        f.write('int main(void) { return 0; }\n')
    def accepts(flags):
        r = subprocess.run(cc.split() + flags.split() + ['-o', os.path.join(d, 't'), src],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return r.returncode == 0
    warn = '-std=c99 -Wall -Werror'
    env = dict(CC=cc,
               CWARN=warn if accepts(warn) else '',
               CPEDANTIC=warn + ' -pedantic' if accepts(warn + ' -pedantic') else '',
               CQUIET='-w' if accepts('-w') else '',
               CSTDS=' '.join(s for s in ('c99', 'c11', 'c17')
                              if accepts('-std=%s -pedantic -Wall -Wextra -Werror' % s)))
    shutil.rmtree(d, ignore_errors=True)
    return env


def build(name, outdir):
    out = os.path.join(outdir, name)
    cmd = compiler().split() + ['-o', out, os.path.join(HERE, name + '.c')]
    subprocess.run(cmd, check=True)
    return out


def non_ascii(*texts):
    """does any of the texts (str or bytes) hold a character outside ASCII?"""
    for t in texts:
        if isinstance(t, str):
            t = t.encode('utf-8', 'surrogateescape')
        if any(b > 127 for b in t):
            return True
    return False
