#!/bin/sh
# test_id.sh - tests for id (POSIX.1-2024).  Run: sh test_id.sh
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
U=${U:-$PWD/id}
T=${TMPDIR:-/tmp}/test_id.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# the reference: the system's id, with the options POSIX gives it
words() { tr ' ' '\n' | sort; }
uid=$(id -u) gid=$(id -g)
t "-u" "$uid" 0 "$U" -u
t "-g" "$gid" 0 "$U" -g
t "-ur" "$(id -ur)" 0 "$U" -ur
t "-gr" "$(id -gr)" 0 "$U" -gr
t "-un" "$(id -un)" 0 "$U" -un
t "-gn" "$(id -gn)" 0 "$U" -gn
t "-u -n -r" "$(id -unr)" 0 "$U" -u -n -r
t "-G: the same groups" "$(id -G | words)" 0 sh -c '"$1" -G | tr " " "\n" | sort' sh "$U"
t "-G: the effective group first" "$gid" 0 sh -c '"$1" -G | cut -d " " -f 1' sh "$U"
t "-Gn: the same names" "$(id -Gn | words)" 0 sh -c '"$1" -Gn | tr " " "\n" | sort' sh "$U"
t "-G: each once" "" 0 sh -c '"$1" -G | tr " " "\n" | sort | uniq -d' sh "$U"
t "the default: uid= and gid=" "uid=$uid($(id -un)) gid=$gid($(id -gn))" 0 sh -c '"$1" | cut -d " " -f 1,2' sh "$U"
t "the default: groups= as -G" "$(id -G | tr ' ' ,)" 0 sh -c '"$1" | sed "s/.*groups=//; s/([^)]*)//g"' sh "$U"
t "the default: no euid= or egid= when the same" "" 1 sh -c '"$1" | grep -E "e(u|g)id="' sh "$U"
# a user operand
t "root: -u" "0" 0 "$U" -u root
t "root: -un" "root" 0 "$U" -un root
t "root: -g" "$(id -g root)" 0 "$U" -g root
t "root: -G" "$(id -G root | words)" 0 sh -c '"$1" -G root | tr " " "\n" | sort' sh "$U"
t "root: the default" "uid=0(root) gid=$(id -g root)($(id -gn root))" 0 sh -c '"$1" root | cut -d " " -f 1,2' sh "$U"
t "a user ID as the operand" "root" 0 "$U" -un 0
t "an unknown user" "" 1 "$U" no_such_user_x9
t "an unknown user ID" "" 1 sh -c 'python3 -c "import pwd,sys
try: pwd.getpwuid(54321); sys.exit(3)
except KeyError: pass" || exit 9; "$1" 54321' sh "$U"
# usage
t "-n alone" "" 2 "$U" -n
t "-r alone" "" 2 "$U" -r
t "-G -r" "" 2 "$U" -G -r
t "-G -u" "" 2 "$U" -G -u
t "-g -u" "" 2 "$U" -g -u
t "two users" "" 2 "$U" root root
t "an unknown option" "" 2 "$U" -x
t "--" "0" 0 "$U" -u -- root
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: id root -u: -u is a second operand" "" 2 "$U" root -u
# other IDs, set with Python (as root, when other users can run $U: its
# directories must be searchable by them)
others=false
$root && python3 -c "import os,sys; os.setresuid(54321,54321,54321); os.execv(sys.argv[1],sys.argv[1:])" "$U" -u >/dev/null 2>&1 && others=true
if $others; then
    nob=$(id -un 65534 2>/dev/null)
    [ -n "$nob" ] && nob="($nob)"
    t "a different effective user ID: euid=" "uid=0(root) gid=0($(id -gn root)) euid=65534$nob" 0 sh -c 'python3 -c "import os,sys; os.setresuid(0,65534,0); os.execv(sys.argv[1],sys.argv[1:])" "$1" | cut -d " " -f 1-3' sh "$U"
    t "a different effective group ID: egid=" "egid=65534" 0 sh -c 'python3 -c "import os,sys; os.setresgid(0,65534,0); os.execv(sys.argv[1],sys.argv[1:])" "$1" | cut -d " " -f 3 | sed "s/(.*//"' sh "$U"
    t "-G with a different effective group: effective, real, ..." "65534 0" 0 sh -c 'python3 -c "import os,sys; os.setgroups([]); os.setresgid(0,65534,0); os.execv(sys.argv[1],sys.argv[1:])" "$1" -G' sh "$U"
    t "-r: the real IDs" "0${nl}0" 0 sh -c 'python3 -c "import os,sys; os.setresgid(0,65534,0); os.setresuid(0,65534,0); os.execv(sys.argv[1],sys.argv[1:])" "$1" -ur; python3 -c "import os,sys; os.setresgid(0,65534,0); os.setresuid(0,65534,0); os.execv(sys.argv[1],sys.argv[1:])" "$1" -gr' sh "$U"
    t "IDs without names: no (name)" "uid=54321 gid=54322 groups=54322,54323" 0 sh -c 'python3 -c "
import os, sys, pwd, grp
for f, i in ((pwd.getpwuid, 54321), (grp.getgrgid, 54322), (grp.getgrgid, 54323)):
    try: f(i); sys.exit(3)
    except KeyError: pass
os.setgroups([54323]); os.setresgid(54322, 54322, 54322); os.setresuid(54321, 54321, 54321)
os.execv(sys.argv[1], sys.argv[1:])" "$1"' sh "$U"
    t "-n without a name: the number" "54321" 0 sh -c 'python3 -c "import os,sys; os.setresuid(54321,54321,54321); os.execv(sys.argv[1],sys.argv[1:])" "$1" -un' sh "$U"
fi
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" > /dev/full' sh "$U"

done_tests id
