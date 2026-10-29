#!/usr/bin/env python3
"""Compare the ex regular expression matcher with sed(1) (POSIX BREs,
leftmost-longest): the matched text, global substitution and back
references in replacements."""
import os, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
EX = os.path.join(HERE, '..', 'ex')

pats = [r'a*', r'a\{2,3\}', r'\(ab\)*', r'\(a*\)*b', r'a\{0,1\}\(ab\)*', r'x*y*', r'\(a\)\1',
        r'\(.\)\(.\)\2\1', r'[^a]*', r'^a', r'a$', r'.*', r'\(a*\)\(b*\)', r'[]a]', r'[a-c]*d',
        r'\(a\{1,2\}\)\{2\}', r'b*\(ab\)*', r'\(x*\)\(y*\)\2', r'[[:alpha:]][[:alnum:]]*', r'a.c',
        r'\.', r'a\{3\}', r'\(\(a\)b\)*c', r'\(a\)\(b\)*', r'[[:digit:]]*', r'^\(a*\)$', r'\(a\|b\)']
strs = ['aaa', 'abab', 'aabab', 'xxyy', 'abba', 'baaac', 'aaaa', 'ab]c', 'abcd', 'xyy', 'a.c',
        'zz9a1', 'ababc', 'aaab', 'yyx', '']

def ex_subst(s, p, repl, flags):
    script = 'a\n%s\n.\n1s/%s/%s/%s\n1p\nq!\n' % (s, p, repl, flags)
    r = subprocess.run([EX, '-s', '/dev/null'], input=script.encode(), capture_output=True,
                       env={'LC_ALL': 'C', 'PATH': os.environ.get('PATH', '/bin:/usr/bin')})
    return r.stdout.decode() if r.returncode == 0 else s + '\n'

def sed_subst(s, p, repl, flags):
    r = subprocess.run(['sed', 's/%s/%s/%s' % (p, repl, flags)], input=(s + '\n').encode(),
                       capture_output=True, env={'LC_ALL': 'C', 'PATH': os.environ.get('PATH', '/bin:/usr/bin')})
    return r.stdout.decode() if r.returncode == 0 else None

def main():
    n = bad = 0
    for p in pats:
        if '\\|' in p:
            continue      # not a basic regular expression
        for repl, flags in (('[&]', ''), ('[&]', 'g'), (r'<\1|\2>', '')):
            if '\\2' in repl and p.count('\\(') < 2:
                continue
            for s in strs:
                want = sed_subst(s, p, repl, flags)
                if want is None:
                    continue
                n += 1
                got = ex_subst(s, p, repl, flags)
                if got != want:
                    bad += 1
                    print('DIFF s/%s/%s/%s on %r: sed %r ex %r' % (p, repl, flags, s, want, got))
    print('%d cases, %d differ' % (n, bad))

main()
