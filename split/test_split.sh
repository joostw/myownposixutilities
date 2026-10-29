#!/bin/sh
# test_split.sh - tests for split (POSIX.1-2024).  Run: sh test_split.sh
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
U=${U:-$PWD/split}
T=${TMPDIR:-/tmp}/test_split.$$
mkdir -m 700 "$T" || exit 2
# fstat: exact file status, which POSIX ls does not show (../testlib/fstat.c)
cc=${CC:-}
[ -n "$cc" ] || for c in c17 c99 cc; do command -v $c >/dev/null 2>&1 && { cc=$c; break; }; done
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T" "$T.fstat"' EXIT
$cc -o "$T.fstat" ../testlib/fstat.c || { echo "cannot compile ../testlib/fstat.c"; exit 2; }
fstat() { "$T.fstat" "$@"; }
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# sp DIR ARGS...: split in a new directory DIR; then the files and sizes
mkdir in
awk 'BEGIN { for (i = 1; i <= 2500; i++) print i }' > in/lines
printf 'a\nb\nc\nd\ne' > in/nonl
awk 'BEGIN { srand(1); for (i = 0; i < 5000; i++) printf "%c", 65 + int(rand() * 26) }' > in/bytes
: > in/empty
listing() { for f in *; do printf '%s %s\n' "$f" "$(wc -c < "$f" | tr -d ' ')"; done; }
sp() { _d=$1; shift; mkdir "$_d" && cd "$_d" && "$U" "$@"; _r=$?; listing; cd "$T"; return $_r; }
t "1000 lines a file, xaa xab xac" "xaa 3893${nl}xab 5000${nl}xac 2500" 0 sp d1 ../in/lines
t "the pieces put together" "same" 0 sh -c 'cat d1/x* | cmp -s - in/lines && echo same'
t "-l 2: a partial last line" "xaa 4${nl}xab 4${nl}xac 1" 0 sp d2 -l 2 ../in/nonl
t "-l 2: the last line without its newline" "e" 0 cat d2/xac
t "-b" "xaa 2000${nl}xab 2000${nl}xac 1000" 0 sp d3 -b 2000 ../in/bytes
t "-b k" "xaa 2048${nl}xab 2048${nl}xac 904" 0 sp d4 -b 2k ../in/bytes
t "-b m" "xaa 5000" 0 sp d5 -b 1m ../in/bytes
t "-b: the pieces together" "same" 0 sh -c 'cat d3/x* | cmp -s - in/bytes && echo same'
t "a name" "pre.aa 3893${nl}pre.ab 5000${nl}pre.ac 2500" 0 sp d6 ../in/lines pre.
t "-a 3" "xaaa 3893${nl}xaab 5000${nl}xaac 2500" 0 sp d7 -a 3 ../in/lines
t "-a 1, -l 1: a to e" "xa 2${nl}xb 2${nl}xc 2${nl}xd 2${nl}xe 1" 0 sp d8 -a 1 -l 1 ../in/nonl
t "the standard input" "xaa 4${nl}xab 4${nl}xac 1" 0 sh -c 'mkdir d9 && cd d9 && "$1" -l 2 < ../in/nonl && for f in *; do echo "$f $(wc -c < $f | tr -d " ")"; done' sh "$U"
t "- for the standard input" "y.aa 4${nl}y.ab 4${nl}y.ac 1" 0 sh -c 'mkdir d10 && cd d10 && "$1" -l 2 - y. < ../in/nonl && for f in *; do echo "$f $(wc -c < $f | tr -d " ")"; done' sh "$U"
t "an empty input: no files" "" 0 sh -c 'mkdir d11 && cd d11 && "$1" ../in/empty && ls' sh "$U"
t "the suffixes run out: the last file full, then an error" "xa 2${nl}xb 2${nl}xc 2" 1 sh -c 'mkdir d12 && cd d12 && awk "BEGIN { for (i = 0; i < 27; i++) print \"x\" }" > ../in/27 && "$1" -a 1 -l 1 ../in/27 2>/dev/null; r=$?; ls | sed -n "1,3p" | while read f; do echo "$f $(wc -c < $f | tr -d " ")"; done; exit $r' sh "$U"
t "the suffixes run out: 26 files kept" "26" 0 sh -c 'ls d12 | wc -l | tr -d " "'
t "the suffixes run out: the last is xz" "xz" 0 sh -c 'ls d12 | tail -n 1'
t "exactly 26 pieces: no error" "26" 0 sh -c 'mkdir d13 && cd d13 && awk "BEGIN { for (i = 0; i < 26; i++) print \"x\" }" | "$1" -a 1 -l 1 && ls | wc -l | tr -d " "' sh "$U"
t "existing files are overwritten" "xaa 1" 0 sh -c 'mkdir d14 && cd d14 && echo old old old > xaa && printf "z" | "$1" && for f in *; do echo "$f $(wc -c < $f | tr -d " ")"; done' sh "$U"
t "the files: mode 0666 less the mask" "640" 0 sh -c 'mkdir d15 && cd d15 && umask 027 && echo x | "$1" && "$2" %a xaa' sh "$U" "$T.fstat"
t "a name too long for {NAME_MAX}: no files" "" 1 sh -c 'mkdir d16 && cd d16 && echo x | "$1" - $(awk "BEGIN { while (i++ < 254) printf \"n\" }"); r=$?; ls | grep . && exit 9; exit $r' sh "$U"
# as the system's split makes them
for o in "-a 3 -l 1" "-l 999" "-a 3 -b 7" "-b 777" "-b 1k" "-a 4 -l 300"; do
    t "as the system's split: $o" "same" 0 sh -c 'rm -rf A B && mkdir A B && (cd A && "$1" $2 ../in/lines p) && (cd B && split $2 ../in/lines p) && for f in A/*; do cmp -s "$f" "B/${f#A/}" || exit 1; done && [ "$(ls A)" = "$(ls B)" ] && echo same' sh "$U" "$o"
done
t "a missing file" "" 1 "$U" no_such_file
t "-l 0" "" 2 "$U" -l 0 in/lines
t "-l x" "" 2 "$U" -l x in/lines
t "-b 2g" "" 2 "$U" -b 2g in/lines
t "-l and -b" "" 2 "$U" -l 2 -b 2 in/lines
t "three operands" "" 2 "$U" in/lines a b
t "an unknown option" "" 2 "$U" -x in/lines
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: split file -l 5: -l is the name" "-laa" 0 sh -c 'mkdir d17 && cd d17 && "$1" ../in/nonl -l && ls' sh "$U"

done_tests split
