#!/bin/sh
# test_rm.sh - tests for rm (POSIX.1-2024).  Run: sh test_rm.sh
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
U=${U:-$PWD/rm}
T=${TMPDIR:-/tmp}/test_rm.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
root=false; [ "$(id -u)" = 0 ] && root=true

# files
echo x > f
t "file" "" 0 "$U" f
ok "file gone" test ! -e f
t "missing" "" 1 "$U" nofile
t "missing: a message" "1" 0 sh -c '"$1" nofile 2>&1 >/dev/null | grep -c nofile' sh "$U"
t "-f missing" "" 0 "$U" -f nofile
t "-f missing: silent" "" 0 sh -c '"$1" -f nofile 2>&1' sh "$U"
t "-f no operands" "" 0 "$U" -f
t "no operands" "" 2 "$U"
t "bad option" "" 2 "$U" -x f
echo a > a; echo b > b
t "several, one missing" "" 1 "$U" a nofile b
ok "both gone" test ! -e a -a ! -e b
t "--" "" 0 sh -c 'echo x > -f; "$1" -- -f; test ! -e ./-f' sh "$U"
ln -s nowhere dl
t "dangling symlink" "" 0 "$U" dl
ok "dangling symlink gone" test ! -h dl
echo x > ro; chmod 444 ro
t "write-protected, input not a terminal" "" 0 "$U" ro </dev/null
ok "write-protected gone" test ! -e ro

# directories
mkdir d
t "directory without -r" "" 1 "$U" d
ok "directory stays" test -d d
t "directory: a message" "1" 0 sh -c '"$1" d 2>&1 | grep -c d' sh "$U"
t "-d empty" "" 0 "$U" -d d
ok "-d empty gone" test ! -e d
mkdir d; echo x > d/x
t "-d not empty" "" 1 "$U" -d d
ok "-d not empty stays" test -f d/x
t "-d file" "" 0 sh -c 'echo x > g; "$1" -d g; test ! -e g' sh "$U"
mkdir -p d/e/f d/g; echo y > d/e/f/y; ln -s ../out d/e/lnk
mkdir out; echo keep > out/k
t "-r" "" 0 "$U" -r d
ok "-r gone" test ! -e d
ok "-r does not follow symlinks" test -f out/k
mkdir -p d/e; echo z > d/e/z
t "-R" "" 0 "$U" -R d
ok "-R gone" test ! -e d
ln -s out lo
t "-r symlink to directory: the link" "" 0 "$U" -r lo
ok "link gone" test ! -h lo
ok "target kept" test -f out/k
mkdir -p d/e
t "-r trailing slash" "" 0 "$U" -r d/
ok "trailing slash gone" test ! -e d
t "-rf missing" "" 0 "$U" -rf nothing here
mkdir d
t "dot" "" 1 "$U" -r .
t "dot-dot" "" 1 "$U" -r d/..
t "d/." "" 1 "$U" -rf d/.
t "d/./" "" 1 "$U" -rf d/./
t "dot: a message" "1" 0 sh -c '"$1" -rf . 2>&1 | grep -c "\."' sh "$U"
ok "nothing removed" test -d d
t "the root directory" "" 1 "$U" -d /
t "the root directory, //" "" 1 "$U" -d //
t "the root directory: a message" "1" 0 sh -c '"$1" -d / 2>&1 | grep -c "/"' sh "$U"
ok "root still there" test -d /
"$U" -d d

# -i and -f
echo x > f
t "-i, no" "" 0 sh -c 'echo n | "$1" -i f' sh "$U"
ok "-i no: kept" test -f f
t "-i prompt names the file" "1" 0 sh -c 'echo n | "$1" -i f 2>&1 | grep -c "f"' sh "$U"
t "-i, yes" "" 0 sh -c 'echo y | "$1" -i f' sh "$U"
ok "-i yes: gone" test ! -e f
echo x > f
t "-i, end of input" "" 0 sh -c '"$1" -i f </dev/null' sh "$U"
ok "-i end of input: kept" test -f f
t "-fi: -i wins" "" 0 sh -c 'echo n | "$1" -f -i f' sh "$U"
ok "-fi kept" test -f f
t "-if: -f wins" "" 0 sh -c 'echo n | "$1" -i -f f' sh "$U"
ok "-if gone" test ! -e f
mkdir d; echo x > d/x
t "-ri: no to descend" "" 0 sh -c 'echo n | "$1" -ri d' sh "$U"
ok "-ri no: kept" test -f d/x
t "-ri: yes, no" "" 0 sh -c 'printf "y\nn\n" | "$1" -ri d' sh "$U"
ok "-ri yes no: file kept" test -f d/x
t "-ri: yes, yes, no" "" 0 sh -c 'printf "y\ny\nn\n" | "$1" -ri d' sh "$U"
ok "-ri: the directory kept" test -d d -a ! -e d/x
t "-ri: empty directory, one prompt" "1" 0 sh -c 'echo y | "$1" -ri d 2>&1 | grep -c "?"' sh "$U"
ok "-ri empty: gone" test ! -e d
mkdir d; echo x > d/x
t "-ri: three prompts" "3" 0 sh -c 'printf "y\ny\ny\n" | "$1" -ri d 2>&1 | grep -o "?" | wc -l | tr -d " "' sh "$U"
ok "-ri all yes: gone" test ! -e d
mkdir d
t "-di" "" 0 sh -c 'echo y | "$1" -di d' sh "$U"
ok "-di gone" test ! -e d

# -v
echo x > f; mkdir -p d/e
t "-v" "removed f" 0 "$U" -v f
t "-v -r" "removed directory d/e
removed directory d" 0 "$U" -rv d
[ -c /dev/full ] && t "-v write error" "" 1 sh -c 'echo x > f; "$1" -v f >/dev/full' sh "$U"

# depth: past PATH_MAX, and under a low limit on open files
name=$(awk 'BEGIN { while (i++ < 100) printf "n" }')
mkdir deep
(cd -P deep && i=0 && while [ $i -lt 100 ]; do mkdir "$name" && cd -P "$name" || exit 1; i=$((i+1)); done; echo x > leaf) || echo "could not make the tree"
t "a tree deeper than PATH_MAX" "" 0 "$U" -r deep
ok "deep tree gone" test ! -e deep
mkdir lim
(cd lim && i=0 && while [ $i -lt 200 ]; do mkdir a && cd a || exit 1; i=$((i+1)); done)
t "200 levels with 16 open files" "" 0 sh -c 'ulimit -n 16 && "$1" -r lim' sh "$U"
ok "200 levels gone" test ! -e lim
mkdir -p wide/a/b
i=0; while [ $i -lt 300 ]; do : > wide/a/f$i; mkdir wide/a/b/d$i; i=$((i+1)); done
t "a wide tree with 16 open files" "" 0 sh -c 'ulimit -n 16 && "$1" -r wide' sh "$U"
ok "wide tree gone" test ! -e wide

# permissions (they do not apply to root)
if ! $root; then
    mkdir pd; echo x > pd/x; chmod 555 pd
    t "entry of a read-only directory" "" 1 "$U" pd/x
    t "-r read-only directory" "" 1 "$U" -r pd
    ok "read-only directory: kept" test -f pd/x
    chmod 755 pd; "$U" -r pd
    mkdir ud; chmod 0 ud
    t "an empty directory that cannot be read" "" 0 "$U" -r ud
    ok "unreadable empty gone" test ! -e ud
    mkdir ud; : > ud/x; chmod 0 ud
    t "a full directory that cannot be read" "" 1 "$U" -r ud
    chmod 755 ud; "$U" -r ud
fi

# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: rm f -r: -r is a file" "" 1 sh -c 'd=${TMPDIR:-/tmp}/opts.$$ && mkdir "$d" && cd "$d" && echo x > f && { "$1" f -r; r=$?; ls; exit $r; }; r=$?; cd / && rm -rf "$d"; exit $r' sh "$U"

done_tests rm
