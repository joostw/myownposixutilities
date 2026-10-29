#!/bin/sh
# test_xargs.sh - tests for xargs (POSIX.1-2024).  Run: sh test_xargs.sh
# -p reads its answers from /dev/tty, so only its refusal without a
# terminal is tested.
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/xargs}
T=${TMPDIR:-/tmp}/test_xargs.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# x NAME INPUT EXPECTED STATUS ARGUMENT...: printf INPUT | xargs ARGUMENT...
x() {
    _n=$1 _in=$2 _ex=$3 _st=$4; shift 4
    printf "$_in" > in
    t "$_n" "$_ex" "$_st" sh -c '"$0" "$@" < in' "$U" "$@"
}
# args: each argument in brackets, to see the boundaries
printf '#!/bin/sh\nfor a; do printf "[%%s]" "$a"; done; echo\n' > args; chmod +x args
A=./args

x "echo is the default" 'a b\nc\n' "a b c" 0
x "blanks and newlines delimit" ' a\t b \n\nc\n' "[a][b][c]" 0 $A
x "double quotes" 'a "b c" d\n' "[a][b c][d]" 0 $A
x "single quotes" "a 'b \"c' d\\n" "[a][b \"c][d]" 0 $A
x "empty quotes: an empty argument" 'a "" b\n' "[a][][b]" 0 $A
x "a backslash escapes" 'a\\ b c\\\\d\n' "[a b][c\\d]" 0 $A
x "a backslash before a newline" 'a\\\nb\n' "[a${nl}b]" 0 $A
x "quotes join with the rest" 'x"a b"y\n' "[xa by]" 0 $A
x "an unmatched quote: the arguments before it, status 1" 'a "b\n' "[a]" 1 $A
x "a quote cannot span lines" "a 'b\\nc'\\n" "[a]" 1 $A
x "the initial arguments first" 'c d\n' "[a][b][c][d]" 0 $A a b
x "no input: once" '' "[x]" 0 $A x
x "-r no input: not at all" '' "" 0 -r $A x
x "-r blanks only" '  \n\n' "" 0 -r $A x
x "-r with input" 'a\n' "[a]" 0 -r $A

# -n and -s
x "-n" 'a b c d e\n' "[a][b]${nl}[c][d]${nl}[e]" 0 -n 2 $A
x "-n 1" 'a b\n' "[a]${nl}[b]" 0 -n1 $A
x "-s" 'a b c d e f\n' "a b c${nl}d e f" 0 -s 12 echo
x "-s and -n" 'a b c d e f\n' "a b${nl}c d${nl}e f" 0 -s 12 -n 2 echo
x "-s an argument too long" 'aaaaaaaaaaaaaaaa b\n' "" 1 -s 12 echo
x "-x -n: when n do not fit" 'aaa bbb ccc\n' "" 1 -n 3 -x -s 14 echo
x "-n without -x: fewer" 'aaa bbb ccc\n' "aaa bbb${nl}ccc" 0 -n 3 -s 14 echo
x "a large -s is no error" 'a\n' "a" 0 -s 999999999 echo
awk 'BEGIN { for (i = 0; i < 20000; i++) printf "%08d\n", i }' > many
t "many arguments: several commands, none lost" "20000" 0 sh -c '"$1" sh -c "for a; do echo \$a; done" sh < many | wc -l | tr -d " "' sh "$U"
t "many arguments: in order" "00000000 00019999" 0 sh -c '"$1" sh -c "for a; do echo \$a; done" sh < many | sed -n "1p;\$p" | tr "\n" " " | sed "s/ \$//"' sh "$U"

# -E
x "-E" 'a b END c\n' "[a][b]" 0 -E END $A
x "-E after quote processing" 'a "END" c\n' "[a]" 0 -E END $A
x "-E not part of an argument" 'a xEND c\n' "[a][xEND][c]" 0 -E END $A
x "_ is not special without -E" 'a _ b\n' "[a][_][b]" 0 $A
x "-E empty: disabled" 'a _ b\n' "[a][_][b]" 0 -E '' $A
x "-E in a later line" 'a\nEND\nb\n' "[a]" 0 -E END $A

# -0
x "-0" 'a b\0c\0' "[a b][c]" 0 -0 $A
x "-0: quotes are not special" '"a"\0\\b\0' "[\"a\"][\\b]" 0 -0 $A
x "-0: each null byte delimits" 'a\0\0b\0' "[a][][b]" 0 -0 $A
x "-0: a newline is an ordinary byte" 'a\nb\0' "[a${nl}b]" 0 -0 $A
x "-0: no null at the end: ignored" 'a\0b' "[a]" 0 -0 $A
x "-0 -n" 'a\0b\0c\0' "[a][b]${nl}[c]" 0 -0 -n 2 $A
x "-0 and -E ''" 'a\0_\0' "[a][_]" 0 -0 -E '' $A

# -I
x "-I" 'one\ntwo\n' "[<one>][one]${nl}[<two>][two]" 0 -I {} $A "<{}>" {}
x "-I: blanks are kept, leading ones are not" '  a b \n' "[a b ]" 0 -I @ $A @
x "-I: empty lines are skipped" 'a\n\nb\n' "[a]${nl}[b]" 0 -I @ $A @
x "-I: quotes" '"a  b"  c\n' "[a  b  c]" 0 -I @ $A @
x "-I: in five arguments" 'x\n' "[x][x][x][x][x][xx]" 0 -I @ $A @ @ @ @ @ @@
x "-I: no input" '' "[@]" 0 -I @ $A @
x "-I: -r no input" '' "" 0 -r -I @ $A @
x "-I: too long (-x)" 'aaaaaaaaaa\n' "" 1 -s 20 -I @ echo @@@@

# -L (each line is one argument, as POSIX.1-2024 says)
x "-L 1" 'a b\nc\n' "[a b]${nl}[c]" 0 -L 1 $A
x "-L 2" 'a\nb\nc\n' "[a][b]${nl}[c]" 0 -L 2 $A
x "-L: empty lines are skipped" 'a\n\n\nb\n' "[a][b]" 0 -L 2 $A
x "-L: a trailing blank continues" 'a \n\nb\nc\n' "[ab]${nl}[c]" 0 -L 1 $A
x "-L: an escaped blank does not" 'a\\ \nb\n' "[a ]${nl}[b]" 0 -L 1 $A
x "-L: the last one fewer" 'a\nb\nc\n' "[a][b][c]" 0 -L 5 $A
x "-L -0" 'a b\0c\0' "[a b]${nl}[c]" 0 -0 -L 1 $A

# -I -L -n: the last
x "-n after -I" 'a b\n' "[a][b]" 0 -I @ -n 2 $A
x "-I after -n" 'a b\n' "[a b]" 0 -n 1 -I @ $A @

# -t
t "-t writes the command" "echo a b" 0 sh -c 'printf "a b\n" | "$1" -t echo 2>&1 >/dev/null' sh "$U"
t "-t and the command's output" "a b" 0 sh -c 'printf "a b\n" | "$1" -t echo 2>/dev/null' sh "$U"
t "-p without a terminal" "" 1 sh -c 'printf "a\n" | "$1" -p echo </dev/null 2>/dev/null' sh "$U"

# exit status
x "a failing command: 123, the others run" 'a\nb\n' "b" 123 -n 1 sh -c 'echo $0 >&2; [ "$0" = b ] && echo b; [ "$0" = b ]'
x "255: stop at once, 124" 'a\nb\n' "a" 124 -n 1 sh -c 'echo $0; exit 255'
x "killed: 125" 'a\n' "" 125 sh -c 'kill -s TERM $$'
x "not found: 127" 'a\n' "" 127 no_such_utility_here
x "cannot be run: 126" 'a\n' "" 126 /etc
# a directory in PATH that cannot be searched does not make it 126
$root || t "not found past an unsearchable directory: 127" "" 127 sh -c 'mkdir -p nd && chmod 0 nd && echo a | PATH=$PWD/nd:$PATH "$1" no_such_utility_here' sh "$U"
x "the utility's standard input is not xargs's" 'x\n' "[]" 0 sh -c 'read l; printf "[%s]\n" "$l"'
x "options of the utility are its own" 'a\n' "[-n][a]" 0 $A -n
x "-- ends the options" 'a\n' "[a]" 0 -- $A
x "a bad option" 'a\n' "" 1 -q echo
x "-n 0" 'a\n' "" 1 -n 0 echo
x "-n x" 'a\n' "" 1 -n x echo
x "-s without a number" 'a\n' "" 1 -s

done_tests xargs
