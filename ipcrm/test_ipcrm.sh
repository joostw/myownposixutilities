#!/bin/sh
# test_ipcrm.sh - tests for ipcrm (POSIX.1-2024).  Run: sh test_ipcrm.sh
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
U=${U:-$PWD/ipcrm}
T=${TMPDIR:-/tmp}/test_ipcrm.$$
mkdir -m 700 "$T" || exit 2
# fstat: exact file status, which POSIX ls does not show (../testlib/fstat.c)
cc=${CC:-}
[ -n "$cc" ] || for c in c17 c99 cc; do command -v $c >/dev/null 2>&1 && { cc=$c; break; }; done
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T" "$T.fstat"' EXIT
$cc -o "$T.fstat" ../testlib/fstat.c || { echo "cannot compile ../testlib/fstat.c"; exit 2; }
fstat() { "$T.fstat" "$@"; }
# ptyrun FDS COMMAND...: COMMAND with the descriptors FDS on a terminal (../testlib/ptyrun.py)
TL=$(cd ../testlib && pwd) || exit 2; export TL
ptyrun() { python3 "$TL/ptyrun.py" "$@"; }
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# ipc: XSI IPC objects for the tests (../testlib/ipc.c); each one made is
# removed at the end.  The keys are from the process ID, so that runs at the
# same time do not meet.
$cc -o "$T.ipc" "$TL/ipc.c" || { echo "cannot compile ../testlib/ipc.c"; exit 2; }
ipc() { "$T.ipc" "$@"; }
made=
# mk VAR q|m|s KEY MODE [N]: make one, its ID in VAR
mk() { _v=$1; shift; _id=$(ipc "$@") || return 1; made="$made $1:$_id"; eval "$_v=\$_id"; }
cleanup() { for _o in $made; do ipc rm ${_o%%:*} ${_o#*:} 2>/dev/null; done; }
trap 'cleanup; chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T" "$T.ipc" "$T.fstat"' EXIT
base=$(( 0x50000000 + ($$ % 65536) * 16 ))
there() { ipc exists "$1" "$2"; }
gone() { ! ipc exists "$1" "$2"; }
mk q q $base 600
mk m m $((base + 1)) 600
mk s s $((base + 2)) 600
t "-q: the queue is removed" "" 0 "$U" -q $q
ok "-q: gone" gone q $q
t "-m" "" 0 "$U" -m $m
ok "-m: gone" gone m $m
t "-s" "" 0 "$U" -s $s
ok "-s: gone" gone s $s
mk q q $((base + 3)) 600
mk m m $((base + 4)) 600
mk s s $((base + 5)) 600
t "-Q: by key (hexadecimal)" "" 0 "$U" -Q $(printf '0x%x' $((base + 3)))
ok "-Q: gone" gone q $q
t "-M: by key (decimal)" "" 0 "$U" -M $((base + 4))
ok "-M: gone" gone m $m
t "-S: by key (octal)" "" 0 "$U" -S $(printf '0%o' $((base + 5)))
ok "-S: gone" gone s $s
mk q1 q $((base + 6)) 600
mk q2 q $((base + 7)) 600
mk s1 s $((base + 8)) 600
t "several options" "" 0 "$U" -q $q1 -Q $((base + 7)) -s $s1
ok "several options: all gone" sh -c '! "$1" exists q $2 && ! "$1" exists q $3 && ! "$1" exists s $4' sh "$T.ipc" $q1 $q2 $s1
mk q1 q $((base + 9)) 600
t "an identifier that is not there: an error, the others done" "" 1 "$U" -q 2147483000 -q $q1
ok "the others done: gone" gone q $q1
t "a key that is not there" "" 1 "$U" -Q 0x7ffffff1
t "the key IPC_PRIVATE" "" 1 "$U" -Q 0
t "an identifier that is not a number" "" 1 "$U" -q abc
t "a negative identifier" "" 1 "$U" -m -5
t "a key that is not a number" "" 1 "$U" -S 0xzz
t "the wrong kind: -s with a queue's identifier" "" 1 sh -c 'i=$("$2" q 0 600); "$1" -s $i; r=$?; "$2" rm q $i; exit $r' sh "$U" "$T.ipc"
t "the wrong kind: the queue stays" "yes" 0 sh -c 'i=$("$2" q 0 600); "$1" -s $i 2>/dev/null; "$2" exists q $i && echo yes; "$2" rm q $i' sh "$U" "$T.ipc"
# an attached segment: removed when the last process detaches it
mk m m $((base + 10)) 600
t "-M of an attached segment" "" 0 sh -c '"$2" hold $3 2 & sleep 1; "$1" -M $4; r=$?; wait; exit $r' sh "$U" "$T.ipc" $m $((base + 10))
ok "an attached segment: gone after the detach" gone m $m
t "no options: nothing to do" "" 0 "$U"
# usage errors: nothing is removed
mk q q $((base + 11)) 600
t "an operand" "" 2 "$U" -q $q x
ok "a usage error: nothing removed" there q $q
t "an unknown option" "" 2 "$U" -q $q -x
ok "an unknown option: nothing removed" there q $q
t "an option without its argument" "" 2 "$U" -q $q -m
ok "no argument: nothing removed" there q $q
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: ipcrm x -q ID: x is an operand" "" 2 "$U" x -q $q
ok "options after operands: nothing removed" there q $q
if $root && python3 -c "import os,sys; os.setresuid(54321,54321,54321); os.execv(sys.argv[1],sys.argv[1:])" "$U" -x >/dev/null 2>&1; [ $? = 2 ]; then
    t "another user's queue: not removed" "" 1 python3 -c "import os,sys; os.setgroups([]); os.setgid(65534); os.setuid(65534); os.execv(sys.argv[1],sys.argv[1:])" "$U" -q $q
    ok "another user's queue: still there" there q $q
fi

done_tests ipcrm
