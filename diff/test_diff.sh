#!/bin/sh
# test_diff.sh - tests for diff (POSIX.1-2024).  Run: sh test_diff.sh
# The expected output agrees with GNU diff 3.10 for the same input (apart
# from the headers of -c and -u, whose times GNU writes differently),
# except where the README says otherwise: -C 0, the text for binary files.
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
U=${U:-$PWD/diff}
T=${TMPDIR:-/tmp}/test_diff.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
LC_ALL=C TZ=UTC0
export LC_ALL TZ

printf 'a\nb\nc\nd\ne\nf\ng\nh\ni\nj\n' > ten
printf 'a\nB\nc\nd\ne\nf\ng\nh\ni\nj\nk\n' > ten2
printf 'a\nb\nc\n' > abc
printf 'b\nc\n' > bc
printf 'a\nb\nc\nd\n' > abcd
printf 'a\nb\n' > ab
printf 'x\na\nb\nc\n' > xabc
printf 'a\nb\nc' > abc_nonl
printf 'a  b\nc\t\td \nend\n' > ws1
printf 'a b\nc d\nend\n' > ws2
printf 'ab\ncd\nend\n' > ws3
printf 'a\n.\nb\n' > dot1
printf 'x\n.\n.\ny\n' > dot2
: > empty
printf 'a\0b' > bin1
printf 'a\0c' > bin2
mkdir -p x/sub y/sub x/only1 dir
echo 1 > x/f; echo 2 > y/f; echo s > x/same; echo s > y/same
echo a > x/sub/g; echo b > y/sub/g; echo o > y/onlyy
mkfifo x/p y/p; mkdir y/fd; echo z > x/fd
printf 'a\nb\nc\n' > dir/abc
for f in ten ten2 abc bc abcd ab xabc abc_nonl ws1 ws2 dot1 dot2 empty x/f y/f x/sub/g y/sub/g; do TZ=UTC0 touch -t 202001020304.05 $f; done
TZ=UTC0 touch -t 202106070809.10 ten2 bc
t "identical" "" 0 sh -c '"$1" abc abc' sh "$U"
t "the same file" "" 0 sh -c '"$1" abc ./abc' sh "$U"
t "a change" "2a3
> c" 1 sh -c '"$1" ab abc' sh "$U"
t "a deletion at the start" "1d0
< a" 1 sh -c '"$1" abc bc' sh "$U"
t "an addition at the start" "0a1
> x" 1 sh -c '"$1" abc xabc' sh "$U"
t "an addition at the end" "3a4
> d" 1 sh -c '"$1" abc abcd' sh "$U"
t "a deletion at the end" "4d3
< d" 1 sh -c '"$1" abcd abc' sh "$U"
t "from empty" "0a1,3
> a
> b
> c" 1 sh -c '"$1" empty abc' sh "$U"
t "to empty" "1,3d0
< a
< b
< c" 1 sh -c '"$1" abc empty' sh "$U"
t "changes and additions" "2c2
< b
---
> B
10a11
> k" 1 sh -c '"$1" ten ten2' sh "$U"
t "a missing newline" "3c3
< c
---
> c
\\ No newline at end of file" 1 sh -c '"$1" abc abc_nonl' sh "$U"
t "a missing newline, other way" "3c3
< c
\\ No newline at end of file
---
> c" 1 sh -c '"$1" abc_nonl abc' sh "$U"
t "-e" "10a
k
.
2c
B
." 1 sh -c '"$1" -e ten ten2' sh "$U"
t "-e from empty" "0a
a
b
c
." 1 sh -c '"$1" -e empty abc' sh "$U"
t "-e deletions" "4d
1d" 1 sh -c '"$1" -e abcd bc' sh "$U"
t "-e a line that is a dot" "3c
..
.
s/.//
a
y
.
1c
x
." 1 sh -c '"$1" -e dot1 dot2' sh "$U"
t "-f" "c2
B
.
a10
k
." 1 sh -c '"$1" -f ten ten2' sh "$U"
t "-f deletions" "d1
d4" 1 sh -c '"$1" -f abcd bc' sh "$U"
t "-c" "*** ten Thu Jan  2 03:04:05 2020
--- ten2 Mon Jun  7 08:09:10 2021
***************
*** 1,5 ****
  a
! b
  c
  d
  e
--- 1,5 ----
  a
! B
  c
  d
  e
***************
*** 8,10 ****
--- 8,11 ----
  h
  i
  j
+ k" 1 sh -c '"$1" -c ten ten2' sh "$U"
t "-c an addition only" "*** abc Thu Jan  2 03:04:05 2020
--- abcd Thu Jan  2 03:04:05 2020
***************
*** 1,3 ****
--- 1,4 ----
  a
  b
  c
+ d" 1 sh -c '"$1" -c abc abcd' sh "$U"
t "-c a deletion only" "*** abcd Thu Jan  2 03:04:05 2020
--- abc Thu Jan  2 03:04:05 2020
***************
*** 1,4 ****
  a
  b
  c
- d
--- 1,3 ----" 1 sh -c '"$1" -c abcd abc' sh "$U"
t "-C 1" "*** ten Thu Jan  2 03:04:05 2020
--- ten2 Mon Jun  7 08:09:10 2021
***************
*** 1,3 ****
  a
! b
  c
--- 1,3 ----
  a
! B
  c
***************
*** 10 ****
--- 10,11 ----
  j
+ k" 1 sh -c '"$1" -C 1 ten ten2' sh "$U"
t "-C 0" "" 2 sh -c '"$1" -C 0 ten ten2' sh "$U"
t "-u" "--- ten	2020-01-02 03:04:05.000000000 +0000
+++ ten2	2021-06-07 08:09:10.000000000 +0000
@@ -1,5 +1,5 @@
 a
-b
+B
 c
 d
 e
@@ -8,3 +8,4 @@
 h
 i
 j
+k" 1 sh -c '"$1" -u ten ten2' sh "$U"
t "-U 1" "--- ten	2020-01-02 03:04:05.000000000 +0000
+++ ten2	2021-06-07 08:09:10.000000000 +0000
@@ -1,3 +1,3 @@
 a
-b
+B
 c
@@ -10 +10,2 @@
 j
+k" 1 sh -c '"$1" -U 1 ten ten2' sh "$U"
t "-U 0" "--- ten	2020-01-02 03:04:05.000000000 +0000
+++ ten2	2021-06-07 08:09:10.000000000 +0000
@@ -2 +2 @@
-b
+B
@@ -10,0 +11 @@
+k" 1 sh -c '"$1" -U 0 ten ten2' sh "$U"
t "-U 0 an addition" "--- abc	2020-01-02 03:04:05.000000000 +0000
+++ abcd	2020-01-02 03:04:05.000000000 +0000
@@ -3,0 +4 @@
+d" 1 sh -c '"$1" -U 0 abc abcd' sh "$U"
t "-u from empty" "--- empty	2020-01-02 03:04:05.000000000 +0000
+++ abc	2020-01-02 03:04:05.000000000 +0000
@@ -0,0 +1,3 @@
+a
+b
+c" 1 sh -c '"$1" -u empty abc' sh "$U"
t "-u to empty" "--- abc	2020-01-02 03:04:05.000000000 +0000
+++ empty	2020-01-02 03:04:05.000000000 +0000
@@ -1,3 +0,0 @@
-a
-b
-c" 1 sh -c '"$1" -u abc empty' sh "$U"
t "-u a missing newline" "--- abc	2020-01-02 03:04:05.000000000 +0000
+++ abc_nonl	2020-01-02 03:04:05.000000000 +0000
@@ -1,3 +1,3 @@
 a
 b
-c
+c
\\ No newline at end of file" 1 sh -c '"$1" -u abc abc_nonl' sh "$U"
t "-u a deletion" "--- abc	2020-01-02 03:04:05.000000000 +0000
+++ bc	2021-06-07 08:09:10.000000000 +0000
@@ -1,3 +1,2 @@
-a
 b
 c" 1 sh -c '"$1" -u abc bc' sh "$U"
t "-b" "" 0 sh -c '"$1" -b ws1 ws2' sh "$U"
t "without -b" "1,2c1,2
< a  b
< c		d 
---
> a b
> c d" 1 sh -c '"$1" ws1 ws2' sh "$U"
t "-b: white space is not ignored" "1,2c1,2
< a  b
< c		d 
---
> ab
> cd" 1 sh -c '"$1" -b ws1 ws3' sh "$U"
t "-b and a missing newline" "" 0 sh -c '"$1" -b abc abc_nonl' sh "$U"
t "binary files" "Files bin1 and bin2 differ" 1 sh -c '"$1" bin1 bin2' sh "$U"
t "binary, identical" "" 0 sh -c '"$1" bin1 bin1' sh "$U"
t "standard input" "3d2
< c" 1 sh -c '"$1" - ab <abc' sh "$U"
t "standard input second" "2a3
> c" 1 sh -c '"$1" ab - <abc' sh "$U"
t "a file and a directory" "" 0 sh -c '"$1" abc dir' sh "$U"
t "a directory and a file" "" 0 sh -c '"$1" dir abc' sh "$U"
t "directories" "diff x/f y/f
1c1
< 1
---
> 2
File x/fd is a regular file while file y/fd is a directory
Only in x: only1
Only in y: onlyy
File x/p is a fifo while file y/p is a fifo
Common subdirectories: x/sub and y/sub" 1 sh -c '"$1" x y' sh "$U"
t "-r" "diff -r x/f y/f
1c1
< 1
---
> 2
File x/fd is a regular file while file y/fd is a directory
Only in x: only1
Only in y: onlyy
File x/p is a fifo while file y/p is a fifo
diff -r x/sub/g y/sub/g
1c1
< a
---
> b" 1 sh -c '"$1" -r x y' sh "$U"
t "-r -u: the options in the header" "diff -r -U 1 x/f y/f
--- x/f	2020-01-02 03:04:05.000000000 +0000
+++ y/f	2020-01-02 03:04:05.000000000 +0000
@@ -1 +1 @@
-1
+2
File x/fd is a regular file while file y/fd is a directory
Only in x: only1
Only in y: onlyy
File x/p is a fifo while file y/p is a fifo
diff -r -U 1 x/sub/g y/sub/g
--- x/sub/g	2020-01-02 03:04:05.000000000 +0000
+++ y/sub/g	2020-01-02 03:04:05.000000000 +0000
@@ -1 +1 @@
-a
+b" 1 sh -c '"$1" -r -U 1 x y' sh "$U"
t "-r -e" "diff -r -e x/f y/f
1c
2
.
File x/fd is a regular file while file y/fd is a directory
Only in x: only1
Only in y: onlyy
File x/p is a fifo while file y/p is a fifo
diff -r -e x/sub/g y/sub/g
1c
b
." 1 sh -c '"$1" -r -e x y' sh "$U"
t "a missing file" "" 2 sh -c '"$1" abc nofile' sh "$U"
t "a bad option" "" 2 sh -c '"$1" -q abc abc' sh "$U"
t "-C without a number" "" 2 sh -c '"$1" -C x abc abc' sh "$U"
t "-C 0 is not positive" "" 2 sh -c '"$1" -C 0 abc ab' sh "$U"
t "one operand" "" 2 sh -c '"$1" abc' sh "$U"

# -e scripts, given to ed with a w command, make file2 of file1
# ed_ok NAME FILE1 FILE2
ed_ok() {
    cp "$2" ed.tmp
    { "$U" -e "$2" "$3"; echo w; echo q; } | ed -s ed.tmp >/dev/null 2>&1
    ok "$1" cmp -s ed.tmp "$3"
}
ed_ok "ed: changes and additions" ten ten2
ed_ok "ed: deletions" abcd bc
ed_ok "ed: from empty" empty abc
ed_ok "ed: to empty" abc empty
ed_ok "ed: lines that are a dot" dot1 dot2
printf '.\n.\n' > dots
ed_ok "ed: only dots" empty dots
awk 'BEGIN { srand(1); for (i = 0; i < 3000; i++) print int(rand() * 20) }' > r1
awk 'BEGIN { srand(2); for (i = 0; i < 3000; i++) print int(rand() * 20) }' > r2
ed_ok "ed: random files" r1 r2
t "-u of a standard input: named -" "--- -" 0 sh -c '"$1" -u - abc < ab | sed -n "1s/	.*//p"' sh "$U"
t "-u header" "--- ten	2020-01-02 03:04:05.000000000 +0000
+++ ten2	2021-06-07 08:09:10.000000000 +0000" 0 sh -c '"$1" -u ten ten2 | sed 2q' sh "$U"
t "-c header" "*** ten Thu Jan  2 03:04:05 2020
--- ten2 Mon Jun  7 08:09:10 2021" 0 sh -c '"$1" -c ten ten2 | sed 2q' sh "$U"
ln -s .. x/sub/loop; ln -s .. y/sub/loop
t "-r a loop: reported, status 2" "" 2 sh -c '"$1" -r x y >/dev/null' sh "$U"
[ -c /dev/full ] && t "a write error" "" 2 sh -c '"$1" ab abc >/dev/full' sh "$U"

# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: diff f f -u: three operands" "" 2 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f f -u; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests diff
