#!/bin/sh
# test_m4.sh - tests for m4 (POSIX.1-2024).  Run: sh test_m4.sh
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
# ok NAME CONDITION-COMMAND...
ok() {
    _n=$1; shift
    if "$@"; then pass=$((pass+1)); else fail=$((fail+1)); printf 'FAIL %s\n' "$_n"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/m4}
T=${TMPDIR:-/tmp}/test_m4.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# m NAME EXPECTED-OUTPUT EXPECTED-STATUS INPUT [OPTIONS...]: m4 on INPUT
m() { _n=$1 _e=$2 _s=$3 _i=$4; shift 4; printf '%s' "$_i" > in.m4; t "$_n" "$_e" "$_s" "$U" "$@" in.m4; }
# me: the same, for standard error (standard output dropped)
me() { _n=$1 _e=$2 _s=$3 _i=$4; shift 4; printf '%s' "$_i" > in.m4; t "$_n" "$_e" "$_s" sh -c '"$1" in.m4 2>&1 >/dev/null' sh "$U"; }
q="'"   # the default end quote
printf 'included here' > inc.m4
printf 'define(`a'"'"', b) a' > file.txt

# tokens, quotes, comments
m "text is copied" "a b, c (d) 1 2" 0 "a b, c (d) 1 2"
m "a macro call" "Hello, world" 0 "define(\`hi', \`Hello, \$1')hi(world)"
m "the result is rescanned" "done" 0 "define(\`a', \`b')define(\`b', \`done')a"
m "quotes stop expansion, one level is removed" "a \`a$q" 0 "define(\`a', \`A')\`a' \`\`a$q$q"
m "names: letters, digits, underscores" "X x1 _x X_" 0 "define(\`x', \`X')x x1 _x x\`'_"
m "a name not followed by ( has no arguments" "<> (y)" 0 "define(\`f', \`<\$1>')f (y)"
m "comments are copied, not expanded" "# x and x${nl}X" 0 "define(\`x', \`X')# x and x
x"
m "a comment in an argument stays" "<#,${nl}>" 0 "define(\`f', \`<\$1>')f(#,
)"
m "spaces before an argument go, after it stay" "[a ] [b	]" 0 "define(\`f', \`[\$1] [\$2]')f(  a ,	 b	)"
m "parentheses nest in arguments" "[(a, b)] [c]" 0 "define(\`f', \`[\$1] [\$2]')f((a, b), c)"
m "a quoted comma does not split" "[a, b]" 0 "define(\`f', \`[\$1]')f(\`a, b')"

# arguments
m "\$0 \$# \$1..\$9" "f 9 123456789" 0 "define(\`f', \`\`\$0' \$# \$1\$2\$3\$4\$5\$6\$7\$8\$9')f(1,2,3,4,5,6,7,8,9)"
m "\$10 is \$1 then 0" "a0" 0 "define(\`f', \`\$10')f(a,b,c,d,e,f,g,h,i,j)"
m "\$# without parentheses: 0" "0" 0 "define(\`f', \`\$#')f"
m "\$# with empty parentheses: 1" "1" 0 "define(\`f', \`\$#')f()"
m "\$* and \$@" "a,b c|\`a$q,\`b c$q" 0 "define(\`f', \`\$*|\`\$@$q')f(a,b c)"
m "missing arguments are null" "[]" 0 "define(\`f', \`[\$3]')f(a)"
m "extra arguments are fine" "a" 0 "define(\`f', \`\$1')f(a, b, c)"
m "\$ alone stays" "\$ \$x" 0 "define(\`f', \`\$ \$x')f"

# the built-ins
m "changecom: two strings" "/* x */ X" 0 "define(\`x', \`X')changecom(\`/*', \`*/')/* x */ x"
m "changecom: one string, newline ends" "@@ x${nl}X" 0 "define(\`x', \`X')changecom(\`@@')@@ x
x"
m "changecom: none turns comments off" "# X" 0 "define(\`x', \`X')changecom# x"
m "changecom: five characters" "<!-- x --> X" 0 "define(\`x', \`X')changecom(\`<!--', \`-->')<!-- x --> x"
m "changequote: new strings" "x [y]" 0 "define(\`y', \`x')changequote([, ])y [[y]]"
m "changequote: five characters" "y x" 0 "define(\`y', \`x')changequote(<<<<<, >>>>>)<<<<<y>>>>> y"
m "changequote: back to the default" "y" 0 "define(\`y', \`x')changequote([, ])changequote\`y$q"
m "decr" "41 -1" 0 "decr(42) decr(0)"
m "incr" "43 0" 0 "incr(42) incr(-1)"
m "incr: not a number" "" 1 "incr(x)"
m "decr: not a number" "" 1 "decr(1x)"
m "define: no ( : just the name" "define" 0 "define"
m "define keeps pushed definitions" "2 1" 0 "define(\`x', 1)pushdef(\`x', 0)define(\`x', 2)x popdef(\`x')x"
m "defn: quoted" "\$1 x" 0 "define(\`x', \`\$1 x')defn(\`x')"
m "defn of a built-in" "3" 0 "define(\`l', defn(\`len'))l(abc)"
m "defn: several" "ab" 0 "define(\`a', a)define(\`b', b)defn(\`a', \`b')"
m "divert and undivert at the end" "zero${nl}one${nl}two" 0 "divert(2)two
divert(1)one
divert(0)zero
"
m "divert: negative discards" "kept" 0 "divert(-1)gone
divert(0)kept"
m "divert: a number above 9" "a${nl}b" 0 "divert(12)b
divert(0)a
"
m "divert: not a number" "" 1 "divert(x)"
m "divnum" "0 3 " 0 "divnum divert(3)divnum divert(0)undivert"
m "undivert: named buffers, emptied" "b${nl}a" 0 "divert(1)a
divert(2)b
divert(0)undivert(2, 1)undivert(1)"
m "undivert into a buffer" "x${nl}y" 0 "divert(2)y
divert(1)x
undivert(2)divert(0)"
m "undivert: a file, not scanned" "define(\`a', b) a" 0 "undivert(\`file.txt')" 
m "dnl" "a b" 0 "a dnl gone
b"
m "dnl at the end of the input" "x " 0 "x dnl"
me "dumpdef: a macro" "x:	the text" 0 "define(\`x', \`the text')dumpdef(\`x')"
me "dumpdef: a built-in" "len:	<len>" 0 "dumpdef(\`len')"
me "dumpdef: an undefined macro is an error" "m4: in.m4:1: dumpdef: undefined macro \`nonesuch$q" 1 "dumpdef(\`nonesuch')"
t "dumpdef: all, sorted" "yes" 0 sh -c 'printf "define(\`zz'"'"', 1)dumpdef" | "$1" 2>&1 | awk -F: "{ print \$1 }" | sort -c && echo yes' sh "$U"
me "errprint" "a message, two" 0 "errprint(\`a message,', \`two')"
m "eval: precedence" "7 9 1 1" 0 "eval(1 + 2 * 3) eval((1 + 2) * 3) eval(1 < 2 == 1) eval(1 | 2 & 0)"
m "eval: unary" "-3 -1 0 1 3" 0 "eval(-3) eval(~0) eval(!1) eval(!0) eval(+3)"
m "eval: octal and hexadecimal" "8 255 255" 0 "eval(010) eval(0xff) eval(0XFF)"
m "eval: 32 bits" "-2147483648 0" 0 "eval(2147483647 + 1) eval(4294967296)"
m "eval: radix" "ff 11111111 z" 0 "eval(255, 16) eval(255, 2) eval(35, 36)"
m "eval: digits" "0042 -0042" 0 "eval(42, 10, 4) eval(-42, 10, 4)"
m "eval: blank radix" "10" 0 "eval(10, \`')"
m "eval: division by zero" "" 1 "eval(1 / 0)"
m "eval: a bad expression" "" 1 "eval(1 +)"
m "eval: a bad radix" "" 1 "eval(1, x)"
m "eval: not evaluated: no division by zero" "0 1" 0 "eval(0 && 1/0) eval(1 || 1/0)"
m "ifdef" "yes no no" 0 "ifdef(\`len', yes, no) ifdef(\`none', yes, no) ifdef(\`none', yes)no"
m "ifelse: three to six and more" "c  d c 2 3" 0 "ifelse(a, a, c) ifelse(a, b, c) ifelse(a, b, c, d) ifelse(a, a, c, d) ifelse(a, b, 1, x, x, 2, 3) ifelse(a, b, 1, x, y, 2, 3)"
m "include" "included here" 0 "include(\`inc.m4')"
m "include: a missing file is an error" "" 1 "include(\`none.m4')"
m "sinclude: a missing file is not" "x" 0 "sinclude(\`none.m4')x"
m "index" "2 -1 0" 0 "index(\`hello', \`ll') index(\`hello', \`z') index(\`hello', \`h')"
m "len" "0 5" 0 "len(\`') len(\`hello')"
m "m4exit: the status" "a" 4 "a\`'m4exit(4)b"
m "m4exit: after an error, not 0" "" 1 "incr(x)m4exit"
m "m4exit: not a number" "" 1 "m4exit(x)"
m "m4wrap: at the end, in order" "text${nl}first${nl}second" 0 "m4wrap(\`first
')m4wrap(\`second
')text
"
m "m4wrap: the text is expanded" "X" 0 "define(\`x', \`X')m4wrap(\`x')"
t "mkstemp: a new file" "yes" 0 sh -c 'printf "mkstemp(\`tmpXXXXXX'"'"')" | "$1" > name; f=$(cat name); [ -f "$f" ] && [ ! -s "$f" ] && case $f in tmp??????) echo yes;; esac' sh "$U"
m "mkstemp: a bad template" "" 1 "mkstemp(\`none/xXXXXXX')"
m "popdef and pushdef" "3 2 1 x" 0 "define(\`x', 1)pushdef(\`x', 2)pushdef(\`x', 3)x popdef(\`x')x popdef(\`x')x popdef(\`x')x"
m "shift: rescanned" "b,c" 0 "shift(a, b, c)"
m "substr" "llo el  hello" 0 "substr(hello, 2) substr(hello, 1, 2) substr(hello, 9) substr(hello)"
m "substr: not a number" "" 1 "substr(hello, x)"
m "syscmd: the output" "hello" 0 "syscmd(\`echo hello')"
m "syscmd: the output is not rescanned" "len(x)" 0 "syscmd(\`echo \"len(x)\"')"
m "syscmd: the output is the defining text" "[hi]" 0 "define(\`x', \`['syscmd(\`printf hi')\`]')x"
m "sysval" "0 3" 0 "syscmd(true)sysval syscmd(\`exit 3')sysval"
me "traceon: to standard error" "m4trace: -1- f" 0 "define(\`f', x)traceon(\`f')f traceoff(\`f')f
ifelse(1,1)"
t "traceon: the name" "yes" 0 sh -c 'printf "define(\`f'"'"', x)traceon(\`f'"'"')f traceoff(\`f'"'"')f" | "$1" 2>&1 >/dev/null | grep -c "f" | grep -q "^1\$" && echo yes' sh "$U"
m "translit" "hippo HELLO hll" 0 "translit(\`hello', \`el', \`ip') translit(\`hello', \`a-z', \`A-Z') translit(\`hello', \`eo')"
m "undefine" "x len" 0 "define(\`x', 1)undefine(\`x')x undefine(\`len')len"
m "builtins can be redefined" "mine" 0 "define(\`len', \`mine')len(abc)"

# options and files
t "-D: defined" "1 []" 0 sh -c 'printf "X [E]" | "$1" -DX=1 -DE' sh "$U"
t "-U: undefined" "len" 0 sh -c 'printf "len" | "$1" -Ulen' sh "$U"
t "-D and -U in order" "Y" 0 sh -c 'printf "Y" | "$1" -DY=1 -UY' sh "$U"
t "-U then -D" "2" 0 sh -c 'printf "Y" | "$1" -UY -DY=2' sh "$U"
t "options after operands" "1" 0 sh -c 'printf "X" > f1; "$1" f1 -DX=1' sh "$U"
t "-- ends the options" "" 1 sh -c 'printf "X" > f1; "$1" -- -DX=1' sh "$U"
t "files one after another" "a b" 0 sh -c 'printf "define(\`x'"'"', b)a " > f1; printf "x" > f2; "$1" f1 f2' sh "$U"
t "- is the standard input" "1 2 3" 0 sh -c 'printf "1 " > f1; printf "3" > f3; printf "2 " | "$1" f1 - f3' sh "$U"
t "a missing file: the others done" "x" 1 sh -c 'printf "x" > f1; "$1" none f1' sh "$U"
t "-s: #line" "#line 1 \"f1\"${nl}a${nl}#line 3${nl}b" 0 sh -c 'printf "a\ndnl\nb\n" > f1; "$1" -s f1' sh "$U"
t "an unknown option" "" 2 "$U" -x
t "-D without a name" "" 2 "$U" -D
t "end of file in a quoted string" "" 1 sh -c 'printf "\`open" | "$1"' sh "$U"
t "end of file in arguments" "" 1 sh -c 'printf "len(abc" | "$1"' sh "$U"

done_tests m4
