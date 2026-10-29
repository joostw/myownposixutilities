#!/bin/sh
# test_cp.sh - tests for cp (POSIX.1-2024).  Run: sh test_cp.sh
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
U=${U:-$PWD/cp}
T=${TMPDIR:-/tmp}/test_cp.$$
mkdir -m 700 "$T" || exit 2
# fstat: exact file status, which POSIX ls does not show (../testlib/fstat.c)
cc=${CC:-}
[ -n "$cc" ] || for c in c17 c99 cc; do command -v $c >/dev/null 2>&1 && { cc=$c; break; }; done
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T" "$T.fstat"' EXIT
$cc -o "$T.fstat" ../testlib/fstat.c || { echo "cannot compile ../testlib/fstat.c"; exit 2; }
fstat() { "$T.fstat" "$@"; }
cd "$T" || exit 2
umask 022
root=false; [ "$(id -u)" = 0 ] && root=true
# tree DIR: the files of a tree with their types, and the contents of the
# regular ones
cat > tree <<'EOF'
cd "$1" && find . | sort | while read -r f; do
    if [ -h "$f" ]; then echo "$f -> $(readlink "$f")"
    elif [ -d "$f" ]; then echo "$f/"
    elif [ -p "$f" ]; then echo "$f |"
    else echo "$f: $(cat "$f")"; fi
done
EOF

# the first form
echo hello > f
t "a file" "hello" 0 sh -c '"$1" f g && cat g' sh "$U"
t "a new file: the source mode and the mask" "750" 0 sh -c 'chmod 754 f && umask 027 && "$1" f new1 && "$2" %a new1' sh "$U" "$T.fstat"
t "an existing file keeps its mode" "600" 0 sh -c 'echo old > g && chmod 600 g && "$1" f g && "$2" %a g' sh "$U" "$T.fstat"
t "an existing file: the contents" "hello" 0 cat g
t "truncated" "x" 0 sh -c 'printf "a long line\n" > g; echo x > s1; "$1" s1 g; cat g' sh "$U"
t "an empty file" "0" 0 sh -c ': > e; "$1" e e2; "$2" %s e2' sh "$U" "$T.fstat"
awk 'BEGIN { for (i = 0; i < 100000; i++) print i }' > big
t "a large file" "" 0 sh -c '"$1" big big2 && cmp big big2' sh "$U"
t "a missing source" "" 1 "$U" nofile x
ok "nothing created" test ! -e x
t "the same file" "" 1 "$U" f f
t "the same file by another name" "" 1 "$U" f ./f
t "the same file through a link" "" 1 sh -c 'ln -s f lf; "$1" f lf' sh "$U"
t "the same file: unchanged" "hello" 0 cat f
t "a symbolic link: its target" "hello" 0 sh -c '"$1" lf g2 && test ! -h g2 && cat g2' sh "$U"
t "-P: the link itself" "f" 0 sh -c '"$1" -P lf g3 && readlink g3' sh "$U"
t "a directory without -R" "" 1 sh -c 'mkdir dd; "$1" dd ee' sh "$U"
ok "a directory without -R: nothing" test ! -e ee
t "the target in a missing directory" "" 1 "$U" f nodir/x
t "no operands" "" 2 "$U"
t "one operand" "" 2 "$U" f
t "bad option" "" 2 "$U" -x f g
t "--" "hello" 0 sh -c '"$1" -- f -g && cat ./-g' sh "$U"
[ -c /dev/full ] && t "a write error" "" 1 "$U" f /dev/full

# the second form
mkdir to
t "into a directory" "hello x" 0 sh -c '"$1" f g to && echo $(cat to/f) $(cat to/g)' sh "$U"
t "into a directory with a slash" "hello" 0 sh -c '"$1" f to/ && cat to/f' sh "$U"
t "the target is not a directory" "" 1 "$U" f g e
t "the target does not exist" "" 1 "$U" f g nodir
rm to/f
t "a directory among files" "" 1 "$U" dd f to
ok "a directory among files: the file copied" test -f to/f
ok "the directory not copied" test ! -e to/dd

# -R
mkdir -p s/a/b; echo 1 > s/f; echo 2 > s/a/b/g; ln -s f s/l; ln -s ../../out s/a/lo; mkfifo s/p
mkdir out; echo o > out/o
t "-R to a new name" "./
./a/
./a/b/
./a/b/g: 2
./a/lo -> ../../out
./f: 1
./l -> f
./p |" 0 sh -c '"$1" -R s n && sh tree n' sh "$U"
t "-R into a directory" "1" 0 sh -c 'mkdir in && "$1" -R s in && cat in/s/f' sh "$U"
t "-R a slash at the end" "1" 0 sh -c 'mkdir in2 && "$1" -R s/ in2 && cat in2/s/f' sh "$U"
t "-R into an existing copy" "2" 0 sh -c '"$1" -R s in && cat in/s/a/b/g' sh "$U"
t "-RP" "f" 0 sh -c '"$1" -RP s n2 && readlink n2/l' sh "$U"
t "-RL: the links are followed" "./
./a/
./a/b/
./a/b/g: 2
./a/lo/
./a/lo/o: o
./f: 1
./l: 1
./p |" 0 sh -c '"$1" -RL s n3 && sh tree n3' sh "$U"
ln -s s ls
t "-RH: the operand is followed" "f" 0 sh -c '"$1" -RH ls n4 && test -d n4 && readlink n4/l' sh "$U"
t "-RP: the operand is copied" "s" 0 sh -c '"$1" -RP ls n5 && readlink n5' sh "$U"
t "-R: -P is the default" "s" 0 sh -c '"$1" -R ls n6 && readlink n6' sh "$U"
t "-R -H -L -P: the last" "s" 0 sh -c '"$1" -R -H -L -P ls n7 && readlink n7' sh "$U"
t "-R several into a directory" "1 hello" 0 sh -c 'mkdir m && "$1" -R s f m && echo $(cat m/s/f) $(cat m/f)' sh "$U"
t "-R more than two, no directory" "" 1 "$U" -R s f nodir
t "-R a directory over a file" "" 1 sh -c 'mkdir w && : > w/s && "$1" -R s w' sh "$U"
t "-R a directory into itself" "" 1 "$U" -R s s/x
ok "into itself: nothing" test ! -e s/x
t "-R a directory into itself, the directory exists" "" 1 "$U" -R s s
t "-R a directory to itself" "" 1 "$U" -R s ./s
t "-R a directory into a link to itself" "" 1 sh -c 'ln -s . s/self; "$1" -R s s/self/x' sh "$U"
rm s/self
ln -s .. s/a/up
t "-RL a cycle: reported" "" 1 "$U" -RL s cy
ok "-RL a cycle: the rest copied" test -f cy/f -a -d cy/a/b
ok "-RL a cycle: no copy of it" test ! -e cy/a/up
rm s/a/up
mkdir -p sp/x; echo y > sp/x/y; chmod 555 sp/x sp
t "-R a read-only directory: copied, then its mode" "555 y" 0 sh -c '"$1" -R sp spc && echo $("$2" %a spc/x) $(cat spc/x/y)' sh "$U" "$T.fstat"
chmod 755 sp sp/x
t "-R a new directory: the mask" "700" 0 sh -c 'chmod 755 s && umask 077 && "$1" -R s um && "$2" %a um' sh "$U" "$T.fstat"
t "-R an existing directory keeps its mode" "711" 0 sh -c 'mkdir ex && chmod 711 ex && "$1" -R s/a/. ex && "$2" %a ex' sh "$U" "$T.fstat"
t "-R a FIFO: its mode and the mask" "640" 0 sh -c 'mkfifo -m 664 fi && umask 027 && "$1" -R fi fi2 && test -p fi2 && "$2" %a fi2' sh "$U" "$T.fstat"
t "-R an existing link: replaced" "f" 0 sh -c 'rm n/l && ln -s nowhere n/l && "$1" -R s/l n/l && readlink n/l' sh "$U"
t "-R an existing FIFO: replaced" "" 0 sh -c '"$1" -R s/p n/p && test -p n/p' sh "$U"
t "-Ri an existing link: no" "nowhere" 0 sh -c 'rm n/l && ln -s nowhere n/l && echo n | "$1" -Ri s/l n/l && readlink n/l' sh "$U"
t "-R a link over a directory: an error" "" 1 sh -c 'mkdir -p q/s/l && "$1" -R s q' sh "$U"
ok "-R a link over a directory: the directory stays" test -d q/s/l -a -f q/s/f
mkdir m2
t "-R a missing source in a list" "" 1 "$U" -R nofile s m2
ok "-R a missing source in a list: the others copied" test -f m2/s/f

# -p
echo p > pf; chmod 4751 pf; TZ=UTC0 touch -t 200102030405.06 pf; TZ=UTC0 touch -a -t 200203040506.07 pf
t "-p: the times" "981173106 1015218367" 0 sh -c '"$1" -p pf pc && echo $("$2" %Y pc) $("$2" %X pc)' sh "$U" "$T.fstat"
t "-p: the mode" "4751" 0 fstat %a pc
t "-p: the mode over an existing file" "4751" 0 sh -c 'echo x > pe && chmod 600 pe && "$1" -p pf pe && "$2" %a pe' sh "$U" "$T.fstat"
if $root; then
    chown 1234:5678 pf
    t "-p: the owner and group" "1234 5678" 0 sh -c '"$1" -p pf po && echo $("$2" %u po) $("$2" %g po)' sh "$U" "$T.fstat"
fi
mkdir -p pd/s; echo z > pd/s/z; chmod 750 pd/s; touch -t 199901010000 pd/s/z pd/s pd
t "-Rp: the directory times, after the contents" "$(fstat %Y pd) $(fstat %Y pd/s) 750" 0 sh -c '"$1" -Rp pd pdc && echo $("$2" %Y pdc) $("$2" %Y pdc/s) $("$2" %a pdc/s)' sh "$U" "$T.fstat"
t "-Rp: a link's own times" "$(fstat %Y s/l)" 0 sh -c 'touch -t 199001010000 s/f; "$1" -Rp s/l pl && "$2" %Y pl' sh "$U" "$T.fstat"

# -i and -f
echo new > src; echo old > dst
t "-i no" "old" 0 sh -c 'echo n | "$1" -i src dst; cat dst' sh "$U"
t "-i prompt names the file" "1" 0 sh -c 'echo n | "$1" -i src dst 2>&1 | grep -c dst' sh "$U"
t "-i yes" "new" 0 sh -c 'echo y | "$1" -i src dst; cat dst' sh "$U"
t "-i no prompt for a new file" "" 0 sh -c '"$1" -i src dst9 </dev/null 2>&1' sh "$U"
t "-f over a directory: an error" "" 1 sh -c 'mkdir -p dir9/src && "$1" -f src dir9' sh "$U"
if ! $root; then
    echo old > ro; chmod 444 ro
    t "an unwritable target" "" 1 "$U" src ro
    t "an unwritable target: unchanged" "old" 0 cat ro
    t "-f an unwritable target: removed first" "new" 0 sh -c '"$1" -f src ro; cat ro' sh "$U"
    t "-f the new file: the source mode" "644" 0 fstat %a ro
    echo s > nr; chmod 0 nr
    t "an unreadable source" "" 1 "$U" nr nrc
    mkdir -p ur/d; echo u > ur/d/u; echo v > ur/v; chmod 0 ur/d
    t "-R an unreadable directory: reported" "" 1 "$U" -R ur urc
    t "-R an unreadable directory: the rest copied" "v" 0 cat urc/v
    chmod 755 ur/d
fi

# depth: past PATH_MAX, and under a low limit on open files
name=$(awk 'BEGIN { while (i++ < 100) printf "n" }')
mkdir deep
(cd -P deep && i=0 && while [ $i -lt 60 ]; do mkdir "$name" && cd -P "$name" || exit 1; i=$((i+1)); done; echo leaf > leaf) || echo "could not make the tree"
t "-R deeper than PATH_MAX, 16 open files" "" 0 sh -c 'ulimit -n 16 && "$1" -R deep deep2' sh "$U"
t "-R the deepest file" "leaf" 0 sh -c 'cd -P deep2 && while [ -d "$1" ]; do cd -P "$1"; done; cat leaf' sh "$name"
mkdir lim
(cd lim && i=0 && while [ $i -lt 150 ]; do mkdir a && cd a || exit 1; echo $i > f; i=$((i+1)); done)
t "-R 150 levels with 16 open files" "149" 0 sh -c 'ulimit -n 16 && "$1" -R lim lim2 && cd lim2 && while [ -d a ]; do cd a; done; cat f' sh "$U"

# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: cp f g -R: three operands, -R the target" "f" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f g -R; r=$?; ls; exit $r; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests cp
