#!/bin/sh
# test_ls.sh - tests for ls (POSIX.1-2024).  Run: sh test_ls.sh
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
U=${U:-$PWD/ls}
# a UTF-8 locale for the multibyte tests, which are skipped without one (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any UTF-8 locale that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
export U8
u8() { [ -n "$U8" ]; }
T=${TMPDIR:-/tmp}/test_ls.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
# fstat: exact file status, which POSIX ls does not show (../testlib/fstat.c)
cc=${CC:-}
[ -n "$cc" ] || for c in c17 c99 cc; do command -v $c >/dev/null 2>&1 && { cc=$c; break; }; done
trap 'rm -rf "$T" "$T.fstat"' EXIT
$cc -o "$T.fstat" ../testlib/fstat.c || { echo "cannot compile ../testlib/fstat.c"; exit 2; }
fstat() { "$T.fstat" "$@"; }
cd "$T" || exit 2
export LC_ALL=C TZ=UTC0
unset COLUMNS
mkdir d e
printf 'xxxx' > d/b; : > d/a; : > .hidden; printf '1234567890' > big
ln -s d sl; ln -s nowhere dang; mkfifo fifo; : > exe; chmod 755 exe
touch -t 202001010000 d/a; touch -t 202101010000 d/b
t "no operand = ." "big
d
dang
e
exe
fifo
sl" 0 "$U"
t "-d no operand" "." 0 "$U" -d
t "-a" ".
..
.hidden" 0 sh -c '"$1" -a | head -n 3' sh "$U"
t "-A" ".hidden" 0 sh -c '"$1" -A | head -n 1' sh "$U"
t "-a then -A" ".hidden" 0 sh -c '"$1" -a -A | head -n 1' sh "$U"
t "-A then -a" "." 0 sh -c '"$1" -A -a | head -n 1' sh "$U"
t "dir operand" "a
b" 0 "$U" d
t "symlink to dir operand: contents" "a
b" 0 "$U" sl
t "-d symlink operand" "sl" 0 "$U" -d sl
t "-F symlink operand" "sl@" 0 "$U" -F sl
t "-F -H follows" "a
b" 0 "$U" -FH sl
t "-F" "big
d/
dang@
e/
exe*
fifo|
sl@" 0 "$U" -F
t "-p" "d/
e/" 0 sh -c '"$1" -p | grep /' sh "$U"
t "-F then -p" "d/
e/" 0 sh -c '"$1" -F -p | grep "[/@*|]"' sh "$U"
t "files first, then dirs" "big
exe

d:
a
b

e:" 0 "$U" e exe d big
t "-r" "b
a" 0 "$U" -r d
t "-t" "b
a" 0 "$U" -t d
t "-tr" "a
b" 0 "$U" -tr d
t "-S" "big
exe" 0 sh -c '"$1" -S big exe | head -n 2; ' sh "$U"
t "-S dir" "b
a" 0 "$U" -S d
t "-S then -t" "a
b" 0 sh -c 'touch -t 201901010000 d/b; "$1" -S -t d; touch -t 202101010000 d/b' sh "$U"
t "-t then -S" "b
a" 0 "$U" -t -S d
t "-t -S -c: -c is last of S,t,c,u" "a" 0 sh -c 'touch d/a; "$1" -t -S -c d | head -n 1; touch -t 202001010000 d/a' sh "$U"
t "-f order is directory order, includes ." "." 0 sh -c '"$1" -f d | grep -x "\."' sh "$U"
t "-1 keeps long format" "1" 0 sh -c '"$1" -l -1 d | head -n 1 | grep -c "^total "' sh "$U"
t "-C after -l disables long" "a  b" 0 sh -c 'COLUMNS=80 "$1" -l -C d' sh "$U"
t "-l after -C" "1" 0 sh -c '"$1" -C -l d | head -n 1 | grep -c "^total "' sh "$U"
t "-l fields" "-rw-r--r-- 1 4 Jan 1 2021 b" 0 sh -c 'chmod 644 d/b; "$1" -ln d | sed -n 3p | awk "{print \$1, \$2, \$5, \$6, \$7, \$8, \$9}"' sh "$U"
t "-l old date: two spaces before the year" "1" 0 sh -c '"$1" -l d | grep -c "Jan  1  2021 b$"' sh "$U"
t "-l recent time" "1" 0 sh -c 'touch d/a; "$1" -l d | sed -n 2p | grep -c " [0-9][0-9]:[0-9][0-9] a$"; touch -t 202001010000 d/a' sh "$U"
t "-l future time shows year" "1" 0 sh -c 'touch -t 209901010000 e; "$1" -ld e | grep -c "Jan  1  2099 e$"' sh "$U"
t "-l symlink" "sl -> d" 0 sh -c '"$1" -l sl | sed "s/.* sl/sl/"' sh "$U"
t "-lF symlink" "sl@ -> d/" 0 sh -c '"$1" -lF sl | sed "s/.* sl/sl/"' sh "$U"
t "-lp symlink" "sl -> d/" 0 sh -c '"$1" -lp sl | sed "s/.* sl/sl/"' sh "$U"
t "-lF dangling" "dang@ -> nowhere" 0 sh -c '"$1" -lF dang | sed "s/.* dang/dang/"' sh "$U"
t "-lL" "d" 0 sh -c '"$1" -lLd sl | cut -c1' sh "$U"
t "-n numeric" "0" 0 sh -c 'chown 0:0 big 2>/dev/null; "$1" -n big | awk "{print \$3}"' sh "$U"
t "-g omits owner" "8" 0 sh -c '"$1" -g big | awk "{print NF}"' sh "$U"
t "-o omits group" "8" 0 sh -c '"$1" -o big | awk "{print NF}"' sh "$U"
t "-go" "7" 0 sh -c '"$1" -go big | awk "{print NF}"' sh "$U"
t "-i" "1" 0 sh -c 'i=$("$2" %i big); "$1" -i big | grep -c "^$i big$"' sh "$U" "$T.fstat"
t "-s -k" "1" 0 sh -c 'b=$("$2" %b big); k=$(( (b + 1) / 2 )); "$1" -sk big | grep -c "^$k big$"' sh "$U" "$T.fstat"
t "total line" "1" 0 sh -c '"$1" -s d | grep -c "^total "' sh "$U"
t "-m" "a, b" 0 "$U" -m d
t "-m wraps" "aaaaaaaaaa, bbbbbbbbbb,
cccccccccc" 0 sh -c 'mkdir m; touch m/aaaaaaaaaa m/bbbbbbbbbb m/cccccccccc; COLUMNS=30 "$1" -m m' sh "$U"
t "-C columns down" "a  c
b  d" 0 sh -c 'mkdir c; touch c/a c/b c/c c/d; COLUMNS=5 "$1" -C c' sh "$U"
t "-x columns across" "a  b
c  d" 0 sh -c 'COLUMNS=5 "$1" -x c' sh "$U"
t "-C exact fit" "a  b  c  d" 0 sh -c 'COLUMNS=10 "$1" -C c' sh "$U"
t "-q" "a?b" 0 sh -c 'mkdir q; touch "q/a	b"; "$1" -q q' sh "$U"
t "-q newline" "a?b" 0 sh -c 'touch "q/x"; rm q/*; touch "q/a
b"; "$1" -q q' sh "$U"
t "-R" "r:
s

r/s:
t" 0 sh -c 'mkdir -p r/s; touch r/s/t; "$1" -R r' sh "$U"
t "-R trailing slash" "r/:
s

r/s:
t" 0 sh -c '"$1" -R r/' sh "$U"
t "-R no symlink follow" "0" 1 sh -c 'ln -s ../d r/ln; "$1" -R r | grep -c "r/ln:"' sh "$U"
t "-RL loop detected" "1" 0 sh -c 'ln -s .. r/s/up; "$1" -RL r 2>err >/dev/null; s=$?; grep -c loop err; exit $((s != 1))' sh "$U"
t "missing operand" "big" 1 "$U" nope big
t "missing operand message" "1" 0 sh -c '"$1" nope 2>&1 | grep -c nope' sh "$U"
t "bad option" "" 2 "$U" -Z
t "options after operand are files" "big" 1 "$U" big -l
u8 && t "UTF-8 column width" "b  é" 0 sh -c 'mkdir u; touch u/é u/b; LC_ALL=$U8 COLUMNS=4 "$1" -C u | head -n 1' sh "$U"
[ -c /dev/full ] && t "write error" "" 1 sh -c '"$1" >/dev/full' sh "$U"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: ls f -l: -l is a file" "f" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f -l; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests ls
