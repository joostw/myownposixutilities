#!/bin/sh
# test_timeout.sh - tests for timeout (POSIX.1-2024).  Run: sh test_timeout.sh
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
U=${U:-$PWD/timeout}
T=${TMPDIR:-/tmp}/test_timeout.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# fast: the command took less than 3 seconds (the utilities would take 5)
fast() { _s=$(date +%s); "$@"; _r=$?; [ $(( $(date +%s) - _s )) -lt 4 ] || { echo slow; return 99; }; return $_r; }
st() { "$@"; echo $?; }         # the status as output
t "the time limit: 124" "" 124 fast "$U" 0.3 sleep 5
t "done in time: the utility's status" "" 3 "$U" 5 sh -c 'exit 3'
t "done in time: 0" "hi" 0 "$U" 5 echo hi
t "0: no time limit" "" 3 "$U" 0 sh -c 'sleep 1; exit 3'
t "0.0: no time limit" "" 0 "$U" 0.0 sleep 1
t "an s suffix" "" 124 fast "$U" 0.3s sleep 5
t "an m suffix" "" 124 fast "$U" 0.01m sleep 5
t "an h suffix" "" 124 fast "$U" 0.0001h sleep 5
t "a d suffix" "" 124 fast "$U" 0.000005d sleep 5
t "a fraction first" "" 124 fast "$U" .3 sleep 5
t "the limit is not too short" "" 0 "$U" 3 sleep 1
t "the m suffix is minutes" "" 0 "$U" 0.05m sleep 1
for d in x -1 1x "" 1.5.5 "1 s" 1sm . s +1; do
    t "not a duration: [$d]" "" 125 "$U" "$d" true
done
# the signal
t "SIGTERM by default: with -p, the utility's status" "143" 0 st "$U" -p 0.3 sleep 5
t "-p: a utility that is done in time" "" 3 "$U" -p 5 sh -c 'exit 3'
t "-p: the status of an exit on the signal" "" 7 "$U" -p 0.3 sh -c 'trap "exit 7" TERM; sleep 5 & wait'
t "-s KILL" "" 124 fast "$U" -s KILL 0.3 sleep 5
t "-s KILL -p" "137" 0 st "$U" -p -s KILL 0.3 sleep 5
t "-s INT -p" "130" 0 st "$U" -p -s INT 0.3 sleep 5
t "-s int: case-independent" "130" 0 st "$U" -p -s int 0.3 sleep 5
t "-s SIGINT: the prefix too" "130" 0 st "$U" -p -s SIGINT 0.3 sleep 5
t "-s 9: a number" "137" 0 st "$U" -p -s 9 0.3 sleep 5
t "-s HUP" "129" 0 st "$U" -p -s HUP 0.3 sleep 5
t "-s USR1" "138" 0 st "$U" -p -s USR1 0.3 sleep 5
t "-s not a signal" "" 125 "$U" -s NOSUCH 1 true
t "-s 0" "" 125 "$U" -s 0 1 true
t "-s without a name" "" 125 "$U" -s
# the utility's dispositions: the inherited ones, but the signal's
t "the signal is not ignored by the utility" "143" 0 sh -c 'trap "" TERM; "$1" -p 0.3 sleep 5; echo $?' sh "$U"
t "other ignored signals stay ignored" "alive" 0 sh -c 'trap "" USR1; "$1" 5 sh -c "kill -s USR1 \$\$; echo alive"' sh "$U"
t "SIGTTOU: as inherited, not as timeout has it" "True" 0 "$U" 5 python3 -c 'import signal; print(signal.getsignal(signal.SIGTTOU) == signal.SIG_DFL)'
t "SIGINT: as inherited" "True" 0 sh -c 'trap "" INT; "$1" 5 python3 -c "import signal; print(signal.getsignal(signal.SIGINT) == signal.SIG_IGN)"' sh "$U"
# the process group
t "the utility is in timeout's process group, timeout leads it" "True" 0 "$U" 5 python3 -c 'import os; print(os.getpgrp() == os.getppid())'
t "-f: no new process group" "True" 0 sh -c 'p=$(python3 -c "import os; print(os.getpgrp())"); "$1" -f 5 python3 -c "import os, sys; print(os.getpgrp() == int(sys.argv[1]))" "$p"' sh "$U"
t "the descendants get the signal" "gone" 0 sh -c '"$1" 0.5 sh -c "sleep 5 & echo \$! > pid; wait"; sleep 1; case $(ps -o stat= -p $(cat pid)) in ""|Z*) echo gone;; *) kill $(cat pid);; esac' sh "$U"
t "-s KILL: the descendants get it too" "gone" 0 sh -c '"$1" -s KILL 0.5 sh -c "sleep 5 & echo \$! > pid; wait"; sleep 1; case $(ps -o stat= -p $(cat pid)) in ""|Z*) echo gone;; *) kill $(cat pid);; esac' sh "$U"
t "-f: only the utility gets the signal" "there" 0 sh -c '"$1" -f 0.5 sh -c "sleep 5 & echo \$! > pid; wait"; sleep 1; case $(ps -o stat= -p $(cat pid)) in ""|Z*) ;; *) kill $(cat pid); echo there;; esac' sh "$U"
t "-f: the time limit" "" 124 fast "$U" -f 0.3 sleep 5
# a stopped utility gets SIGCONT
t "a stopped utility: SIGCONT follows" "" 124 fast "$U" 0.3 sh -c 'kill -s STOP $$; sleep 5'
t "-f: a stopped utility: SIGCONT follows" "" 124 fast "$U" -f 0.3 sh -c 'kill -s STOP $$; sleep 5'
# -k
t "-k: the signal is ignored: SIGKILL, 124" "yes" 0 sh -c 'fast() { s=$(date +%s); "$@"; r=$?; [ $(( $(date +%s) - s )) -lt 4 ] || r=99; return $r; }; fast "$1" -k 0.3 0.3 sh -c "trap \"\" TERM; sleep 5"; [ $? = 124 ] && echo yes' sh "$U"
t "-k -f: the signal is ignored: SIGKILL, 124" "" 124 fast "$U" -f -k 0.3 0.3 sh -c 'trap "" TERM; sleep 5'
t "-k -p -f: killed" "137" 0 st fast "$U" -p -f -k 0.3 0.3 sh -c 'trap "" TERM; sleep 5'
t "-k: the utility ends on the signal: 124" "" 124 fast "$U" -k 5 0.3 sleep 5
t "-k 0: no SIGKILL" "" 124 "$U" -k 0 0.3 sh -c 'trap "exit 2" TERM; sleep 1; exit 2'
t "-k not a time" "" 125 "$U" -k x 1 true
t "-k without a time" "" 125 "$U" -k
# signals timeout receives are passed on
t "SIGTERM to timeout: passed on, the utility's status" "got${nl}3" 0 sh -c '"$1" 5 sh -c "trap \"echo got; exit 3\" TERM; sleep 5 & wait" & p=$!; sleep 1; kill -s TERM $p; wait $p; echo $?' sh "$U"
t "SIGHUP to timeout -f: passed on" "129" 0 sh -c '"$1" -f 5 sleep 5 & p=$!; sleep 1; kill -s HUP $p; wait $p; echo $?' sh "$U"
t "an ignored signal is not passed on" "0" 0 sh -c 'trap "" HUP; "$1" 5 sh -c "sleep 2" & p=$!; sleep 1; kill -s HUP $p; wait $p; echo $?' sh "$U"
t "SIGUSR1 to timeout: passed on" "138" 0 sh -c '"$1" 5 sleep 5 & p=$!; sleep 1; kill -s USR1 $p; wait $p; echo $?' sh "$U"
t "a signal passed on is the first: -k counts from it" "yes" 0 sh -c 'fast() { s=$(date +%s); "$@"; r=$?; [ $(( $(date +%s) - s )) -lt 4 ] || r=99; return $r; }; "$1" -f -k 0.5 30 sh -c "trap \"\" TERM; sleep 5" & p=$!; sleep 1; kill -s TERM $p; fast wait $p; [ $? = 137 ] && echo yes' sh "$U"
# the status of the utility
t "killed by a signal, not timed out: the same signal" "138" 0 st "$U" 5 sh -c 'kill -s USR1 $$'
t "the arguments" "a b${nl}c" 0 "$U" 5 printf '%s\n' "a b" c
t "--" "hi" 0 "$U" -- 5 echo hi
t "a script without #!: run by sh" "plain" 0 sh -c 'printf "echo plain\n" > plain && chmod +x plain && "$1" 5 ./plain' sh "$U"
t "not found: 127" "" 127 "$U" 5 no_such_utility_x9
t "not executable: 126" "" 126 sh -c ': > nx && chmod 644 nx && "$1" 5 ./nx' sh "$U"
$root || t "not found past an unsearchable directory: 127" "" 127 sh -c 'mkdir -p nd && chmod 0 nd && PATH=$PWD/nd:$PATH "$1" 5 no_such_utility_here' sh "$U"
t "no utility" "" 125 "$U" 5
t "no operands" "" 125 "$U"
t "an unknown option" "" 125 "$U" -x 5 true
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: timeout 5 printf %s -p: -p is printf's" "-p" 0 "$U" 5 printf %s -p

done_tests timeout
