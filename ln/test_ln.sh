#!/bin/sh
# test_ln.sh - tests for ln (POSIX.1-2024).  Run: sh test_ln.sh
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
U=${U:-$PWD/ln}
T=${TMPDIR:-/tmp}/test_ln.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
ino() { ls -i "$1" | awk '{print $1}'; }
echo data > f
t "hard link" "" 0 "$U" f g
ok "same inode" test "$(ino f)" = "$(ino g)"
t "exists, no -f" "" 1 "$U" f g
t "-f replaces" "data" 0 sh -c 'echo other > h; "$1" -f f h; cat h' sh "$U"
t "-f result" "data" 0 cat h
t "-s" "" 0 "$U" -s f s
t "-s target" "f" 0 readlink s
t "-s dangling ok" "" 0 "$U" -s nowhere s2
t "-s dangling target" "nowhere" 0 readlink s2
mkdir d
t "into dir (g2 missing)" "" 1 "$U" f g2 d
ok "dir/f" test -f d/f
ok "dir/g2 absent" test ! -e d/g2
t "into dir status" "" 1 "$U" f nofile d
t "many to non-dir" "" 1 "$U" f g h
t "same entry" "" 1 "$U" -f f f
ok "file survives" test -f f
t "same entry via dir" "" 1 "$U" -f f ./f
ok "still there" test -f f
t "hard link to dir" "" 1 "$U" d dlink
t "-L follows symlink" "" 0 "$U" -L s lf
ok "-L same inode as f" test "$(ino f)" = "$(ino lf)"
t "-P links the symlink" "" 0 "$U" -P s lp
ok "-P result is symlink" test -h lp
t "-P link target" "f" 0 readlink lp
t "missing source" "" 1 "$U" nope x
t "one operand" "" 1 "$U" f
t "bad option" "" 1 "$U" -x f y
t "-f over dir symlink -> into dir" "" 0 sh -c '"$1" -s d sd && "$1" -f f sd && test -f d/f' sh "$U"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: ln f g -s: three operands, -s the target" "f" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f g -s; r=$?; ls; exit $r; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests ln
