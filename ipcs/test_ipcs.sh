#!/bin/sh
# test_ipcs.sh - tests for ipcs (POSIX.1-2024).  Run: sh test_ipcs.sh
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
U=${U:-$PWD/ipcs}
T=${TMPDIR:-/tmp}/test_ipcs.$$
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
mk q q $base 640
mk m m $((base + 1)) 600 8192
mk s s $((base + 2)) 666 3
mk p q 0 600
me=$(id -un) mg=$(id -gn)
key() { printf '0x%08x' "$1"; }
# the introductory line, and the reports
t "the introductory line" "yes" 0 sh -c '"$1" | sed 1q | grep -E "^IPC status from /proc/sysvipc as of (Sun|Mon|Tue|Wed|Thu|Fri|Sat) (Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 123][0-9] [0-2][0-9]:[0-5][0-9]:[0-6][0-9] .* [0-9]{4}\$" >/dev/null && echo yes' sh "$U"
t "the introductory line: the date as date writes it" "same" 0 sh -c 'for i in 1 2 3; do a=$("$1" | sed 1q | sed "s/^IPC status from \/proc\/sysvipc as of //"); b=$(LC_ALL=C date); [ "$a" = "$b" ] && { echo same; exit; }; done' sh "$U"
t "no options: the three reports, in order" "Message Queues:${nl}Shared Memory:${nl}Semaphores:" 0 sh -c '"$1" | grep ":\$" | grep -v "^IPC"' sh "$U"
t "no options: one heading" "1" 0 sh -c '"$1" | grep -c "^T "' sh "$U"
t "the heading" "T ID KEY MODE OWNER GROUP" 0 sh -c '"$1" | grep "^T " | tr -s " "' sh "$U"
t "-q: only the message queues" "Message Queues:" 0 sh -c '"$1" -q | grep ":\$" | grep -v "^IPC"' sh "$U"
t "-m: only the shared memory" "Shared Memory:" 0 sh -c '"$1" -m | grep ":\$" | grep -v "^IPC"' sh "$U"
t "-s: only the semaphores" "Semaphores:" 0 sh -c '"$1" -s | grep ":\$" | grep -v "^IPC"' sh "$U"
t "-qs" "Message Queues:${nl}Semaphores:" 0 sh -c '"$1" -qs | grep ":\$" | grep -v "^IPC"' sh "$U"
t "an entry in its report" "Message Queues:" 0 sh -c '"$1" | awk -v i=$2 "/:\$/ { r = \$0 } \$1 == \"q\" && \$2 == i { print r }"' sh "$U" $q
# the columns of every report
t "a queue" "q $q $(key $base) --rw-r----- $me $mg" 0 sh -c '"$1" | awk -v i="$2" "\$1 == \"q\" && \$2 == i" | tr -s " "' sh "$U" $q
t "a segment" "m $m $(key $((base + 1))) --rw------- $me $mg" 0 sh -c '"$1" | awk -v i="$2" "\$1 == \"m\" && \$2 == i" | tr -s " "' sh "$U" $m
t "a semaphore set: a for alter" "s $s $(key $((base + 2))) --ra-ra-ra- $me $mg" 0 sh -c '"$1" | awk -v i="$2" "\$1 == \"s\" && \$2 == i" | tr -s " "' sh "$U" $s
t "IPC_PRIVATE: the key 0" "0x00000000" 0 sh -c '"$1" -q | awk -v i="$2" "\$1 == \"q\" && \$2 == i { print \$3 }"' sh "$U" $p
t "MODE: 11 characters and a space" "--rw-r----- " 0 sh -c '"$1" -q | awk -v i="$2" "\$1 == \"q\" && \$2 == i" | cut -c 25-36' sh "$U" $q
# the options
ipc send $q hello
ipc attach $m
ipc semop $s
t "-o: CBYTES and QNUM" "5 1" 0 sh -c '"$1" -qo | awk -v i="$2" "\$1 == \"q\" && \$2 == i { print \$7, \$8 }"' sh "$U" $q
t "-o: the heading" "T ID KEY MODE OWNER GROUP CBYTES QNUM" 0 sh -c '"$1" -qo | grep "^T " | tr -s " "' sh "$U"
t "-o: NATTCH, 0" "0" 0 sh -c '"$1" -mo | awk -v i="$2" "\$1 == \"m\" && \$2 == i { print \$7 }"' sh "$U" $m
t "-o: NATTCH of an attached segment" "1" 0 sh -c '"$2" hold $3 3 & sleep 1; "$1" -mo | awk -v i="$3" "\$1 == \"m\" && \$2 == i { print \$7 }"; wait' sh "$U" "$T.ipc" $m
t "-b: QBYTES" "$(cat /proc/sys/kernel/msgmnb 2>/dev/null)" 0 sh -c '"$1" -qb | awk -v i="$2" "\$1 == \"q\" && \$2 == i { print \$7 }"' sh "$U" $q
t "-b: SEGSZ" "8192" 0 sh -c '"$1" -mb | awk -v i="$2" "\$1 == \"m\" && \$2 == i { print \$7 }"' sh "$U" $m
t "-b: NSEMS" "3" 0 sh -c '"$1" -sb | awk -v i="$2" "\$1 == \"s\" && \$2 == i { print \$7 }"' sh "$U" $s
t "-c: CREATOR and CGROUP" "$me $mg" 0 sh -c '"$1" -qc | awk -v i="$2" "\$1 == \"q\" && \$2 == i { print \$7, \$8 }"' sh "$U" $q
t "-p: LSPID set, LRPID 0" "yes 0" 0 sh -c '"$1" -qp | awk -v i="$2" "\$1 == \"q\" && \$2 == i { print (\$7 > 0 ? \"yes\" : \$7), \$8 }"' sh "$U" $q
t "-p: CPID and LPID" "yes yes" 0 sh -c '"$1" -mp | awk -v i="$2" "\$1 == \"m\" && \$2 == i { print (\$7 > 0 ? \"yes\" : \$7), (\$8 > 0 ? \"yes\" : \$8) }"' sh "$U" $m
hms='[0-9]{1,2}:[0-5][0-9]:[0-5][0-9]'
t "-t: STIME, RTIME no-entry, CTIME" "yes" 0 sh -c '"$1" -qt | awk -v i="$2" "\$1 == \"q\" && \$2 == i" | grep -E " $3 +no-entry +$3\$" >/dev/null && echo yes' sh "$U" $q "$hms"
t "-t: ATIME, DTIME and CTIME of a segment" "yes" 0 sh -c '"$1" -mt | awk -v i="$2" "\$1 == \"m\" && \$2 == i" | grep -E " $3 +$3 +$3\$" >/dev/null && echo yes' sh "$U" $m "$hms"
t "-t: OTIME of a semaphore set" "yes" 0 sh -c '"$1" -st | awk -v i="$2" "\$1 == \"s\" && \$2 == i" | grep -E " $3 +$3\$" >/dev/null && echo yes' sh "$U" $s "$hms"
t "-t: never attached: no-entry" "no-entry no-entry" 0 sh -c 'i=$("$2" m 0 600 64); "$1" -mt | awk -v i="$i" "\$1 == \"m\" && \$2 == i { print \$7, \$8 }"; "$2" rm m $i' sh "$U" "$T.ipc"
t "-a: as -bcopt" "same" 0 sh -c '"$1" -a | sed 1d > a; "$1" -bcopt | sed 1d > b; cmp -s a b && echo same' sh "$U"
t "-a: the queue's columns" "T ID KEY MODE OWNER GROUP CREATOR CGROUP CBYTES QNUM QBYTES LSPID LRPID STIME RTIME CTIME" 0 sh -c '"$1" -qa | grep "^T " | tr -s " "' sh "$U"
t "-a: the segment's columns" "T ID KEY MODE OWNER GROUP CREATOR CGROUP NATTCH SEGSZ CPID LPID ATIME DTIME CTIME" 0 sh -c '"$1" -ma | grep "^T " | tr -s " "' sh "$U"
t "-a: the semaphore set's columns" "T ID KEY MODE OWNER GROUP CREATOR CGROUP NSEMS OTIME CTIME" 0 sh -c '"$1" -sa | grep "^T " | tr -s " "' sh "$U"
t "-a: a heading for each report" "3" 0 sh -c '"$1" -a | grep -c "^T "' sh "$U"
t "-a: every line of a report has the heading's fields" "yes" 0 sh -c '"$1" -qa | awk "/^T / { n = NF } \$1 == \"q\" && NF != n { bad = 1 } END { print bad ? \"no\" : \"yes\" }"' sh "$U"
t "a removed queue is gone" "" 0 sh -c 'i=$("$2" q 0 600); "$2" rm q $i; "$1" -q | awk -v i="$i" "\$1 == \"q\" && \$2 == i"' sh "$U" "$T.ipc"
# errors
t "an operand" "" 2 "$U" x
t "an unknown option" "" 2 "$U" -x
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: ipcs x -q: x is an operand" "" 2 "$U" x -q
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" > /dev/full' sh "$U"

done_tests ipcs
