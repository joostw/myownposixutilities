"""Random shell programs for equiv.py.

program(rnd) builds a program as a list of tokens (so that mutate() can
delete, repeat, swap or insert tokens and make syntax errors anywhere)
and renders it.  Here-document bodies follow the next newline.  The
programs terminate: loops count, and background jobs print nothing."""

KEYWORDS = ['if', 'then', 'else', 'elif', 'fi', 'do', 'done', 'case', 'esac',
            'while', 'until', 'for', 'in', '{', '}', '!']
OPS = [';', '&&', '||', '|', '(', ')', ';;', ';&', '>', '<', '>>', '2>&1', '<<', '\n']
NL = '\n'


class HD:
    """a here-document operator and its body"""
    def __init__(self, op, delim, body):
        self.op, self.delim, self.body = op, delim, body


class Gen:
    def __init__(self, rnd, depth=3):
        self.r = rnd
        self.depth = depth
        self.funcs = ['f1', 'f2']
        self.vars = ['v', 'w', 'n', 'i']

    def ch(self, *xs):
        return self.r.choice(xs)

    def p(self, x):
        return self.r.random() < x

    # ---------- words ----------
    def word(self, d=0):
        r = self.r.random()
        if r < 0.30:
            return self.ch('a', 'b', 'x y'.split()[0], 'hello', '1', '42', 'foo=bar', '-n', '--', '%s',
                           '*.none', 'a\\ b', "it''s", 'z:y')
        if r < 0.40:
            return self.ch(*KEYWORDS)
        if r < 0.50:
            return self.ch("'single q'", '"dq $v"', '"$w"', "'if'", '"fi"', '\\if', "$'a\\tb'",
                           '"a\\"b"', "''", '""')
        if r < 0.60:
            return self.ch('$v', '${w}', '${u:-def}', '${v:+alt}', '${#v}', '${v%a}', '${v#?}',
                           '$1', '$#', '$?', '"$@"', '${u-"q d"}', '$((n+1))', '$((i*2))')
        if r < 0.70 and d < 2:
            inner = Gen(self.r, 1).list_tokens(1)
            if self.p(0.5):
                return ['$('] + inner + [')']
            text = render([t for t in inner if not isinstance(t, HD)]).replace('`', '').replace('\\', '')
            return '`' + text.replace(NL, ';') + '`'
        if r < 0.75:
            return self.ch(*self.funcs)
        return self.ch('word', 'x', 'y', 'p_q', 'END', 'EOF')

    def flat(self, w):
        return w if isinstance(w, list) else [w]

    # ---------- commands ----------
    def redir(self):
        r = self.r.random()
        if r < 0.5:
            return [self.ch('>/dev/null', '2>/dev/null', '2>&1', '>>out', '>out', '<out', '<&-',
                            '3>&1', '>|out2', '<>rw', '1>&2')]
        if r < 0.65:
            return [self.ch('>', '<', '>>', '2>'), self.ch('out', '/dev/null', 'f', '$v')]
        delim = self.ch('EOF', 'END', "'Q'", '"D"', 'E\\F', 'x')
        op = self.ch('<<', '<<-', '<<')
        lines = []
        for _ in range(self.r.randint(0, 3)):
            lines.append(self.ch('line $v', 'plain', '\tTabbed $w', "'quoted' \"dq\"", 'back\\slash',
                                 '$(echo sub)', '`echo bq`', '$((1+2))', 'EOFX', '\\$v'))
        return [HD(op, delim, lines)]

    def simple(self):
        t = []
        while self.p(0.25):
            t.append(self.ch('v=1', 'w="two words"', 'n=0', 'x=$v', 'u=', 'v=$(echo cs)'))
        if self.p(0.1):
            t += self.redir()
        name = self.ch('echo', 'echo', 'echo', 'printf', 'true', 'false', ':', 'f1', 'f2', 'al1',
                       'al2', 'eval', 'cat', 'set', '[', 'alias', 'unalias', 'readonly', 'export')
        if name == 'eval':
            t += ['eval', self.ch("'echo ev'", "'if true; then echo et; fi'", '"echo \\$v"',
                                  "'echo $((1+1))'", "'case a in a) echo c;; esac'", "'fi'",
                                  "'echo $('", "'(echo s)'")]
        elif name == 'printf':
            t += ['printf', "'%s|'"]
        elif name == '[':
            t += ['[', self.ch('"$v"', '1', 'a'), self.ch('=', '-eq', '!='), self.ch('1', 'a'), ']']
        elif name == 'alias':
            t += ['alias', self.ch("al1='echo '", "al2='echo al2'", "al3=if", "al4='then echo t;'",
                                   "al5='{'", "al1='printf %s-'", "fi=echo", "x='echo x '")]
        elif name == 'cat':
            t += ['cat'] + self.redir()
            return t
        elif name == 'set':
            t += ['set', '--', 'p1', self.ch('p2', '"p 3"')]
            return t
        else:
            t.append(name)
        for _ in range(self.r.randint(0, 3)):
            t += self.flat(self.word())
        if self.p(0.2):
            t += self.redir()
        return t

    def compound(self, d):
        k = self.r.randint(0, 9)
        if k == 0:
            t = ['if'] + self.clist(d) + ['then'] + self.clist(d)
            while self.p(0.3):
                t += ['elif'] + self.clist(d) + ['then'] + self.clist(d)
            if self.p(0.5):
                t += ['else'] + self.clist(d)
            return t + ['fi']
        if k == 1:
            v = self.ch('i', 'n')
            return [v + '=0;', self.ch('while', 'until')] + \
                (['[', '$((%s+=1))' % v, '-le', '2', ']'] if self.p(0.5) else ['false']) + \
                [self.ch(';', NL), 'do'] + self.clist(d) + ['done']
        if k == 2:
            t = ['for', self.ch('i', 'v', 'x')]
            if self.p(0.8):
                t += [self.ch('in', NL + 'in')]
                for _ in range(self.r.randint(0, 3)):
                    t += self.flat(self.word(1))
                t += [self.ch(';', NL, ';' + NL)]
            else:
                t += [self.ch(';', NL, '')]
            return t + ['do'] + self.clist(d) + ['done']
        if k == 3:
            t = ['case', self.ch('a', '$v', 'hello', '"x y"', 'esac', 'in'), self.ch('in', NL + 'in')]
            for _ in range(self.r.randint(0, 3)):
                if self.p(0.3):
                    t.append(NL)
                pats = [self.ch('a', '*', 'h*', '[ab]', '"x y"', 'esac', 'in', '?', '$v')]
                while self.p(0.3):
                    pats += ['|', self.ch('b', 'c*', 'x')]
                if self.p(0.3):
                    pats = ['('] + pats
                t += pats + [')'] + self.clist(d) + [self.ch(';;', ';;', ';&', ';;' + NL)]
            if self.p(0.3):
                t += ['z)'] + self.clist(d)
            return t + ['esac']
        if k in (4, 5):
            return ['{'] + self.clist(d) + [self.ch(';', NL), '}']
        if k == 6:
            return ['('] + self.clist(d) + [')']
        if k == 7:
            name = self.ch(*self.funcs)
            return [name, '()', self.ch('', NL)] + self.compound(d)
        if k == 8:
            return self.simple() + ['|'] + self.simple()
        return ['!'] + self.simple()

    def command(self, d):
        if d < self.depth and self.p(0.45):
            t = self.compound(d + 1)
            if self.p(0.15):
                t += self.redir()
            return t
        return self.simple()

    def and_or(self, d):
        t = self.command(d)
        while self.p(0.25):
            t += [self.ch('&&', '||', '|', '&&' + NL, '|' + NL)] + self.command(d)
        if self.p(0.05):
            t = ['!'] + t
        return t

    def clist(self, d):
        t = []
        for _ in range(self.r.randint(1, 3)):
            t += self.and_or(d)
            t.append(self.ch(';', NL, ';', NL, NL + NL, '; ' + NL))
        if self.p(0.05):
            t += [':', '&']
        return t

    def list_tokens(self, n):
        t = []
        for _ in range(n):
            t += self.and_or(0)
            t.append(self.ch(';', NL, NL))
        return t[:-1] if t and t[-1] == ';' and self.p(0.5) else t


def render(tokens):
    """Join tokens; here-document bodies go after the next newline."""
    out, pending = [], []
    for t in tokens:
        if isinstance(t, HD):
            out.append(t.op + t.delim + ' ')
            pending.append(t)
            continue
        if NL in t:
            out.append(t)
            for h in pending:
                d = h.delim.replace("'", '').replace('"', '').replace('\\', '')
                pre = '\t' if h.op == '<<-' else ''
                for l in h.body:
                    out.append(pre + l + NL)
                out.append(pre + d + NL)
            pending = []
            continue
        out.append(t + ' ')
    return ''.join(out)


ALIASES = ["alias al1='echo '", "alias al2='echo al2'", "alias kif='if'", "alias kthen='then echo kt;'",
           "alias kbr='{ echo br;'", "alias al3='al2 '", "alias fi='echo not-fi'", "alias e2=''",
           "alias rd='>/dev/null echo r'", "alias semi='echo s;'"]


def program(rnd):
    g = Gen(rnd, depth=rnd.randint(1, 3))
    head = []
    if rnd.random() < 0.3:
        head = rnd.sample(ALIASES, rnd.randint(1, 3))
        head = [x + '\n' for x in head]
    toks = g.list_tokens(rnd.randint(1, 4))
    if rnd.random() < 0.3:
        toks = [x if not (isinstance(x, str) and x in ('echo', 'if', 'then')) or rnd.random() > 0.3
                else rnd.choice(('al1', 'al2', 'kif', 'kthen', 'kbr', 'rd', 'e2', 'semi', 'al3')) for x in toks]
    body = render(toks)
    if rnd.random() < 0.15:
        body = body.replace(' ', ' \\\n', 1)
    if rnd.random() < 0.15:
        body = body.replace(NL, ' # comment\n', 1)
    return ''.join(head) + 'f1() { echo in-f1 "$@"; }\nf2() ( echo in-f2 )\nv=val w=wv\n' + body + '\n'


MUT_POOL = KEYWORDS + OPS + ['word', '$(', '`', '"', "'", '${', '$((', '))', 'f()', '{ ', '}']


def mutate(rnd, script):
    """Token-level damage: delete, repeat, swap or insert tokens."""
    lines = script.split('\n')
    k = rnd.randrange(len(lines))
    toks = lines[k].split(' ')
    for _ in range(rnd.randint(1, 3)):
        op = rnd.randint(0, 3)
        if not toks:
            toks = ['']
        i = rnd.randrange(len(toks))
        if op == 0 and len(toks) > 1:
            del toks[i]
        elif op == 1:
            toks.insert(i, toks[i])
        elif op == 2 and i + 1 < len(toks):
            toks[i], toks[i + 1] = toks[i + 1], toks[i]
        else:
            toks.insert(i, rnd.choice(MUT_POOL))
    lines[k] = ' '.join(toks)
    if rnd.random() < 0.2:
        cut = rnd.randrange(len(script))
        return '\n'.join(lines)[:cut]
    return '\n'.join(lines)


# ---------- arithmetic ----------

AOPS = ['+', '-', '*', '/', '%', '<<', '>>', '<', '<=', '>', '>=', '==', '!=', '&', '^', '|',
        '&&', '||', ',']
ASSIGN = ['=', '+=', '-=', '*=', '/=', '%=', '<<=', '>>=', '&=', '^=', '|=']
ANAMES = ['x', 'y', 'z', 'h', 'bad', 'e', 'u', 'r', 'n', 'neg', 'sp', 'big', 'oct']


def aexpr(rnd, d=0):
    r = rnd.random()
    if d > 4 or r < 0.25:
        return rnd.choice(['0', '1', '7', '42', '010', '0x1F', '0X10', '08', '0x', '1a', '00', '9',
                           '2147483648', '9223372036854775807', '9223372036854775808', '0xffffffffffffffff',
                           '0777', '1_', '_x']) if rnd.random() < 0.6 else rnd.choice(ANAMES)
    if r < 0.55:
        return aexpr(rnd, d + 1) + rnd.choice(['', ' ', '  ', '\n', '\t']) + rnd.choice(AOPS) + \
            rnd.choice(['', ' ']) + aexpr(rnd, d + 1)
    if r < 0.62:
        return rnd.choice(['-', '+', '!', '~', '- ', '--', '++', '+ +', '-+']) + aexpr(rnd, d + 1)
    if r < 0.70:
        v = rnd.choice(ANAMES)
        return rnd.choice([v + '++', v + '--', '++' + v, '--' + v, v + ' ++', '++ ' + v, v + '\n++'])
    if r < 0.78:
        return rnd.choice(ANAMES) + rnd.choice(['', ' ']) + rnd.choice(ASSIGN) + rnd.choice(['', ' ']) + \
            aexpr(rnd, d + 1)
    if r < 0.86:
        return '(' + aexpr(rnd, d + 1) + rnd.choice([')', ')', ', ' + aexpr(rnd, d + 1) + ')', ''])
    if r < 0.93:
        return aexpr(rnd, d + 1) + ' ? ' + aexpr(rnd, d + 1) + rnd.choice([' : ', ':', ' ', ' , ']) + \
            aexpr(rnd, d + 1)
    return rnd.choice(['@', '$', ' ', '', '))', '(', '#', '.', '1.5', '=', '?', ':', 'x y', '3 4',
                       '1 ++ 2', 'x+++y', 'x---y', '+++x', '5++x', '5--x', '(x)++', '++(x)', 'x++ ++',
                       '++x++', 'x++=1', '1+=2', '!=3', '~-1', 'x=y=3', 'a?b:c=1', '1?2:x=3'])


def arith_program(rnd):
    e = aexpr(rnd)
    if rnd.random() < 0.15:
        e = e[:rnd.randrange(len(e) + 1)]
    pre = "x=5 y=' 7 ' z=-3 h=0x10 bad=abc e= n=2 neg=-9223372036854775808 sp=' ' big=99999999999999999999 oct=011\n" \
          "readonly r=4\nunset u\n"
    if rnd.random() < 0.1:
        pre += 'set -u\n'
    form = rnd.choice(['echo $((%s))', 'echo "$((%s))"', ': $((%s)); echo ok', 'echo $(( %s ))',
                       'eval \'echo $((%s))\'', 'v=$((%s)); echo "$v"'])
    return pre + (form % e) + '\necho "x=$x y=$y z=$z u=$u n=$n h=$h e=$e"\n'


# ---------- scripts that read their own standard input ----------

STDIN_CASES = [
    ('read a\necho "got [$a]"\n', b'data line\n'),
    ('if true; then\nread a\nfi\necho "[$a]"\n', b'more\n'),
    ('cat <<EOF\nhd\nEOF\nread b; echo "b=$b"\n', b'after\n'),
    ('x=$(head -n 1); echo "x=$x"\n', b'first\nsecond\n'),
    ('f() {\n read l\n echo "f:$l"\n}\nf\nf\n', b'one\ntwo\n'),
    ('alias r=read\nr a\necho "$a"\n', b'aliased\n'),
    ('while read l; do echo "<$l>"; done\n', b'p\nq\n'),
    ('echo a && \\\nread z; echo "z=$z"\n', b'zz\n'),
    ('case x in x) read q;; esac; echo "q=$q"\n', b'qq\n'),
    ('{ read m; echo "m=$m"; }\n', b'mm\n'),
    ('cat\n', b'rest\nof\ninput\n'),
    ('head -n 1\necho tail\n', b'h1\nh2\n'),
    ('echo $((1+\n2)); read t; echo "t=$t"\n', b'tt\n'),
    ('for i in 1 2; do read v; echo "$i:$v"; done\n', b'x1\nx2\n'),
    ('read a <<E\nfromdoc\nE\necho "$a"; read b; echo "$b"\n', b'fromstdin\n'),
    ('echo "multi\nline"; read c; echo "c=$c"\n', b'cc\n'),
    ('eval "read e"; echo "e=$e"\n', b'ee\n'),
    ('(read s; echo "s=$s")\n', b'ss\n'),
    ('echo `read k; echo k=$k`\n', b'kk\n'),
    ('if read y\nthen echo "y=$y"\nfi\n', b'yy\n'),
]


# ---------- the error grid: every token in every place ----------

GRID_TEMPLATES = [
    'if echo A ; then echo B ; fi',
    'if echo A ; then echo B ; elif echo C ; then echo D ; else echo E ; fi',
    'while false ; do echo B ; done ; echo W',
    'until true ; do echo B ; done',
    'for i in x y ; do echo $i ; done',
    'for i ; do echo $i ; done',
    'for i do echo $i ; done',
    'for i \n in x \n do echo $i \n done',
    'case w in a ) echo A ;; ( w | d ) echo W ;& f ) echo F ;; esac',
    'case w in esac',
    'case w \n in \n w ) echo A \n esac',
    '{ echo A ; echo B ; }',
    '( echo A ; echo B )',
    'f ( ) { echo F ; } ; f',
    'f ( ) \n ( echo F ) ; f',
    '! echo A | cat && echo C || echo D & wait ; echo F',
    'echo A > f 2>&1 < /dev/null ; cat f',
    'x=1 y=2 echo $x $y',
    'echo $( echo A ; echo B ) ` echo C `',
    'cat << E ; echo after \n body \n E',
    'echo A && \n echo B',
    'if ( true ) then echo B ; fi',
    'while { false ; } do echo B ; done ; echo W',
    '{ ( echo A ) }',
    'if true ; then { echo A ; } fi',
    'for i in a ; do ( echo $i ) done',
    'case a in a ) ( echo A ) esac',
    'echo $( if true ; then echo T ; fi ) $( case a in a ) echo C ;; esac )',
    'eval "if true ; then echo E ; fi"',
    'T echo A ; F',
    'X Y Z',
]
GRID_ALIASES = "alias T='if true; then' F=fi D=do E=esac B='{' S=';' N='' X='echo ' Y='y ' Z=then\n"
GRID_TOKENS = KEYWORDS + [';', '&', '|', '&&', '||', '(', ')', ';;', ';&', '>', '<<', '2>', '\n',
                          'x=1', 'word', 'f()', '()', 'T', 'F', 'D', 'E', 'B', 'S', 'N', 'X', 'Z',
                          '$(', ')', '`', '{', '}', '#', '\\\n', 'echo']


def grid():
    """(name, script) for every edit of every template"""
    for ti, tpl in enumerate(GRID_TEMPLATES):
        toks = tpl.replace('\n', ' \n ').split(' ')
        toks = [t for t in toks if t != '']
        n = len(toks)
        def show(ts):
            return GRID_ALIASES + ' '.join(ts).replace(' \n ', '\n') + '\n'
        yield '%d/orig' % ti, show(toks)
        for i in range(n + 1):
            yield '%d/cut%d' % (ti, i), GRID_ALIASES + ' '.join(toks[:i]).replace(' \n ', '\n')
            if i < n:
                yield '%d/del%d' % (ti, i), show(toks[:i] + toks[i + 1:])
            for k, tok in enumerate(GRID_TOKENS):
                yield '%d/ins%d.%d' % (ti, i, k), show(toks[:i] + [tok] + toks[i:])
                if i < n:
                    yield '%d/rep%d.%d' % (ti, i, k), show(toks[:i] + [tok] + toks[i + 1:])
