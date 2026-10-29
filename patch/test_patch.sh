#!/bin/sh
# test_patch.sh - tests for patch (POSIX.1-2024).  Run: sh test_patch.sh
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
U=${U:-$PWD/patch}
T=${TMPDIR:-/tmp}/test_patch.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
# ptyrun FDS COMMAND...: COMMAND with the descriptors FDS on a terminal (../testlib/ptyrun.py)
TL=$(cd ../testlib && pwd) || exit 2; export TL
ptyrun() { python3 "$TL/ptyrun.py" "$@"; }
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# lines FIRST LAST: the lines "line N", one a line
lines() { awk -v a="$1" -v b="$2" 'BEGIN { for (i = a; i <= b; i++) print "line", i }'; }
lines 1 40 > old
sed 's/^line 5$/line five/; /^line 20$/d; s/^line 35$/line 35\nline 35b/' old > new
# python3 -c "$NOTTY" COMMAND...: COMMAND without a controlling terminal
NOTTY='import os, sys
try:
    os.setsid()
except OSError:
    pass
os.execvp(sys.argv[1], sys.argv[1:])'
export NOTTY
# fresh: the old file as f (again)
fresh() { cp old f; rm -f f.orig f.rej; }

# the four formats, found from the input
for fmt in "" -c -u -e -C1 -U0; do
    diff $fmt old new > p$fmt
    t "diff ${fmt:-(normal)}: applied" "same" 0 sh -c 'cp old f; "$1" -i "$2" f 2>/dev/null && cmp -s f new && echo same' sh "$U" "p$fmt"
done
t "the standard input by default" "same" 0 sh -c 'cp old f; "$1" f < p-u 2>/dev/null && cmp -s f new && echo same' sh "$U"
t "-c: forced" "same" 0 sh -c 'cp old f; "$1" -c -i p-c f 2>/dev/null && cmp -s f new && echo same' sh "$U"
t "-u: forced" "same" 0 sh -c 'cp old f; "$1" -u -i p-u f 2>/dev/null && cmp -s f new && echo same' sh "$U"
t "-n: forced" "same" 0 sh -c 'cp old f; "$1" -n -i p f 2>/dev/null && cmp -s f new && echo same' sh "$U"
t "-e: forced" "same" 0 sh -c 'cp old f; "$1" -e -i p-e f 2>/dev/null && cmp -s f new && echo same' sh "$U"
t "-u on a normal diff: no hunks" "same" 0 sh -c 'cp old f; "$1" -u -i p f 2>/dev/null; cmp -s f old && echo same' sh "$U"
t "two of -c -e -n -u" "" 2 "$U" -c -u -i p-u f
t "the mode is kept" "-rwxr-x---" 0 sh -c 'cp old f; chmod 750 f; "$1" -i p-u f 2>/dev/null; ls -l f | cut -c 1-10' sh "$U"
t "a line without a newline at the end" "yes" 0 sh -c 'printf "a\nb" > n1; printf "a\nc" > n2; diff -u n1 n2 > pn; cp n1 n3; "$1" -i pn n3 2>/dev/null && cmp -s n3 n2 && echo yes' sh "$U"
t "adding the newline at the end" "yes" 0 sh -c 'printf "a\nb" > n1; printf "a\nb\nc\n" > n2; diff -c n1 n2 > pn; cp n1 n3; "$1" -i pn n3 2>/dev/null && cmp -s n3 n2 && echo yes' sh "$U"
t "an ed script with a line ." "yes" 0 sh -c 'printf "a\nb\n" > e1; printf "a\n.\nb\n" > e2; diff -e e1 e2 > pe; cp e1 e3; "$1" -i pe e3 2>/dev/null && cmp -s e3 e2 && echo yes' sh "$U"

# finding the place
t "an offset: lines added at the top" "same" 0 sh -c '{ lines 0 0 | sed "s/line/top/"; cat old; } > f; { lines 0 0 | sed "s/line/top/"; cat new; } > want; "$1" -i p-u f 2>/dev/null && cmp -s f want && echo same' sh "$U"
t "the offset is reported" "yes" 0 sh -c '{ echo top; echo top; cat old; } > f; "$1" -i p-u f 2>&1 | grep -q "offset 2 lines" && echo yes' sh "$U"
t "the fuzz: a changed context line" "same" 0 sh -c 'sed "s/^line 2\$/line two/" old > f; sed "s/^line 2\$/line two/" new > want; "$1" -i p-u f 2>/dev/null && cmp -s f want && echo same' sh "$U"
t "the fuzz is reported" "yes" 0 sh -c 'sed "s/^line 2\$/line two/" old > f; "$1" -i p-u f 2>&1 | grep -q "with fuzz 1" && echo yes' sh "$U"
t "no fuzz for a normal diff: rejected" "" 1 sh -c 'sed "s/^line 5\$/line V/" old > f; "$1" -i p f 2>/dev/null' sh "$U"
t "-l: blanks match blanks" "same" 0 sh -c 'sed "s/^line 4\$/line   4/" old > f; sed "s/^line 4\$/line   4/" new > want; "$1" -l -i p-u f 2>/dev/null && cmp -s f want && echo same' sh "$U"
t "without -l: fuzz then" "yes" 0 sh -c 'sed "s/^line 2\$/line	2/" old > f; "$1" -i p-u f 2>&1 | grep -q "fuzz" && echo yes' sh "$U"

# rejects
lines 1 40 | sed 's/^line 5$/something else/' > other
t "a hunk that does not fit: status 1" "" 1 sh -c 'cp other f; "$1" -i p-u f 2>/dev/null' sh "$U"
t "the other hunks are applied" "yes" 0 sh -c 'cp other f; "$1" -i p-u f 2>/dev/null; grep -q "line 35b" f && ! grep -q "^line 20\$" f && echo yes' sh "$U"
t "the reject file: f.rej" "yes" 0 sh -c 'cp other f; rm -f f.rej; "$1" -i p-u f 2>/dev/null; grep -q "^-line 5\$" f.rej && grep -q "^+line five\$" f.rej && echo yes' sh "$U"
t "a unified reject: a unified hunk" "yes" 0 sh -c 'cp other f; "$1" -i p-u f 2>/dev/null; grep -q "^@@ -" f.rej && echo yes' sh "$U"
t "a normal reject: copied context" "yes" 0 sh -c 'cp other f; "$1" -i p f 2>/dev/null; grep -q "^\*\*\*\*\*\*\*\*\*\*\*\*\*\*\*\$" f.rej && grep -q "^! line 5\$" f.rej && grep -q "^! line five\$" f.rej && echo yes' sh "$U"
t "a context reject: copied context" "yes" 0 sh -c 'cp other f; "$1" -i p-c f 2>/dev/null; grep -q "^\*\*\* [0-9]" f.rej && grep -q "^--- [0-9]" f.rej && echo yes' sh "$U"
t "-r: the reject file named" "yes" 0 sh -c 'cp other f; rm -f f.rej; "$1" -r rj -i p-u f 2>/dev/null; [ -s rj ] && [ ! -e f.rej ] && echo yes' sh "$U"
t "the reject message" "yes" 0 sh -c 'cp other f; "$1" -i p-u f 2>&1 | grep -q "1 out of 3 hunks FAILED -- saving rejects to file f.rej" && echo yes' sh "$U"

# -R and already applied patches
t "-R: the patch reversed" "same" 0 sh -c 'cp new f; "$1" -R -i p-u f 2>/dev/null && cmp -s f old && echo same' sh "$U"
t "-R with a normal diff" "same" 0 sh -c 'cp new f; "$1" -R -i p f 2>/dev/null && cmp -s f old && echo same' sh "$U"
t "-R with an ed script: an error" "" 2 sh -c 'cp new f; "$1" -R -e -i p-e f 2>/dev/null' sh "$U"
t "-R: rejects in the reversed form" "yes" 0 sh -c 'cp other f; "$1" -R -i p-c f 2>/dev/null; awk "/^\*\*\* [0-9]/ { o = 1 } /^--- [0-9]/ { o = 0 } o && /line five/ { f = 1 } END { exit !f }" f.rej && echo yes' sh "$U"
t "-N: an applied patch is skipped" "same" 0 sh -c 'cp new f; "$1" -N -i p-u f 2>/dev/null; cmp -s f new && echo same' sh "$U"
t "an applied patch, no terminal: rejected" "" 1 sh -c 'cp new f; python3 -c "$NOTTY" "$1" -i p-u f < /dev/null 2>/dev/null' sh "$U"
t "an applied patch, the answer y: reversed" "same" 0 sh -c 'cp new f; PTYRUN_INPUT="y$3" python3 "$TL/ptyrun.py" 0 "$1" -i "$2/p-u" f > msg 2>&1; grep -q "Assume -R" msg && cmp -s f old && echo same' sh "$U" "$PWD" "$nl"
t "an applied patch, the answer n: rejected" "same" 1 sh -c 'cp new f; PTYRUN_INPUT="n$3" python3 "$TL/ptyrun.py" 0 "$1" -i "$2/p-u" f > /dev/null 2>&1; s=$?; cmp -s f new && echo same; exit $s' sh "$U" "$PWD" "$nl"

# names
mkdir -p d/sub
cp old d/sub/g
cp new d/sub/g.new
( cd d && diff -u sub/g sub/g.new > ../pg )
sed 's|sub/g|sub/nothere|; s|sub/g.new|sub/nothere|' pg > pnone
t "the name from ---: the basename without -p" "same" 0 sh -c 'cp old g; "$1" -i pg 2>/dev/null && cmp -s g new && echo same' sh "$U"
t "-p0: the whole name" "same" 0 sh -c 'cp old d/sub/g; cd d && "$1" -p0 -i ../pg 2>/dev/null && cmp -s sub/g ../new && echo same' sh "$U"
t "-p1: one component off" "same" 0 sh -c 'mkdir -p e; cp old e/g; cd e && "$1" -p1 -i ../pg 2>/dev/null && cmp -s g ../new && echo same' sh "$U"
t "-d: the directory first" "same" 0 sh -c 'cp old d/sub/g; "$1" -d d -p0 -i ../pg 2>/dev/null && cmp -s d/sub/g new && echo same' sh "$U"
t "the +++ name when the --- one does not exist" "same" 0 sh -c 'printf "%s\n" "--- nothere" "+++ h2" > ph; sed 1,2d p-u >> ph; cp old h2; "$1" -i ph 2>/dev/null && cmp -s h2 new && echo same' sh "$U"
t "the *** name of a context diff" "same" 0 sh -c 'printf "%s\n" "*** h3	date" "--- nothere	date" > ph; sed 1,2d p-c >> ph; cp old h3; "$1" -i ph 2>/dev/null && cmp -s h3 new && echo same' sh "$U"
t "Index:" "same" 0 sh -c '{ echo "Index: h4"; diff old new; } > ph; cp old h4; "$1" -i ph 2>/dev/null && cmp -s h4 new && echo same' sh "$U"
t "the operand wins" "same" 0 sh -c 'cp old h5; "$1" -i pg h5 2>/dev/null && cmp -s h5 new && echo same' sh "$U"
t "an absolute name: -p1 takes the slash" "same" 0 sh -c 'printf "%s\n" "--- /h6" "+++ /h6" > ph; sed 1,2d p-u >> ph; cp old h6; "$1" -p1 -i ph 2>/dev/null && cmp -s h6 new && echo same' sh "$U"
t "no file found, no terminal: skipped" "" 2 sh -c 'python3 -c "$NOTTY" "$1" -i pnone < /dev/null 2>/dev/null' sh "$U"
t "no file found: the prompt on a terminal" "same" 0 sh -c 'cp old h7; PTYRUN_INPUT="h7$2" python3 "$TL/ptyrun.py" 01 "$1" -i "$3/pnone" > msg 2>/dev/null; grep -q "File to patch:" msg && cmp -s h7 new && echo same' sh "$U" "$nl" "$PWD"
t "a new file from /dev/null" "same" 0 sh -c 'diff -u /dev/null new | sed "s|^+++ new|+++ made|" > pm; rm -f made; "$1" -i pm 2>/dev/null && cmp -s made new && echo same' sh "$U"
t "a file removed: to /dev/null" "gone" 0 sh -c 'cp old gone1; diff -u gone1 /dev/null > pr; "$1" -i pr 2>/dev/null; [ -e gone1 ] || echo gone' sh "$U"

# several patches in one file
cp old m1; cp old m2
diff -u old new | sed 's/^--- old/--- m1/; s/^+++ new/+++ m1/' > pp
diff -c old new | sed 's/^\*\*\* old/*** m2/; s/^--- new/--- m2/' >> pp
t "several patches, several files, two formats" "same" 0 sh -c '"$1" -i pp 2>/dev/null && cmp -s m1 new && cmp -s m2 new && echo same' sh "$U"
t "an indented patch" "same" 0 sh -c 'sed "s/^/    /" p-u > pi; cp old f; "$1" -i pi f 2>/dev/null && cmp -s f new && echo same' sh "$U"
t "junk before and between patches" "same" 0 sh -c '{ echo "From: someone"; echo; diff -u old new; echo "-- "; echo signature; } > pj; cp old f; "$1" -i pj f 2>/dev/null && cmp -s f new && echo same' sh "$U"

# -b, -o, -D
t "-b: f.orig" "yes" 0 sh -c 'cp old f; rm -f f.orig; "$1" -b -i p-u f 2>/dev/null && cmp -s f.orig old && cmp -s f new && echo yes' sh "$U"
t "-b: the first version only" "yes" 0 sh -c 'cp old f; rm -f f.orig; cat p-u p-u > p2; diff -u new old > back; cat p-u back > p3; "$1" -b -i p3 f 2>/dev/null; cmp -s f.orig old && cmp -s f old && echo yes' sh "$U"
t "-o: the result elsewhere" "yes" 0 sh -c 'cp old f; rm -f out; "$1" -o out -i p-u f 2>/dev/null && cmp -s out new && cmp -s f old && echo yes' sh "$U"
t "-o: several versions one after another" "yes" 0 sh -c 'cp old f; diff -u new old > back; cat p-u back > p3; "$1" -o out -i p3 f 2>/dev/null; cat new old > want; cmp -s out want && cmp -s f old && echo yes' sh "$U"
t "-o -b: outfile.orig, no f.orig" "yes" 0 sh -c 'cp old f; rm -f f.orig; echo before > out; "$1" -b -o out -i p-u f 2>/dev/null; [ "$(cat out.orig)" = before ] && [ ! -e f.orig ] && cmp -s out new && echo yes' sh "$U"
t "-D: #ifdef for new lines, #ifndef for old" "yes" 0 sh -c 'cp old f; "$1" -D NEW -i p-u f 2>/dev/null; for d in "" -DNEW; do awk -v d=$d "
    /^#ifdef NEW/ { s = 1; ok = d != \"\"; next } /^#ifndef NEW/ { s = 1; ok = d == \"\"; next }
    /^#else/ { ok = !ok; next } /^#endif/ { s = 0; next } !s || ok" f > out$d; done
    cmp -s out new && cmp -s out-DNEW new 2>/dev/null; cmp -s out old && echo yes' sh "$U"
t "-D: the marks" "yes" 0 sh -c 'cp old f; "$1" -D NEW -i p-u f 2>/dev/null; grep -q "^#ifndef NEW\$" f && grep -q "^#else\$" f && grep -q "^#ifdef NEW\$" f && grep -q "^#endif\$" f && echo yes' sh "$U"

# errors and options
t "a missing patch file" "" 2 "$U" -i nonexistent f
t "a missing directory for -d" "" 2 "$U" -d nonexistent -i p f
t "a bad -p" "" 2 "$U" -p x -i p f
t "more than one operand" "" 2 "$U" -i p f g
t "a broken unified hunk" "" 2 sh -c 'printf "%s\n" "--- f" "+++ f" "@@ -1,3 +1,3 @@" " line 1" > pb; cp old f; "$1" -i pb f 2>/dev/null' sh "$U"
t "an unknown option" "" 2 "$U" -x
# getopt() must not take an option after an operand for one (no permuting)
t "an option after an operand is an operand" "" 2 sh -c 'cp old f; "$1" f -R < p-u 2>/dev/null' sh "$U"

done_tests patch
