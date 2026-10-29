"""Random awk programs for equiv.py.

program(rnd) returns a list of tokens; render() joins them, with or
without blanks between tokens (blanks matter: "f(" is a call, "f (" is
not, "a ++b" differs from "a++ b").  mutate() deletes, repeats, swaps
or inserts tokens so that syntax errors land everywhere.  grid() yields
small templates with every token replaced by, or preceded by, each
token of POOL.
"""
import random

NL = '\n'

NAMES = ['x', 'y', 'n', 'arr', 'a', 's', 'i', 'k', 'NF', 'NR', 'FS', 'OFS', 'ORS', 'RS',
         'SUBSEP', 'RSTART', 'RLENGTH', 'FILENAME', 'ENVIRON', 'ARGV', 'CONVFMT']
FUNCS = ['f', 'g', 'h']
BUILTINS1 = ['length', 'int', 'sqrt', 'exp', 'log', 'sin', 'cos', 'tolower', 'toupper',
             'system', 'close', 'fflush', 'srand', 'rand']
STRINGS = ['"a"', '"hello world"', '""', '"\\t|\\n"', '"x\\"y"', '"\\101\\7"', '"\\q"',
           '"a\\/b"', '"%d %s\\n"', '"%5.2f"', '"\\\\"', '"tab\\\nnext"', '"1e3"', '" 12 "']
NUMBERS = ['0', '1', '2', '10', '3.5', '.5', '1.', '1e3', '1E-2', '1e', '1e+', '1.2.3',
           '007', '0x1A', '123456789012', '2e308', '1.5e2x']
REGEXES = ['/a/', '/^[a-z]+$/', '/a|b/', '/[/]/', '/\\//', '/[[:alpha:]]+/', '/x*/',
           '/[]a]/', '/[^]a]/', '/=x/', '/a\\.b/', '/(ab)+c?/', '/[[:digit:].]/',
           '/[[.a.]]/', '/\\$/', '/{/', '/a{2}/', '/\\t/']
ASSIGNOPS = ['=', '+=', '-=', '*=', '/=', '%=', '^=', '**=']
RELOPS = ['<', '<=', '==', '!=', '>=', '>']
COMMENTS = ['# comment', '#', '# } { ) /']


class Gen:
    def __init__(self, rnd, depth=3):
        self.r = rnd
        self.depth = depth
        self.loop = 0
        self.infunc = False

    def ch(self, xs):
        return self.r.choice(xs)

    def p(self, x):
        return self.r.random() < x

    # ---------- expressions ----------
    def lvalue(self, d):
        r = self.r.random()
        if r < 0.5:
            return [self.ch(NAMES)]
        if r < 0.75:
            return [self.ch(NAMES), '['] + self.exprlist(d - 1, 1 + self.p(0.3)) + [']']
        return ['$'] + self.dollar_operand(d - 1)

    def exprlist(self, d, n):
        out = []
        for i in range(n):
            if i:
                out += [','] + ([NL] if self.p(0.1) else [])
            out += self.expr(d)
        return out

    def dollar_operand(self, d):
        r = self.r.random()
        if d > 0 and r < 0.1:
            return [self.ch(['++', '--'])] + self.dollar_operand(d - 1)
        if d > 0 and r < 0.2:
            return [self.ch(['-', '+', '!'])] + self.dollar_operand(d - 1)
        return self.primary(d)

    def primary(self, d):
        r = self.r.random()
        if d <= 0 or r < 0.25:
            return [self.ch(NUMBERS + STRINGS + NAMES + REGEXES[:4])]
        if r < 0.35:
            return self.lvalue(d)
        if r < 0.45:
            return ['('] + ([NL] if self.p(0.1) else []) + self.expr(d - 1) + [')']
        if r < 0.5:
            return ['$'] + self.dollar_operand(d - 1)
        if r < 0.55:
            return [self.ch(['++', '--'])] + self.lvalue(d - 1)
        if r < 0.62:
            f = self.ch(FUNCS)
            n = self.r.randint(0, 2)
            return [f + '('] + self.exprlist(d - 1, n) + [')']
        if r < 0.72:
            b = self.ch(BUILTINS1)
            if b == 'length' and self.p(0.3):
                return ['length']
            n = 0 if b in ('rand', 'srand', 'fflush', 'length') and self.p(0.5) else 1
            return [b, '('] + self.exprlist(d - 1, n) + [')']
        if r < 0.8:
            which = self.ch(['substr', 'index', 'split', 'sub', 'gsub', 'match', 'sprintf',
                             'atan2'])
            if which in ('sub', 'gsub'):
                args = [self.ch(REGEXES)] + [','] + self.expr(d - 1)
                if self.p(0.5):
                    args += [','] + self.lvalue(d - 1)
            elif which == 'split':
                args = self.expr(d - 1) + [','] + [self.ch(NAMES)]
                if self.p(0.5):
                    args += [','] + [self.ch(REGEXES + STRINGS)]
            elif which == 'match':
                args = self.expr(d - 1) + [','] + [self.ch(REGEXES)]
            else:
                args = self.exprlist(d - 1, self.r.randint(1, 3))
            return [which, '('] + args + [')']
        if r < 0.88:
            g = ['getline']
            if self.p(0.5):
                g += self.lvalue(d - 1)
            if self.p(0.5):
                g += ['<'] + self.primary(d - 1)
            return g
        if r < 0.94:
            return ['('] + self.exprlist(d - 1, 2) + [')', 'in', self.ch(NAMES)]
        return [self.ch(['-', '+', '!'])] + self.primary(d - 1)

    def expr(self, d, print_level=False):
        r = self.r.random()
        if d <= 0 or r < 0.25:
            return self.primary(d)
        if r < 0.33:
            return self.lvalue(d - 1) + [self.ch(ASSIGNOPS)] + ([NL] if self.p(0.05) else []) + self.expr(d - 1)
        if r < 0.38:
            return self.lvalue(d - 1) + [self.ch(['++', '--'])]
        if r < 0.45:
            return self.expr(d - 1) + ['?'] + ([NL] if self.p(0.1) else []) + self.expr(d - 1) + [':'] + self.expr(d - 1)
        if r < 0.5:
            return self.expr(d - 1) + [self.ch(['&&', '||'])] + ([NL] if self.p(0.2) else []) + self.expr(d - 1)
        if r < 0.54:
            return self.expr(d - 1) + ['in', self.ch(NAMES)]
        if r < 0.6:
            return self.expr(d - 1) + [self.ch(['~', '!~'])] + [self.ch(REGEXES)] if self.p(0.6) else self.expr(d - 1) + [self.ch(['~', '!~'])] + self.expr(d - 1)
        if r < 0.67:
            op = self.ch(RELOPS[:-1] if print_level else RELOPS)
            return self.expr(d - 1) + [op] + self.expr(d - 1)
        if r < 0.75:
            return self.expr(d - 1) + self.expr(d - 1)       # concatenation
        if r < 0.85:
            return self.expr(d - 1) + [self.ch(['+', '-', '*', '/', '%', '^', '**'])] + self.expr(d - 1)
        if r < 0.9:
            return [self.ch(['-', '+', '!'])] + self.expr(d - 1)
        if r < 0.95:
            return self.expr(d - 1) + ['|', 'getline'] + (self.lvalue(d - 1) if self.p(0.5) else [])
        return self.primary(d)

    # ---------- statements ----------
    def term(self):
        return [self.ch([NL, ';', ';' + NL, NL + NL])]

    def simple(self, d):
        r = self.r.random()
        if r < 0.3:
            out = [self.ch(['print', 'printf'])]
            if out[0] == 'printf':
                out += [self.ch(STRINGS[8:10] + ['"%s %s\\n"'])]
                if self.p(0.5):
                    out += [','] + self.exprlist(d - 1, self.r.randint(1, 2))
            elif self.p(0.15):
                out += ['('] + self.exprlist(d - 1, self.r.randint(1, 3)) + [')']
            elif self.p(0.8):
                out += self.exprlist(d - 1, self.r.randint(1, 3))
            if self.p(0.25):
                out += [self.ch(['>', '>>', '|'])] + [self.ch(['"/dev/null"', '"cat >/dev/null"', 's', '"o" n'])]
            return out
        if r < 0.38:
            out = ['delete', self.ch(NAMES)]
            if self.p(0.7):
                out += ['['] + self.exprlist(d - 1, 1 + self.p(0.3)) + [']']
            return out
        return self.expr(d)

    def stmt(self, d):
        r = self.r.random()
        if d <= 0 or r < 0.35:
            return self.simple(d) + self.term()
        if r < 0.45:
            return ['if', '('] + self.expr(d - 1) + [')'] + ([NL] if self.p(0.3) else []) + self.stmt(d - 1) + \
                (['else'] + ([NL] if self.p(0.3) else []) + self.stmt(d - 1) if self.p(0.5) else [])
        self.loop += 1
        try:
            if r < 0.52:
                body = [';'] if self.p(0.1) else ([NL] if self.p(0.3) else []) + self.stmt(d - 1)
                return ['while', '('] + self.expr(d - 1) + [')'] + body
            if r < 0.57:
                return ['do'] + ([NL] if self.p(0.3) else []) + self.stmt(d - 1) + ['while', '('] + self.expr(d - 1) + [')'] + self.term()
            if r < 0.64:
                init = self.simple(d - 1) if self.p(0.8) else []
                cond = self.expr(d - 1) if self.p(0.8) else []
                inc = self.simple(d - 1) if self.p(0.8) else []
                body = [';'] if self.p(0.1) else self.stmt(d - 1)
                return ['for', '('] + init + [';'] + ([NL] if self.p(0.1) else []) + cond + [';'] + inc + [')'] + body
            if r < 0.7:
                return ['for', '(', self.ch(NAMES), 'in', self.ch(NAMES), ')'] + self.stmt(d - 1)
        finally:
            self.loop -= 1
        if r < 0.8:
            out = ['{']
            for _ in range(self.r.randint(0, 3)):
                out += self.stmt(d - 1)
            return out + ['}'] + ([NL] if self.p(0.3) else [])
        if r < 0.85 and self.loop:
            return [self.ch(['break', 'continue'])] + self.term()
        if r < 0.9:
            kw = 'return' if self.infunc and self.p(0.6) else self.ch(['exit', 'next', 'nextfile'])
            out = [kw]
            if kw in ('return', 'exit') and self.p(0.5):
                out += self.expr(d - 1)
            return out + self.term()
        if r < 0.93:
            return [';']
        return [self.ch(COMMENTS), NL] + self.simple(d) + self.term()

    def block(self, d):
        out = ['{']
        for _ in range(self.r.randint(0, 4)):
            out += self.stmt(d)
        return out + ['}']

    def item(self, d):
        r = self.r.random()
        if r < 0.15:
            self.infunc = True
            name = self.ch(FUNCS)
            params = self.r.sample(['p', 'q', 'arr2', 'loc'], self.r.randint(0, 3))
            out = ['function', name + ('(' if self.p(0.7) else ''), '(' if False else '']
            out = ['function', name, '(']
            for i, pn in enumerate(params):
                if i:
                    out += [','] + ([NL] if self.p(0.1) else [])
                out.append(pn)
            out += [')'] + ([NL] if self.p(0.2) else []) + self.block(d)
            self.infunc = False
            return out
        if r < 0.3:
            return [self.ch(['BEGIN', 'END'])] + ([NL] if self.p(0.1) else []) + self.block(d)
        if r < 0.4:
            return self.block(d)
        pat = self.expr(d - 1)
        if self.p(0.15):
            pat += [','] + ([NL] if self.p(0.2) else []) + self.expr(d - 1)
        if self.p(0.75):
            pat += self.block(d)
        return pat


def program(rnd, depth=3):
    g = Gen(rnd, depth)
    out = [NL]                          # never an option
    for _ in range(rnd.randint(1, 4)):
        it = g.item(depth)
        out += it
        if it[-1] == '}' and rnd.random() < 0.3:
            continue                    # items after "}" need no separator
        out += [rnd.choice([NL, ';', NL + NL, ' ' + rnd.choice(COMMENTS) + NL])]
    # define the functions that are called but not defined (mostly)
    for f in FUNCS:
        called = (f + '(') in out
        defined = any(out[i] == 'function' and out[i + 1] == f for i in range(len(out) - 1))
        if called and not defined and rnd.random() < 0.95:
            out += [NL, 'function', f, '(', 'p', ',', 'q', ')', '{', 'return', 'p', '}', NL]
    return out


def render(tokens, rnd):
    out = []
    prev = ''
    word = lambda c: c.isalnum() or c in '_."'
    for t in tokens:
        if prev and not prev.endswith('\n') and not t.startswith('\n'):
            r = rnd.random()
            if word(prev[-1]) and word(t[0]) and r >= 0.7 and rnd.random() < 0.9:
                r = 0           # mostly keep words apart
            if r < 0.7:
                out.append(' ')
            elif r < 0.75:
                out.append(' \\\n')
            elif r < 0.78:
                out.append('\t')
        out.append(t)
        prev = t
    return ''.join(out)


POOL = ['{', '}', '(', ')', '[', ']', ';', NL, ',', '+', '-', '*', '/', '%', '^', '!', '>', '<',
        '|', '?', ':', '~', '!~', '$', '=', '+=', '/=', '||', '&&', '==', '++', '--', '>>', '**',
        'in', 'if', 'else', 'while', 'do', 'for', 'break', 'continue', 'next', 'nextfile',
        'exit', 'return', 'delete', 'print', 'printf', 'getline', 'function', 'BEGIN', 'END',
        'length', 'substr', 'x', 'f(', '1', '"s"', '/r/', '/r', '"s', '@', '&', '\\', '#c',
        '.', '\\\n', '/=r/']


def mutate(tokens, rnd):
    t = list(tokens)
    for _ in range(rnd.randint(1, 3)):
        if not t:
            break
        i = rnd.randrange(len(t))
        r = rnd.random()
        if r < 0.3:
            del t[i]
        elif r < 0.45:
            t.insert(i, t[i])
        elif r < 0.6:
            j = rnd.randrange(len(t))
            t[i], t[j] = t[j], t[i]
        else:
            t.insert(i, rnd.choice(POOL))
    return t


GRID_TEMPLATES = [
    'BEGIN { x = 1 ; print x }',
    '/re/ , $2 ~ "y" { n ++ } END { print n }',
    'function f ( a , b ) { return a + b } BEGIN { print f( 1 , 2 ) }',
    'BEGIN { if ( x ) y = 1 ; else { z = 2 } }',
    'BEGIN { while ( i < 3 ) i ++ ; do x -- ; while ( x > 0 ) }',
    'BEGIN { for ( i = 0 ; i < 2 ; i ++ ) print i ; for ( k in a ) delete a [ k ] }',
    'BEGIN { print ( 1 , 2 ) > "/dev/null" ; printf "%s" , x | "cat" }',
    'BEGIN { "echo" | getline v ; getline < "f" ; while ( ( getline l ) > 0 ) n ++ }',
    'BEGIN { x = a ? b : c ; y = $ ++ i ; z = - $ 1 ^ 2 ; w = ! a ~ b }',
    'BEGIN { s = substr( "abc" , 2 ) length ; t = ( 1 , 2 ) in a ; u = a b c }',
    '{ a [ $1 , $2 ] += 1 } END { for ( k in a ) print k , a [ k ] }',
    'NR == 1 , NR == 2 ; { next } END { exit 1 }',
    'BEGIN { x = 1 ;\n y = 2 &&\n z }',
    'BEGIN { x = $ ++ y ; z = a in b | "c" ; delete d | e }',
    'BEGIN { print a | "cat" ; x ++ | getline ; $ 1 ++ | getline }',
    'BEGIN { if ( x ) /r/ ; else /s/ ; while ( y ) /t/ }',
    'function f ( a ) { return a | getline } BEGIN { x = length / 2 }',
]


def grid():
    """(name, program text) pairs: every template token replaced by, and
    preceded by, each pool token."""
    for ti, tpl in enumerate(GRID_TEMPLATES):
        toks = tpl.replace('\n', ' \n ').split(' ')
        toks = [x for x in toks if x != '']
        yield ('t%d.orig' % ti, ' '.join(toks))
        for i in range(len(toks)):
            for pi, p in enumerate(POOL):
                rep = toks[:i] + [p] + toks[i + 1:]
                yield ('t%d.rep%d.%d' % (ti, i, pi), ' '.join(rep))
                ins = toks[:i] + [p] + toks[i:]
                yield ('t%d.ins%d.%d' % (ti, i, pi), ' '.join(ins))
            dl = toks[:i] + toks[i + 1:]
            yield ('t%d.del%d' % (ti, i), ' '.join(dl))
            # no blank before the token
            if i:
                tight = ' '.join(toks[:i - 1]) + ' ' + toks[i - 1] + toks[i] + ' ' + ' '.join(toks[i + 1:])
                yield ('t%d.tight%d' % (ti, i), tight)


# hand-written programs for what the generator does not make: bytes
# outside ASCII, NULs, carriage returns, ends in the middle of a token,
# continuations in odd places, and the corners of regular expressions
EDGE = [
    b'', b' ', b'\n', b'#', b'# only a comment', b'\\', b'\\\n', b'\\\r\n', b'\r\n',
    b'BEGIN{print 1}\r\n', b'BEGIN{print 1}\r', b'BEGIN\r\n{print 1}',
    b'BEGIN{print "a\x00b"}', b'BEGIN{print 1}\x00', b'\x00', b'BEGIN{x=1 # c\x00c\nprint x}',
    b'BEGIN{print /a\x00b/}', b'BEGIN{caf\xe9=1; print caf\xe9}', b'BEGIN{\xe9=2; print \xe9}',
    b'BEGIN{x\xe9y=3}', b'BEGIN{print "\xe9"}', b'BEGIN{f\xe9(1)}',
    b'function f\xe9(a){return a} BEGIN{print f\xe9(2)}',
    b'BEGIN{print "abc', b'BEGIN{print "abc\\', b'BEGIN{print "abc\\\n', b'BEGIN{print "a\\\nb"}',
    b'BEGIN{print "a\nb"}', b'BEGIN{print /abc', b'BEGIN{print /abc\\', b'BEGIN{print /a\\/b/}',
    b'BEGIN{print /a\\\nb/}', b'BEGIN{print /[/]/}', b'BEGIN{print /[[:alpha:]/]/}',
    b'BEGIN{print /[[:alpha:/]/}', b'BEGIN{print /[[:alpha/:]]/}', b'BEGIN{print /[]/]/}',
    b'BEGIN{print /[^]/]/}', b'BEGIN{print /[[.a.]/]/}', b'BEGIN{print /[[=a=]/]/}',
    b'BEGIN{print /[/', b'BEGIN{print /[[:a', b'BEGIN{x=/=/; print x}', b'BEGIN{x/=2; print x}',
    b'BEGIN{x=4; x/=2; print x}', b'BEGIN{print 1/2/1}', b'BEGIN{print (1)/2/(1)}',
    b'BEGIN{if (1) /x/; print "ok"}', b'BEGIN{while (0) /x/; print "ok"}',
    b'BEGIN{for (;0;) /x/; print "ok"}', b'BEGIN{for (k in a) /x/; print "ok"}',
    b'BEGIN{print length/2}', b'BEGIN{getline/2}', b'BEGIN{x[1]/2}', b'BEGIN{x++/2}',
    b'BEGIN{print $/x/}', b'BEGIN{print 1 /x/}', b'BEGIN{print "a" /x/}',
    b'BEGIN { "echo a" |getline; print }', b'BEGIN { "echo a" |\\\ngetline; print }',
    b'BEGIN { "echo a" | getline x; print x }', b'BEGIN { "echo a" | getlinex; print }',
    b'BEGIN { print "x" | getline }', b'BEGIN { print | getline }', b'BEGIN { printf | getline }',
    b'BEGIN { print > "/dev/null" | getline }', b'BEGIN { "echo a" | getline < "f" }',
    b'BEGIN{}END{}', b'BEGIN{}\\\nEND{}', b'BEGIN{} END{}', b'BEGIN{}\tEND{}', b'BEGIN{}\fEND{}',
    b'BEGIN{}\vEND{}', b'BEGIN{}\rEND{}', b'BEGIN{}}', b'BEGIN{} }', b'BEGIN{}{}', b'BEGIN{}x',
    b'{}x', b'x{}y', b'x y', b'x\ny', b'x;y', b'x }', b'x )', b'x {', b'{ x', b'{ x;', b'{ x\n',
    b'{ if (x) y', b'{ do x', b'{ do x;', b'{ do x; while', b'{ do x; while (y)', b'{ while (x)\n',
    b'{ print', b'{ printf', b'{ printf }', b'{ printf; }', b'{ printf > "x" }', b'{ exit', b'{ exit }',
    b'{ exit ) }', b'{ return }', b'function f(){ return }', b'function f(){ return ) }',
    b'{ break }', b'{ while(1) break }', b'{ while(1) { break } }', b'{ for(;;) continue }',
    b'function f(a,){} BEGIN{f(1)}', b'function f(,a){}', b'function f(a a){}', b'function f(a,a){}',
    b'function f(f){}', b'function f(){} function f(){}', b'function f()\n\n{}', b'function f() x',
    b'function (a){}', b'function f', b'function f(', b'function f(a', b'function f(a,', b'func f(){}',
    b'function f(a){ a(1) }', b'function f(a){ a[1]; return a } BEGIN { f(x) }',
    b'BEGIN { f = 1 } function f(){}', b'BEGIN { f() }', b'BEGIN { g() } function f(){}',
    b'BEGIN { $++1 }', b'BEGIN { $++x }', b'BEGIN { $--$x }', b'BEGIN { $++1 @ }', b'BEGIN { ++1 }',
    b'BEGIN { ++$1 }', b'BEGIN { x++ ++ }', b'BEGIN { 1 ++ y }', b'BEGIN { x ++y }', b'BEGIN{x+++y}',
    b'BEGIN { $x++ }', b'BEGIN { $$0 }', b'BEGIN { $-1 }', b'BEGIN { $!x }', b'BEGIN { -2^2 }',
    b'BEGIN { 2^-1 }', b'BEGIN { 2^3^2 }', b'BEGIN { !x^2 }', b'BEGIN { ! x ~ y }',
    b'BEGIN { a < b < c }', b'BEGIN { a ~ b ~ c }', b'BEGIN { a in b in c }', b'BEGIN { (a,b) in c }',
    b'BEGIN { ((a,b)) }', b'BEGIN { (a,b) }', b'BEGIN { print (a,b) }', b'BEGIN { print (a,b), c }',
    b'BEGIN { print (a,b) > "/dev/null" }', b'BEGIN { print (a,b) c }', b'BEGIN { x = (a,b) }',
    b'BEGIN { print (a,b)(c,d) }', b'BEGIN { (a,b) / 2 }', b'BEGIN { for ((k) in a) ; }',
    b'BEGIN { for (k in a in b) ; }', b'BEGIN { for ((i,j) in a) ; }', b'BEGIN { for (print; ; ) break }',
    b'BEGIN { for (print ) ; }', b'BEGIN { for (;;print) break }', b'BEGIN { for (\n;;) ; }',
    b'BEGIN { for (;\n;) break }', b'BEGIN { for (;;\n) break }', b'BEGIN { if (\nx) ; }',
    b'BEGIN { x = 1 ? 2 : 3 = 4 }', b'BEGIN { x = 1 ?\n2\n:\n3 }', b'BEGIN { x = a &&\n b ||\n c }',
    b'BEGIN { f(\n1\n,\n2\n) } function f(a,b){}', b'BEGIN { x[1,\n2] }', b'BEGIN { x[\n1] }',
    b'BEGIN { delete x[1,\n2] }', b'BEGIN { delete x[\n1] }', b'BEGIN { delete f(1) }', b'BEGIN { delete }',
    b'BEGIN { delete x[] }', b'BEGIN { length length }', b'BEGIN { length() }', b'BEGIN { length 1 }',
    b'BEGIN { substr }', b'BEGIN { substr ("abc", 2) }', b'BEGIN { getline x y }',
    b'BEGIN { getline < "a" "b" }', b'BEGIN { getline < "a" < 2 }', b'BEGIN { getline x < "a" * 2 }',
    b'BEGIN { getline $1 < "a" }', b'BEGIN { getline $1++ }', b'BEGIN { getline x = 5 }',
    b'BEGIN { print > "a" > "b" }', b'BEGIN { print >> "/dev/null" "x" }', b'BEGIN { print a >= b }',
    b'BEGIN { print a > b ? c : d }', b'BEGIN { print x ? (a,b) : c }', b'BEGIN { print a = b > "/dev/null" }',
    b'BEGIN { if (1) print; else print }', b'BEGIN { if (1) print else print }',
    b'BEGIN { if (1) print;; else print }', b'BEGIN { if (1) {print}\n\nelse print 2 }',
    b'BEGIN { if (1) print 1\n\n\nelse print 2 }', b'BEGIN { if (1) ; else ; }',
    b'BEGIN { while (0)\n;\nprint "x" }', b'BEGIN { do ; while (0) }', b'BEGIN { do\n\nx++\n\nwhile (x < 3) }',
    b'BEGIN { do x++ while (x < 3) }', b'BEGIN { do {x++} while (x < 3) print x }',
    b'BEGIN { 1e; 1e+; 1.2.3; .5.; 08; 0x1A; 1.5e2x }', b'BEGIN { print 1e }', b'BEGIN { print .}',
    b'BEGIN { print 1. }', b'BEGIN { x = "\\q\\/\\"\\101\\7777" ; print x }',
    b'BEGIN { print a !b }', b'BEGIN { print a ! b }', b'BEGIN { print a - b }', b'BEGIN { print a -b }',
    b'BEGIN { print a " " -b }', b'BEGIN { print 1 " " +2 }', b'BEGIN { x = y\n= 2 }',
    b'BEGIN { x =\n 2; print x }', b'BEGIN { print 1,\n2 }', b'BEGIN { print 1\n, 2 }',
    b'NR==1,\nNR==2', b'NR==1,', b'NR==1, {', b'BEGIN\n{ print 1 }', b'BEGIN\n\nx', b'END', b'BEGIN',
    b'BEGIN {} ; ; END {}', b';;;', b'\n\n;\n', b'{ ;;; }', b'{ \n\n }', b'{ ; x }',
    b'@', b'&', b'BEGIN { x & y }', b'BEGIN { a && }', b'BEGIN { a || }', b'BEGIN { ? }',
    b'BEGIN{print 1}' * 3, b'{' * 50 + b'}' * 50, b'BEGIN{x=' + b'(' * 200 + b'1' + b')' * 200 + b'}',
    b'BEGIN{x=1' + b'+1' * 2000 + b'}', b'BEGIN{x="' + b'a' * 100000 + b'"}',
]
