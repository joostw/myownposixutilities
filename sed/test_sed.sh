#!/bin/sh
# test_sed.sh - tests for sed (POSIX.1-2024).  Run: sh test_sed.sh
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
# tb NAME EXPECTED-OD-C COMMAND...  (exact bytes of stdout, as od -c)
tb() {
    _n=$1 _e=$2; shift 2
    _o=$("$@" 2>/dev/null | od -An -c | tr -s ' \n' ' ' | sed 's/^ //;s/ $//')
    if [ "$_o" = "$_e" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  %s\n  want: %s\n' "$_n" "$_o" "$_e"; fi
}
# ok NAME CONDITION-COMMAND...
ok() {
    _n=$1; shift
    if "$@"; then pass=$((pass+1)); else fail=$((fail+1)); printf 'FAIL %s\n' "$_n"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/sed}
# a UTF-8 locale for the multibyte tests, which are skipped without one (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any UTF-8 locale that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
export U8
u8() { [ -n "$U8" ]; }
T=${TMPDIR:-/tmp}/test_sed.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
printf '1\n2\n3\n4\n5\n' > n5
s() { _in=$1; shift; printf "$_in" | "$U" "$@"; }
t "p -n" "3" 0 "$U" -n 3p n5
t "range" "2
3
4" 0 "$U" -n 2,4p n5
t "range addr2 <= addr1" "4" 0 "$U" -n 4,2p n5
t "range regex" "2
3" 0 "$U" -n '/2/,/3/p' n5
t "range regex addr2 starts next line" "2
3
2" 0 s '2\n3\n2\n4\n5\n' -n '/2/,/2/p'
t "range repeats" "a
b
a
b" 0 s 'a\nb\nc\na\nb\n' -n '/a/,/b/p'
t "\$ address" "5" 0 "$U" -n '$p' n5
t "\$ across files" "x" 0 sh -c 'printf "1\n" > a; printf "x\n" > b; : > c; "$1" -n "\$p" a b c' sh "$U"
t "line numbers cumulate" "b:x" 0 sh -c '"$1" -n "2s/^/b:/p" a b' sh "$U"
t "! negation" "1
5" 0 "$U" '2,4!b;d' n5
t "! negation 2" "1
5" 0 "$U" -n '2,4!p' n5
t "d" "1
2
4
5" 0 "$U" 3d n5
t "s basic" "hexlo" 0 s 'hello\n' 's/l/x/'
t "s g" "hexxo" 0 s 'hello\n' 's/l/x/g'
t "s nth" "helxo" 0 s 'hello\n' 's/l/x/2'
t "s nth g" "abbb" 0 s 'aaaa\n' 's/a/b/2g'
t "s &" "[hello]" 0 s 'hello\n' 's/.*/[&]/'
t "s \\&" "&" 0 s 'x\n' 's/x/\&/'
t "s backref" "ba" 0 s 'ab\n' 's/\(a\)\(b\)/\2\1/'
t "s unmatched backref empty" "-b" 0 s 'b\n' -E 's/(a)?b/\1-b/'
t "s ERE" "X" 0 s 'aaa\n' -E 's/a+/X/'
t "s newline in repl" "a
b" 0 s 'ab\n' 's/a/a\
/'
t "s \\n in RE" "a-b" 0 s 'a\nb\n' 'N;s/\n/-/'
t "^ only start of ps" "a
b" 0 s 'a\nb\n' 'N;s/^b/X/'
t "\$ only end of ps" "a
X" 0 s 'a\nb\n' 'N;s/b$/X/'
t ". matches newline" "X" 0 s 'a\nb\n' 'N;s/a.b/X/'
t "empty matches g" "xbxcx" 0 s 'baaac\n' 's/a*/x/g'
t "empty matches every pos" "-a-b-c-" 0 s 'abc\n' 's/x*/-/g'
t "^ with g" "Xaa" 0 s 'aaa\n' 's/^a/X/g'
t "word boundary context" "foo Bar" 0 s 'foo bar\n' 's/\<b/B/g'
t "s p" "X
X" 0 s 'a\n' 's/a/X/p'
t "s p -n" "X" 0 s 'a\nb\n' -n 's/a/X/p'
t "s i flag" "X" 0 s 'A\n' 's/a/X/i'
t "s w" "X" 0 sh -c 'printf "a\nb\n" | "$1" -n "s/a/X/w out" && cat out' sh "$U"
t "s other delim" "a/b" 0 s 'a:b\n' 's,:,/,'
t "escaped delim literal" "X" 0 s 'a,b\n' 's,a\,b,X,'
t "escaped special delim" "X" 0 s 'a.b\n' 's.a\.b.X.'
t "delim in bracket" "aXb" 0 s 'a/b\n' 's/[/]/X/'
t "spec bracket example" "ab" 0 s 'a1b2\n' 's-[0-9]--g'
t "\\cREc address" "2" 0 "$U" -n '\%2%p' n5
t "\\cREc escaped" "a%b" 0 s 'a%%b\nab\n' -n '\%a\%b%p'
t "empty RE = last used" "X" 0 s 'abc\n' -n '/b/s//X/;s/a//;s/c//p'
t "empty RE dynamic" "aX" 0 s 'ab\n' -n '/a/{/b/s//X/p;}'
t "no previous RE" "" 1 s 'a\n' 's//x/'
t "bad backref" "" 1 s 'a\n' 's/a/\1/'
t "y" "HELLO" 0 s 'hello\n' 'y/ehlo/EHLO/'
t "y \\n" "a b" 0 s 'a\nb\n' 'N;y/\n/ /'
t "y escaped delim" "a/b" 0 s 'a,b\n' 'y/,/\//'
t "y lengths" "" 1 s 'a\n' 'y/ab/c/'
u8 && t "y UTF-8" "eaü" 0 sh -c 'printf "éaü\n" | LC_ALL=$U8 "$1" "y/é/e/"' sh "$U"
t "a" "1
A
2" 0 s '1\n2\n' '1a\
A'
t "a one-liner" "1
A
2" 0 s '1\n2\n' '1a A'
t "a multi-line" "1
A
B" 0 s '1\n' 'a\
A\
B'
t "a backslash" 'x
a\b' 0 s 'x\n' 'a\
a\\b'
t "a with -n" "A" 0 s '1\n' -n 'a\
A'
t "i" "I
1" 0 s '1\n' 'i\
I'
t "c" "C
2" 0 s '1\n2\n' '1c\
C'
t "c range: once at end" "C
4" 0 s '1\n2\n3\n4\n' '1,3c\
C'
t "c !" "1
C
C" 0 s '1\n2\n3\n' '1!c\
C'
t "a before next n" "1
A
2" 0 s '1\n2\n' -n '1{a\
A
p;n;p;}'
t "a after q" "1
A" 0 s '1\n2\n' '1{a\
A
q;}'
t "r" "1
r1
2" 0 sh -c 'printf "r1\n" > rf; printf "1\n2\n" | "$1" "1r rf"' sh "$U"
t "r missing is empty" "1
2" 0 sh -c 'printf "1\n2\n" | "$1" "1r nosuchfile"' sh "$U"
t "w" "1
3" 0 sh -c '"$1" -n "/[13]/w wout" n5 && cat wout' sh "$U"
t "w created though unused" "0" 0 sh -c 'rm -f wc; "$1" -n "/zzz/w wc" n5 && wc -c < wc | tr -d " "' sh "$U"
t "w /dev/stdout" "1
1
2" 0 s '1\n2\n' '1w /dev/stdout'
t "n" "1
3
5" 0 "$U" -n 'p;n' n5
t "n at end autoprints" "1
3" 0 s '1\n2\n3\n' 'n;d'
t "N" "1-2
3-4
5" 0 "$U" '$!N;s/\n/-/' n5
t "N at end quits silently" "1-2
3-4" 0 "$U" 'N;s/\n/-/' n5
t "D" "4
5" 0 "$U" '$!N;$!D' n5
t "P" "1" 0 s '1\n2\n' -n 'N;P;D'
t "D restarts without reading" "b" 0 s 'a\nb\n' -n '1{N;D;};p'
t "g h" "1
1" 0 s '1\n2\n' '1h;2g'
t "G H x" "2
1" 0 s '1\n2\n' -n 'H;${x;s/^\n//;G;s/\(.*\)\n\(.*\)\n.*/\2\
\1/;p;}'
t "tac" "3
2
1" 0 s '1\n2\n3\n' -n '1!G;h;$p'
t "x" "
1" 0 s '1\n' 'x;G'
t "=" "1
a
2
b" 0 s 'a\nb\n' '='
t "= last" "5" 0 "$U" -n '$=' n5
t "l" 'a\tb\\c\001$' 0 s 'a\tb\\c\001\n' -n l
t "l newline" 'a\012b$' 0 s 'a\nb\n' -n 'N;l'
t "l fold" "$(printf '%069d\\\n0$' 0)" 0 sh -c 'printf "%070d\n" 0 | "$1" -n l' sh "$U"
u8 && t "l UTF-8" 'é$' 0 sh -c 'printf "é\n" | LC_ALL=$U8 "$1" -n l' sh "$U"
u8 && t "l invalid UTF-8" '\377$' 0 sh -c 'printf "\377\n" | LC_ALL=$U8 "$1" -n l' sh "$U"
t "q" "1
2" 0 "$U" 2q n5
t "q -n" "" 0 "$U" -n 2q n5
t "b label" "1
x2" 0 s '1\n2\n' '/1/b end
s/^/x/
:end'
t "b to end" "1
x2" 0 s '1\n2\n' '/1/b;s/^/x/'
t "t" "yes" 0 s 'a\n' 's/a/b/;t ok
s/.*/no/;b
:ok
s/.*/yes/'
t "t resets" "no" 0 s 'a\n' 's/a/b/;t x
:x
t ok
s/.*/no/;b
:ok
s/.*/yes/'
t "t reset by N" "NO" 0 s 'a\nb\n' 's/a/A/;N;tx
s/^.*$/NO/;b
:x
s/^.*$/YES/'
t "loop join" "a b c" 0 s 'a\nb\nc\n' ':a
N
$!ba
s/\n/ /g'
t "missing label" "" 1 s 'a\n' 'b nowhere'
t "block" "2
3" 0 "$U" -n '2,3{p;}' n5
t "nested block" "3" 0 "$U" -n '2,4{/3/{p;};}' n5
t "block on one line" "2" 0 "$U" -n '2{p}' n5
t "unmatched {" "" 1 "$U" -n '2{p' n5
t "unexpected }" "" 1 "$U" -n 'p;}' n5
t "comment" "1" 0 "$U" -n '# hello
1p' n5
t "#n" "2" 0 "$U" '#n
2p' n5
t "-e joined by newline" "1
A
2" 0 s '1\n2\n' -e '1a\' -e 'A'
t "-e order" "x2" 0 s '1\n' -e 's/1/2/' -e 's/^/x/'
t "-f" "x1" 0 sh -c 'printf "s/^/x/\n" > sc; printf "1\n" | "$1" -f sc' sh "$U"
t "-f then -e" "xy1" 0 sh -c 'printf "s/^/y/\n" > sc; printf "1\n" | "$1" -f sc -e "s/^/x/"' sh "$U"
t "files and -" "a
b
c" 0 sh -c 'printf "a\n" > f1; printf "c\n" > f3; printf "b\n" | "$1" "" f1 - f3' sh "$U"
t "missing file continues" "a" 2 sh -c '"$1" "" nofile f1' sh "$U"
tb "no final newline gets one" 'a b c \n' sh -c 'printf abc | "$1" -n p' sh "$U"
t "blanks and semicolons" "2" 0 "$U" -n ' ; 2 p ; ' n5
t "s flags then blank" "X" 0 s 'a\n' 's/a/X/ ;'
t "extra characters" "" 1 s 'a\n' 'px'
t "2 addresses for =" "" 1 s 'a\n' '1,2='
t "address 0" "" 1 s 'a\n' '0p'
t "0 addresses for :" "" 1 s 'a\n' '1:a'
t "unknown command" "" 1 s 'a\n' 'k'
t "bad RE" "" 1 s 'a\n' 's/\(/x/'
t "unterminated s" "" 1 s 'a\n' 's/a/b'
t "no script" "" 1 "$U"
t "bad option" "" 1 "$U" -z p n5
[ -c /dev/full ] && t "write error" "" 4 sh -c '"$1" p n5 >/dev/full' sh "$U"
tb "NUL in pattern space" 'a \0 X \n' sh -c 'printf "a\\000b\\n" | "$1" s/b/X/' sh "$U"
t "\\n in replacement is n (POSIX)" "anb" 0 s 'ab\n' 's/a/a\n/'
t "G H x" "2n1" 0 s '1\n2\n' -n 'H;${x;s/^\n//;G;s/\(.*\)\n\(.*\)\n.*/\2n\1/;p;}'
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: sed p f -n: -n is a file" "x
x" 2 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" p f -n; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests sed
