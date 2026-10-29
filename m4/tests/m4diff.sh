#!/bin/sh
# m4diff.sh - compare m4 with GNU m4 -G (its extensions off): the output
# and the exit status of many inputs, and the standard error where it is
# not a diagnostic (errprint, dumpdef).  Left out are what GNU does its
# own way: the order of several m4wrap (GNU reverses it), syscmd inside
# other macros (GNU does not capture its output), the trace format, and
# non-numeric arguments (only a warning for GNU, an error here).
# Run: sh tests/m4diff.sh ./m4   (needs GNU m4 as "m4" in PATH)
M=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
m4 --version 2>/dev/null | grep -q GNU || { echo "m4diff: GNU m4 is needed"; exit 2; }
T=${TMPDIR:-/tmp}/m4diff.$$
mkdir "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
pass=0 fail=0 n=0
printf 'define(`inc'"'"', `INC'"'"')included text inc\n' > inc.m4
# c [-e] [OPTIONS...]: the input on standard input, both m4s; -e: compare
# standard error too
c() {
    e=false
    [ "$1" = -e ] && { e=true; shift; }
    n=$((n + 1))
    cat > in.$n
    # at most 5 seconds of CPU time each, in case of a loop
    (ulimit -t 5; "$M" "$@" in.$n > o1 2> e1); s1=$?
    (ulimit -t 5; m4 -G "$@" in.$n > o2 2> e2); s2=$?
    if [ $s1 = $s2 ] && cmp -s o1 o2 && { ! $e || cmp -s e1 e2; }; then pass=$((pass+1))
    else
        fail=$((fail+1)); echo "FAIL case $n (status $s1 and $s2):"; sed 's/^/  | /' in.$n
        diff o1 o2 | cut -c 1-150 | sed 's/^/  /' | head -10
        $e && diff e1 e2 | cut -c 1-150 | sed 's/^/  /' | head -6
    fi
}
c <<'X'
plain text, no macros
X
c <<'X'
define(`hello', `Hello, $1!')hello(`world') hello
X
c <<'X'
define(`a', `A')define(`b', `a')b `b' ``b'' a`'b
X
c <<'X'
define(`args', `$# [`$0'] [$1] [$2] [$*] [$@]')args args() args(x) args(x, y) args( a ,  b c , (d,e) )
X
c <<'X'
define(`ten', `$1$2$3$4$5$6$7$8$9 $10')ten(1,2,3,4,5,6,7,8,9,X)
X
c <<'X'
define(`q', `$@')define(`list', `q(`a,b', c)')list
X
c <<'X'
define(`rec', `ifelse($1, `0', `', `$1 rec(decr($1))')')rec(5)
X
c <<'X'
define(`x', `1')pushdef(`x', `2')x popdef(`x')x popdef(`x')x
X
c <<'X'
define(`x', `1')pushdef(`x', `2')define(`x', `3')x popdef(`x')x undefine(`x')x
X
c <<'X'
define(`mydef', defn(`define'))mydef(`z', `zed')z defn(`z') defn(`nonesuch')
X
c <<'X'
ifdef(`define', `yes', `no') ifdef(`nope', `yes', `no') ifdef(`nope', `yes')
X
c <<'X'
ifelse(a, a, same, diff) ifelse(a, b, same, diff) ifelse(a, b, same)
ifelse(a, b, 1, c, c, 2, 3) ifelse(a, b, 1, c, d, 2, 3) ifelse(a, b, 1, c, d, 2)
ifelse(`a', `a') ifelse(x)
X
c <<'X'
incr(41) decr(43) incr(-1) incr(2147483647) decr(0)
X
c <<'X'
len(`') len(`abc') len(hello world) index(`hello', `l') index(`hello', `z') index(`abc', `')
X
c <<'X'
substr(`hello world', 6) substr(`hello', 1, 3) substr(`hello', 10) substr(`hello', 2, 0) substr(`hello', 3, 100)
X
c <<'X'
translit(`hello', `el', `ip') translit(`hello', `l') translit(`hello', `a-z', `A-Z') translit(`abc', `abc', `xy')
X
c <<'X'
eval(1 + 2 * 3) eval((1 + 2) * 3) eval(7 / 2) eval(-7 / 2) eval(7 % 3) eval(-7 % 3)
eval(1 << 4) eval(256 >> 2) eval(-16 >> 2) eval(5 & 3) eval(5 | 3) eval(5 ^ 3)
eval(~0) eval(!0) eval(!5) eval(-(3)) eval(+3) eval(3 < 4) eval(3 <= 3) eval(4 > 5)
eval(4 >= 5) eval(1 == 1) eval(1 != 1) eval(1 && 0) eval(1 || 0) eval(0 || 0)
eval(010) eval(0x1F) eval(0X10) eval(2147483647 + 1) eval(-2147483648 / -1)
eval(1 + 2 * 3 - 4 / 2 % 3 << 1 >> 1 < 10 == 1 & 1 ^ 0 | 0 && 1 || 0)
eval(255, 16) eval(255, 2) eval(255, 36) eval(5, 10, 4) eval(-5, 10, 4) eval(0, 16, 0) eval(35, 36)
eval(0 && 1/0) eval(1 || 1/0)
X
c <<'X'
divert(1)one
divert(2)two
divert(1)more one
divert(0)zero
divnum divert(3)divnum divert
undivert(2)after two
divert(-1)discarded
divert(0)end
X
c <<'X'
divert(1)a
divert(2)b
divert(1)undivert(2)c
divert(0)undivert(1)d
undivert
X
c <<'X'
divert(2)two
divert(0)undivert(1, 2)x
X
c <<'X'
dnl a comment line
text dnl rest of line
more
X
c <<'X'
# a comment with define(`x', `y') and x
x
changecom(`/*', `*/')/* define(`x') x */ # not now x
changecom(`//')// to the end x
x
changecom// still?
X
c <<'X'
changequote([, ])define([x], [y])x [x] [[x]] `x'
changequote`'define(`z', `w')z
changequote({{, }})define({{a}}, {{b}})a {{a}}
changequote
X
c <<'X'
define(`x', `y')changequote(<<, >>)<<x>> x <<<<x>>>> changequote`'`x'
X
c <<'X'
shift shift(a) shift(a, b) shift(a, `b, c', d)
define(`last', `ifelse($#, 1, `$1', `last(shift($@))')')last(a, b, c)
X
c <<'X'
define(`f', `<$1>')f (x) f(x)y f(
x) f( 	 x)
X
c <<'X'
define(`nest', `($1)')nest((a, b)) nest(((a), (b), c)) nest(`(', `)')
X
c <<'X'
define(`x', `X')x_y xy x1 _x x.y (x) x,x
X
c <<'X'
define(`a', `b')define(`b', `c')define(`c', `done')a
X
c <<'X'
define(`exch', `$2, $1')define(exch(`expansion text', `macro'))macro
X
c <<'X'
define(`x', `$1 $2')x(`a)', b)
X
c <<'X'
define(`m', ``quoted $1'')m(arg)
X
c <<'X'
define(`car', `$1')define(`cdr', `shift($@)')car(cdr(a, b, c))
X
c -e <<'X'
errprint(`a message
')errprint(`two', `args')
X
c -e <<'X'
define(`foo', `bar baz')dumpdef(`foo')dumpdef(`define', `foo')
X
c <<'X'
include(`inc.m4')after
sinclude(`nonexistent.m4')after sinclude
X
c <<'X'
m4exit
not reached
X
c <<'X'
text m4exit(3) not reached
X
c <<'X'
divert(1)diverted
divert(0)m4exit(0)
X
c <<'X'
m4wrap(`at the end
')text
X
c <<'X'
m4wrap(`define(`x', `X')x
')before x
X
c <<'X'
syscmd(`echo hello')sysval syscmd(`exit 3')sysval
X
c <<'X'
define(`x', `y')undefine(`x')x undefine(`define')define(`a', `b')a
X
c <<'X'
len define divnum dnl
X
c -DX=1 -Dempty -DY=2 -UY <<'X'
X empty. Y
X
c -s <<'X'
one
define(`two', `2
2')two
three
X
c <<'X'
define(`x', `1')x()y x(a)(b)
X
c <<'X'
`unbalanced'' quotes' and `nested `deep' quotes'
X
c <<'X'
define(`i', 0)define(`loop', `ifelse(i, 50, `', `define(`i', incr(i))i loop')')loop
X
c <<'X'
define(`stack', `pushdef(`s', $1)')stack(1)stack(2)stack(3)s popdef(`s')s popdef(`s')s
X
c <<'X'
define(`reverse', `ifelse($#, 0, , $#, 1, ``$1'', `reverse(shift($@)), `$1'')')reverse(a, b, c, d)
X
c <<'X'
define(`forloop', `pushdef(`$1', `$2')_forloop($@)popdef(`$1')')
define(`_forloop', `$4`'ifelse($1, `$3', `', `define(`$1', incr($1))$0($@)')')
forloop(`i', `1', `8', `i ')
X
c <<'X'
define(`upcase', `translit(`$*', `a-z', `A-Z')')upcase(hello, world)
X
c <<'X'
changecom(`#')# comment
define(`#', x)
`#' quoted
X
c <<'X'
defn(`len')(abc) define(`l', defn(`len'))l(abcd)
X
echo "m4diff: $pass passed, $fail failed"
[ "$fail" = 0 ]
