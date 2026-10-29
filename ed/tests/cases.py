# Cases for difftest.py: name, script, and optionally files (the default
# is f with five lines), args (default -s f), stderr (compare it too).

def c(name, script, **kw):
    d = dict(name=name, script=script)
    d.update(kw)
    return d

TEN = ''.join('line %d\n' % i for i in range(1, 11))
WORDS = 'alpha beta gamma\nbeta beta\n\nGamma alpha\nend $ \\ tab\there\n'

CASES = [
    # printing and addresses
    c('print-all', ',p\nQ\n'),
    c('print-n-l', '1,3n\n2l\n$p\n.=\n=\nQ\n'),
    c('addr-offsets', '2\n+\n-\n++\n--\n3 ---- 2p\n1 2 3p\n+2p\n-1p\nQ\n', files={'f': TEN}),
    c('addr-caret', '5\n^p\n^^p\nQ\n', files={'f': TEN}, differs='^ as an address (allowed, not required; GNU ed rejects it)'),
    c('addr-separators', '2,4p\n2;+2p\n,p\n;p\n7,p\n7,5,p\n7,5,9p\n,7p\n,,p\n,;p\n7;p\n7;5;p\n7;5;9p\n7;5,9p\n7;+p\n;7p\n;;p\n;,p\n1,2,3,4,5p\nQ\n', files={'f': TEN}),
    c('addr-errors', '0p\n11p\n5,3p\n-20p\n$+1p\n0\n.=\nQ\n', files={'f': TEN}),
    c('addr-search', '/line 5/p\n//p\n?line 2?p\n??p\n/line/\n?line\n/nomatch/p\n/line 1$/;//p\n3;/line/p\nQ\n', files={'f': TEN}),
    c('addr-search-wrap', '$\n/line 2/p\n1\n?line 9?p\nQ\n', files={'f': TEN}),
    c('addr-marks', '3ka\n7kb\n\'a,\'bp\n\'ap\n\'zp\n5kA\n\'a\nQ\n', files={'f': TEN}),
    c('marks-follow-lines', '5ka\n1,2d\n\'ap\n\'a=\n\'ad\n\'ap\nQ\n', files={'f': TEN}),
    c('null-command', '\n\n3\n\n$\n\n1,3\n\nQ\n', files={'f': TEN}),
    # input mode
    c('append-insert', '2a\nnew A\nnew B\n.\n,p\n0a\nfirst\n.\n0i\nzeroth\n.\n1i\nbefore one\n.\n$a\nlast\n.\n,n\nQ\n'),
    c('append-empty', '2a\n.\n.=\nq\n'),
    c('change', '2,3c\nX\nY\nZ\n.\n,p\n.=\n$c\nLAST\n.\n,p\n1,$c\n.\n.=\n,p\nQ\n'),
    c('change-no-text', '2,3c\n.\n.=\n,p\n$c\n.\n.=\nQ\n'),
    c('input-dot-lines', 'a\n..\n .\n.x\n.\n,l\nQ\n'),
    c('empty-buffer-ops', 'a\none\n.\n1d\n.=\n,p\na\nagain\n.\n,p\nQ\n', args=['-s']),
    c('i-zero-empty', '0i\nfirst\n.\n,p\nQ\n', args=['-s']),
    # delete, join, move, copy
    c('delete', '2d\n.=\n,p\n$d\n.=\n,p\n1,$d\n.=\n,p\nQ\n'),
    c('join', '1,3j\n,p\n.=\n2j\n,p\nj\n,p\n$j\nQ\n'),
    c('move', '1m3\n,p\n.=\n$m0\n,p\n2,3m$\n,p\n2,4m3\n2,4m2\n,p\n1,2m1\n,p\nQ\n'),
    c('copy', '1t3\n,p\n.=\n2,3t0\n,p\n$t$\n,p\n1,2t1\n,p\nQ\n'),
    # substitution
    c('subst-basic', '1s/l/L/\n2s/e/E/g\n3s/e/E/2\n4s/[aeiou]/(&)/g\n5s/\\(.\\)\\(.\\)/\\2\\1/\n,p\nQ\n', files={'f': TEN}),
    c('subst-flags', '1s/line/LINE/p\n2s/line/LINE/n\n3s/line/LINE/l\n4s/i/I/gp\n5s/line/X\n6s/line/\n,p\nQ\n', files={'f': TEN}),
    c('subst-nomatch', 's/zzz/y/\n1,$s/zzz/y/\n1,3s/line 2/two/\n.=\nQ\n', files={'f': TEN}),
    c('subst-delims', '1s,line,LINE,\n2s|i|I|g\n3s#e#\\##\n4s.n.\\..\n6s&e&\\&&\n7s-[0-9]--g\n8s/i/\\//g\n,p\nQ\n', files={'f': TEN}),
    c('subst-digit-delim', '5s1i1\\11\np\nQ\n', files={'f': TEN}, differs='GNU ed rejects digits as delimiters; the standard allows them'),
    c('subst-amp-and-escapes', '1s/line/[&] \\& \\\\ x/\n2s/\\(l\\)\\(i\\)/\\2\\1/\n,p\nQ\n', files={'f': TEN}),
    c('subst-percent', '1s/line/L/\n2s/line/%/\n3s/line/%%/\n4s/line/\\%/\n,p\nQ\n', files={'f': TEN}),
    c('subst-newline', '1s/ /\\\n/\n,p\n.=\n3s/e/\\\nX\\\nY/g\n,p\n.=\nQ\n'),
    c('subst-empty-global', '1s/x*/-/g\n2s/i*/-/g\n,p\nQ\n', files={'f': TEN}, differs='GNU ed refuses empty matches with g ("infinite substitution loop"); this ed does as sed'),
    c('subst-missing-group', '2s/\\(l\\)\\(i\\)/\\2\\1\\3/\np\nQ\n', files={'f': TEN}, differs='\\3 without a third group: an error here (as in sed), a literal 3 in GNU ed'),
    c('subst-empty-matches', '1s/x*/-/\n3s/$/!/\n4s/^/>/\n5s/.*/[&]/\n,p\nQ\n', files={'f': TEN}),
    c('subst-count-large', 's/a/b/3\np\ns/a/b/99\np\nQ\n', files={'f': 'a a a a a\n'}),
    c('subst-null-re', '/line 3/\ns//THREE/\n,p\ns/e/E/\ns//F/\np\nQ\n', files={'f': TEN}),
    c('subst-range-current', '2,5s/line/L/\n.=\n,p\nQ\n', files={'f': TEN}),
    c('subst-bracket-delim', 's/[/]/X/\np\ng%[%]%s//Y/\n,p\nQ\n', files={'f': 'a/b%c\n'}),
    c('subst-backref-in-re', 's/\\(a\\)\\1/X/\np\nQ\n', files={'f': 'baab\n'}),
    c('subst-classes', 's/[[:digit:]]\\{2,\\}/N/g\np\ns/[[:upper:]]/u/g\np\nQ\n', files={'f': 'A1 B22 C333\n'}),
    c('subst-errors', 's/a\ns\ns/\\(/x/\ns/a/b/x\ns/a/b/0\ns/[a/b/\nQ\n'),
    # global commands
    c('global-basic', 'g/line [13579]/p\nv/line [13579]/s/line/even/\n,p\n.=\nQ\n', files={'f': TEN}),
    c('global-multi', 'g/line/s/line/L/\\\ns/L/M/\\\np\n,p\nQ\n', files={'f': TEN}),
    c('global-default-p', 'g/line 1\nv/line [0-8]\nQ\n', files={'f': TEN}),
    c('global-delete', 'g/[02468]$/d\n,p\n.=\nu\n,p\n.=\nQ\n', files={'f': TEN}),
    c('global-move', 'g/^/m0\n,p\nQ\n', files={'f': TEN}),
    c('global-append', 'g/line [12]$/a\\\nadded\\\nmore\n,p\nQ\n', files={'f': TEN}),
    c('global-append-dot', 'g/line 3/a\\\nX\\\n.\n,p\nQ\n', files={'f': TEN}),
    c('global-nomatch', '3\ng/nothing/d\n.=\nQ\n', files={'f': TEN}),
    c('global-nested', 'g/line/g/x/p\nQ\n', files={'f': TEN}),
    c('global-range', '3,5g/line/s/$/!/\n,p\nQ\n', files={'f': TEN}),
    c('global-join', 'g/line/j\n,p\nQ\n', files={'f': TEN}),
    c('global-modify-unmarks', 'g/line/+d\n,p\nQ\n', files={'f': TEN}),
    c('interactive-global', 'G/line [123]$/\ns/line/L/\n\n&\n,p\nQ\n', files={'f': TEN}),
    c('interactive-global-v', 'V/line [0-8]$/\np\nQ\n', files={'f': TEN}),
    # undo
    c('undo-after-failed', '2d\nu\nu\ns/two/2/\nu\n,p\nQ\n', differs='GNU ed forgets the undo after a failed command; here u undoes the most recent change'),
    c('undo', '2d\nu\n,p\n.=\nu\n,p\n.=\n1s/one/1/\nu\nu\n,p\n1,2t$\nu\n,p\n3m0\nu\n,p\n1,2j\nu\n,p\nQ\n'),
    c('undo-nothing', 'u\n,p\nQ\n'),
    c('undo-after-read', '$r f\n,p\nu\n,p\nQ\n'),
    # files
    c('write-read', 'w out\n2,3w part\nr part\n,p\n0r part\n,p\nW part\nw\nq\n', stderr=True),
    c('write-bytes', 'w out\nq\n', args=['f']),
    c('edit', 'a\nchanged\n.\ne g\ne g\n,p\nf\nE f\nf\n,p\nQ\n', files={'f': 'eff\n', 'g': 'gee\ngee2\n'}),
    c('edit-nonexistent', ',p\nf\na\nx\n.\nw\nq\n', args=['-s', 'nofile'], stderr=True),
    c('filename', 'f\nf other\nf\nw\nf\nQ\n'),
    c('no-filename', 'w\nr\ne\nf\nQ\n', args=['-s']),
    c('quit-warning', 'a\nx\n.\nq\nq\n'),
    c('quit-warning-reset', 'a\nx\n.\nq\na\ny\n.\nq\nQ\n'),
    c('eof-warning', 'a\nx\n.\n', differs='exit status 1 here, 2 in GNU ed (both indicate an error)'),
    c('eof-quit', '1p\n'),
    c('shell', '!echo hi\n!!\nr !echo read\n,p\nw !cat\n!echo %\ne !printf "a\\\\nb\\\\n"\n,p\nf\nQ\n', args=['f']),
    c('shell-s', '!echo hi\nQ\n'),
    c('wq', 'a\nend\n.\nwq\n'),
    c('byte-counts', 'r f\nw g\ne g\nQ\n', args=['f']),
    c('no-trailing-newline', ',p\nw\nQ\n', files={'f': 'a\nb'}, differs='GNU ed writes "Newline appended" to standard output'),
    # help, prompt, errors
    c('help', '0p\nh\nH\n0p\n1,2f\nH\nQ\n'),
    c('prompt', 'P\n1p\nP\n2p\nQ\n'),
    c('prompt-option', '1p\nP\n2p\nQ\n', args=['-s', '-p', '> ', 'f']),
    c('unknown-commands', '1,2e\n2Q\n1f\n2u\nkx\n1k\n1kA\nQ\n'),
    c('suffixes', '1dp\n2s/o/0/gp\n1t.p\n2m$n\n1,2jl\nQ\n'),
    c('missing-destination', '1t\n,p\n$m\n,p\n2mn\n1tp\nQ\n'),
    c('suffix-errors', '1px\n1dd\nqx\nQ\n'),
    c('list-format', 'l\nQ\n', files={'f': WORDS + 'x' * 150 + '\n\x01\x7f\x08\x0c\x0d\x0b\x07\n'}),
    c('list-utf8', 'l\nn\nQ\n', differs='printable multibyte characters are written as they are; GNU ed writes their bytes in octal', files={'f': 'café 中文 été\n'}),
    c('list-c-locale', 'l\nQ\n', files={'f': 'café\n'}, locale='C'),
    c('equals', '=\n.=\n$=\n3=\n/three/=\nQ\n'),
    c('long-lines', 's/x/y/g\nw\nQ\n', files={'f': 'x' * 100000 + '\n'}),
    c('many-lines', 'g/5/d\n$=\nw\nQ\n', files={'f': ''.join('%d\n' % i for i in range(20000))}),
]
