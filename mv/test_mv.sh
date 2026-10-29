#!/bin/sh
# test_mv.sh - tests for mv (POSIX.1-2024).  Run: sh test_mv.sh
# Moves to another file system are tested in $MV_OTHER_FS, or else in
# /dev/shm, when that is a different file system; otherwise they are
# skipped (and not counted).
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
U=${U:-$PWD/mv}
T=${TMPDIR:-/tmp}/test_mv.$$
mkdir -m 700 "$T" || exit 2
# fstat: exact file status, which POSIX ls does not show (../testlib/fstat.c)
cc=${CC:-}
[ -n "$cc" ] || for c in c17 c99 cc; do command -v $c >/dev/null 2>&1 && { cc=$c; break; }; done
X=
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T" "$T.fstat" $X' EXIT
$cc -o "$T.fstat" ../testlib/fstat.c || { echo "cannot compile ../testlib/fstat.c"; exit 2; }
fstat() { "$T.fstat" "$@"; }
cd "$T" || exit 2
root=false; [ "$(id -u)" = 0 ] && root=true

# renaming
echo a > a
t "rename" "a" 0 sh -c '"$1" a b && cat b' sh "$U"
ok "rename: the old name gone" test ! -e a
echo c > c
t "over a file" "c" 0 sh -c '"$1" c b && cat b' sh "$U"
t "a missing source" "" 1 "$U" nofile x
t "the same entry" "" 1 "$U" b b
t "the same entry by another name" "" 1 "$U" b ./b
ok "the same entry: kept" test -f b
ln b hl
t "hard links: the source entry removed" "c" 0 sh -c '"$1" hl b && cat b' sh "$U"
ok "hard links: hl gone" test ! -e hl
mkdir d e
t "into a directory" "c" 0 sh -c '"$1" b d && cat d/b' sh "$U"
t "into a directory with a slash" "x" 0 sh -c 'echo x > x && "$1" x d/ && cat d/x' sh "$U"
t "several into a directory" "1 2" 0 sh -c 'echo 1 > p; echo 2 > q; "$1" p q e && echo $(cat e/p) $(cat e/q)' sh "$U"
t "several, the target not a directory" "" 1 sh -c 'echo 1 > p; echo 2 > q; "$1" p q nodir' sh "$U"
ok "several: nothing moved" test -f p -a -f q
t "a slash at the end, the source not a directory" "" 1 "$U" p newname/
ok "the slash: nothing moved" test -f p -a ! -e newname
t "a directory to a new name" "c" 0 sh -c '"$1" d dd && cat dd/b' sh "$U"
t "a directory to a new name with a slash" "c" 0 sh -c '"$1" dd/ d2/ && cat d2/b' sh "$U"
t "a directory over an empty directory" "c" 0 sh -c 'mkdir empty && "$1" d2 empty && test -f empty/d2/b && "$1" empty/d2 e/emp && mkdir em2 && "$1" e/emp em2/ && cat em2/emp/b' sh "$U"
t "a directory over a non-empty one" "" 1 sh -c 'mkdir -p x1/y x2/y && : > x2/y/f && "$1" x1/y x2' sh "$U"
t "a directory over a file" "" 1 sh -c 'mkdir dz && : > fz && "$1" dz fz' sh "$U"
t "a file over a directory" "" 1 sh -c 'mkdir -p dy/fy && : > fy && "$1" fy dy' sh "$U"
t "a directory into itself" "" 1 sh -c 'mkdir -p self/sub && "$1" self self/sub' sh "$U"
ok "into itself: kept" test -d self/sub
t "a symbolic link: the link moves" "p" 0 sh -c 'ln -s p lp && "$1" lp lq && readlink lq' sh "$U"
t "a target that is a link to a directory" "1" 0 sh -c 'ln -s e le && echo 1 > z && "$1" z le && cat e/z' sh "$U"
t "a dangling link as the target: replaced" "p" 0 sh -c 'ln -s nowhere dang && "$1" lq dang && readlink dang' sh "$U"
t "--" "1" 0 sh -c 'echo 1 > ./-f && "$1" -- -f g1 && cat g1' sh "$U"
t "no operands" "" 2 "$U"
t "one operand" "" 2 "$U" p
t "bad option" "" 2 "$U" -x p q

# -i and -f
echo new > s; echo old > t
t "-i no" "old" 0 sh -c 'echo n | "$1" -i s t; cat t' sh "$U"
ok "-i no: the source stays" test -f s
t "-i prompt names the file" "1" 0 sh -c 'echo n | "$1" -i s t 2>&1 | grep -c t' sh "$U"
t "-i yes" "new" 0 sh -c 'echo y | "$1" -i s t; cat t' sh "$U"
t "-i no prompt for a new name" "" 0 sh -c 'echo x > s2; "$1" -i s2 s3 </dev/null 2>&1' sh "$U"
t "-fi: -i wins" "x" 0 sh -c 'echo y > s4; echo n | "$1" -f -i s4 s3; cat s3' sh "$U"
t "-if: -f wins" "y" 0 sh -c 'echo n | "$1" -i -f s4 s3; cat s3' sh "$U"
t "write-protected, not a terminal: no prompt" "w" 0 sh -c 'echo w > w1; echo v > w2; chmod 444 w2; "$1" w1 w2 </dev/null; cat w2' sh "$U"

# depth is no matter for rename
name=$(awk 'BEGIN { while (i++ < 100) printf "n" }')
mkdir deep
(cd -P deep && i=0 && while [ $i -lt 60 ]; do mkdir "$name" && cd -P "$name" || exit 1; i=$((i+1)); done; echo leaf > leaf) || echo "could not make the tree"
t "a tree deeper than PATH_MAX" "" 0 "$U" deep deep2

# to another file system
O=
for o in "${MV_OTHER_FS:-}" /dev/shm; do
    [ -n "$o" ] && [ -d "$o" ] && [ -w "$o" ] || continue
    [ "$(fstat %d "$o")" != "$(fstat %d .)" ] && { O=$o; break; }
done
if [ -n "$O" ]; then
    W=$O/test_mv.$$; X=$W
    mkdir -m 700 "$W"
    echo f > xf; chmod 4751 xf; touch -t 200102030405.06 xf; touch -a -t 200203040506.07 xf
    mt=$(fstat %Y xf); at=$(fstat %X xf)
    t "another file system: a file" "" 0 "$U" xf "$W/xf"
    t "another file system: mode, times" "4751 $mt $at" 0 sh -c 'echo $("$1" %a "$2/xf") $("$1" %Y "$2/xf") $("$1" %X "$2/xf")' sh "$T.fstat" "$W"
    t "another file system: the contents" "f" 0 cat "$W/xf"
    ok "another file system: the source removed" test ! -e xf
    t "another file system: and back" "f" 0 sh -c '"$1" "$2/xf" xf && cat xf' sh "$U" "$W"
    mkdir -p tr/a/b; echo 1 > tr/f; echo 2 > tr/a/b/g; ln -s f tr/l; ln -s ../../nowhere tr/a/dl; mkfifo tr/p; chmod 751 tr/a
    touch -t 199901010000 tr/a; $root && chown 1234:5678 tr/f
    t "another file system: a hierarchy" "1 2 f ../../nowhere 751" 0 sh -c '"$1" tr "$2/tr" && cd "$2/tr" && test -p p && echo $(cat f) $(cat a/b/g) $(readlink l) $(readlink a/dl) $("$3" %a a)' sh "$U" "$W" "$T.fstat"
    ok "another file system: the hierarchy removed" test ! -e tr
    t "another file system: a directory's times" "$(touch -t 199901010000 "$W/ref" && fstat %Y "$W/ref")" 0 "$T.fstat" %Y "$W/tr/a"
    $root && t "another file system: the owner" "1234 5678" 0 sh -c 'echo $("$1" %u "$2/tr/f") $("$1" %g "$2/tr/f")' sh "$T.fstat" "$W"
    t "another file system: over an existing file" "1" 0 sh -c 'echo old > "$2/of"; echo 1 > of; "$1" of "$2/of" && cat "$2/of"' sh "$U" "$W"
    mkdir "$W/od" od; echo 1 > od/f
    t "another file system: over an empty directory" "" 0 "$U" od "$W"
    t "another file system: over an empty directory, the contents" "1" 0 cat "$W/od/f"
    mkdir "$W/on" on; echo 1 > on/f; : > "$W/on/g"
    t "another file system: over a directory that is not empty" "" 1 "$U" on "$W"
    ok "another file system: not empty, kept" test -f on/f -a -f "$W/on/g"
    mkdir dx; : > "$W/dx"
    t "another file system: a directory over a file" "" 1 "$U" dx "$W"
    : > fx; mkdir "$W/fx"
    t "another file system: a file over a directory" "" 1 "$U" fx "$W"
    ok "another file system: mismatches kept" test -d dx -a -f fx
    t "another file system: into a directory" "2" 0 sh -c 'echo 2 > g2; "$1" g2 "$2" && cat "$2/g2"' sh "$U" "$W"
    t "another file system: a deep tree, 16 open files" "leaf" 0 sh -c 'ulimit -n 16 && "$1" deep2 "$2/deep" && cd -P "$2/deep" && while [ -d "$3" ]; do cd -P "$3"; done; cat leaf' sh "$U" "$W" "$name"
    ok "another file system: the deep tree removed" test ! -e deep2
    if ! $root; then
        mkdir -p un/d; echo u > un/d/u; chmod 0 un/d
        t "another file system: an unreadable part" "" 1 "$U" un "$W/un"
        ok "an unreadable part: the source stays" test -d un/d
        chmod 755 un/d
    fi
else
    "$U" deep2 deep3 && "$U" deep3 deep2   # still runs mv on the tree
fi

# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: mv f g -i: three operands, -i the target" "f" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f g -i; r=$?; ls; exit $r; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests mv
