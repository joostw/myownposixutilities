#!/bin/sh
# test_chmod.sh - tests for chmod (POSIX.1-2024).  Run: sh test_chmod.sh
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
U=${U:-$PWD/chmod}
T=${TMPDIR:-/tmp}/test_chmod.$$
mkdir -m 700 "$T" || exit 2
# fstat: exact file status, which POSIX ls does not show (../testlib/fstat.c)
cc=${CC:-}
[ -n "$cc" ] || for c in c17 c99 cc; do command -v $c >/dev/null 2>&1 && { cc=$c; break; }; done
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T" "$T.fstat"' EXIT
$cc -o "$T.fstat" ../testlib/fstat.c || { echo "cannot compile ../testlib/fstat.c"; exit 2; }
fstat() { "$T.fstat" "$@"; }
cd "$T" || exit 2
root=false; [ "$(id -u)" = 0 ] && root=true

# m INITIAL UMASK MODE EXPECTED: the mode of a regular file afterwards
: > f
m() {
    chmod "$1" f
    t "$1 umask $2 $3" "$4" 0 sh -c 'umask $1 && "$2" -- "$3" f && "$4" %a f' sh "$2" "$U" "$3" "$T.fstat"
}
# octal
m 0 022 644 644
m 7777 022 0 0
m 0 022 7777 7777
m 0 022 00644 644
m 0 022 4755 4755
# symbolic, the results agree with GNU chmod
m 644 022 u+x 744
m 644 022 +x 755
m 644 077 +x 744
m 644 022 a+x 755
m 777 022 -w 577
m 777 000 -w 555
m 777 022 a-w 555
m 0 022 =r 444
m 0 077 =r 400
m 777 022 = 0
m 7777 022 = 0
m 640 022 o=u 646
m 640 022 g+u 660
m 640 022 u-g 240
m 0 022 u=rwx,g=rx,o= 750
m 0 022 u=rwx,go=u-w 755
m 0 022 ug=rw,o=r,a+X 664
m 644 022 o+s 644
m 644 022 u+s 4644
m 644 022 g+s 2644
m 644 022 +s 6644
m 6755 022 ug-s 755
m 755 022 +t 1755
m 755 022 u+t 755
m 1755 022 a-t 755
m 6625 000 =o+,gu-r 115
m 7774 007 ga+,a-ss,-tx-txr 224
m 3313 000 u=g 3113
m 3267 022 ag=o,ug=stx,u=-u 2017
m 1636 000 a+g-rwx,ga=o,-trx 0
m 4357 077 o+tt,-=u,+rw+wx 700
m 7666 000 go=t+rwr,a--,g=g 5666
m 2575 022 =+xsr,au+sxw+ 6777
m 0143 022 +r 547
m 7204 000 o=o=,a= 0
m 4463 077 au- 4463
m 1360 077 o-,gg=wt=t 1300
m 5553 000 -rw+g,oo+u+,u+txr=s 5011
m 6604 022 -sx 604
m 4343 007 au=g 444
m 2632 022 u+tx+,o== 2730
m 7616 007 u-sw,o--g,a== 0
m 6321 000 a-o 6220
m 7075 000 a+t,ou+tw 7277
m 5277 007 =srw-rtr,+rs 6660
m 1101 000 ou+g=u,=x,og+-tt 111
m 5562 077 +g 5762
m 2406 007 ==xxt,u+xrr-rr,gu- 1110
# X: by the mode before any change, or a directory
m 644 022 a+X 644
m 744 022 a+X 755
m 714 022 go+X 715
m 644 022 u+x,go+X 744
m 744 022 a-x,a+X 755
m 0 000 =X 0
m 0 000 =u+-x+g+ 0

mkdir d
t "X on a directory" "755" 0 sh -c 'chmod 644 d && "$1" a+X d && "$2" %a d' sh "$U" "$T.fstat"
t "octal on a directory" "755" 0 sh -c 'chmod 2700 d && "$1" 755 d && "$2" %a d' sh "$U" "$T.fstat"
t "several files" "600 600" 0 sh -c ': > a; : > b; "$1" 600 a b && echo $("$2" %a a) $("$2" %a b)' sh "$U" "$T.fstat"
t "a missing file" "" 1 "$U" 640 nofile a
t "a missing file, the others changed" "640" 0 fstat %a a
t "a mode starting with -" "400" 0 sh -c 'chmod 600 a && "$1" -w a && "$2" %a a' sh "$U" "$T.fstat"
t "-- before the mode" "600" 0 sh -c '"$1" -- u+w a && "$2" %a a' sh "$U" "$T.fstat"
t "a symbolic link operand: its target" "700" 0 sh -c 'ln -s a la && "$1" 700 la && "$2" %a a' sh "$U" "$T.fstat"
for bad in 8 9 17777 u+q u ug g+ux "" a=r, ,u+x u=gw u=rg =uw "a b"; do
    t "invalid mode '$bad'" "" 2 "$U" "$bad" a
done
t "invalid mode: unchanged" "700" 0 fstat %a a
t "no file" "" 2 "$U" 644
t "nothing" "" 2 "$U"

# -R
mkdir -p r/s/t; : > r/f; : > r/s/g; : > r/s/t/h; mkdir out; : > out/o; ln -s ../out r/lnk
chmod 600 out/o
t "-R" "640 640 640" 0 sh -c '"$1" -R 640 r/f r/s/g r/s/t/h && echo $("$2" %a r/f) $("$2" %a r/s/g) $("$2" %a r/s/t/h)' sh "$U" "$T.fstat"
chmod -R 755 r 2>/dev/null; chmod 644 r/f r/s/g r/s/t/h
t "-R a tree" "750 640 750 640 750 640" 0 sh -c '"$1" -R u=rwX,g=rX,o= r && for f in r r/f r/s r/s/g r/s/t r/s/t/h; do printf "%s " $("$2" %a $f); done | sed "s/ \$//"' sh "$U" "$T.fstat"
ok "-R symbolic links in the tree are not followed" test "$(fstat %a out/o)" = 600
t "-R a symbolic link operand is followed" "700" 0 sh -c 'ln -s r lr && "$1" -R u+rwx,go= lr && "$2" %a r/s/t' sh "$U" "$T.fstat"
t "-RR" "755" 0 sh -c '"$1" -RR 755 r && "$2" %a r/s' sh "$U" "$T.fstat"
t "-R on a file" "600" 0 sh -c '"$1" -R 600 a && "$2" %a a' sh "$U" "$T.fstat"
name=$(awk 'BEGIN { while (i++ < 100) printf "n" }')
mkdir deep
(cd -P deep && i=0 && while [ $i -lt 60 ]; do mkdir "$name" && cd -P "$name" || exit 1; i=$((i+1)); done; : > leaf; chmod 600 leaf) || echo "could not make the tree"
t "-R deeper than PATH_MAX, 16 open files" "" 0 sh -c 'ulimit -n 16 && "$1" -R a+r deep' sh "$U"
t "-R the deepest file" "644" 0 sh -c 'cd -P deep && while [ -d "$1" ]; do cd -P "$1"; done; "$2" %a leaf' sh "$name" "$T.fstat"
if ! $root; then
    mkdir -p c/d; : > c/d/f
    t "-R a-rwx closes the tree" "0 0 0" 0 sh -c '"$1" -R a-rwx c && echo $("$2" %a c) $(chmod u+rx c && "$2" %a c/d) $(chmod u+rx c/d && "$2" %a c/d/f)' sh "$U" "$T.fstat"
    chmod 0 c/d c
    t "-R u+rwx opens it again" "700 700" 0 sh -c '"$1" -R u+rwx c && echo $("$2" %a c/d) $("$2" %a c/d/f)' sh "$U" "$T.fstat"
    t "-R another user's file" "" 1 "$U" 644 /
fi

# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: chmod 600 f -R: -R is a file" "" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" 600 f -R; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests chmod
