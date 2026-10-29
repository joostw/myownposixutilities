#!/bin/sh
# test_time.sh - tests for time (POSIX.1-2024).  Run: sh test_time.sh
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
U=${U:-$PWD/time}
T=${TMPDIR:-/tmp}/test_time.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# the times are on the standard error; their format
t "-p: three lines" "3" 0 sh -c '"$1" -p true 2>&1 | wc -l | tr -d " "' sh "$U"
t "-p: the format" "real${nl}user${nl}sys" 0 sh -c '"$1" -p true 2>&1 | grep -E "^(real|user|sys) [0-9]+\.[0-9]+$" | cut -d " " -f 1' sh "$U"
t "no -p: the same format" "real${nl}user${nl}sys" 0 sh -c '"$1" true 2>&1 | grep -E "^(real|user|sys) [0-9]+\.[0-9]+$" | cut -d " " -f 1' sh "$U"
t "the utility's output is its own" "hi" 0 sh -c '"$1" -p echo hi 2>/dev/null' sh "$U"
t "the utility's standard error comes first" "err${nl}real" 0 sh -c '"$1" -p sh -c "echo err >&2" 2>&1 | sed 2q | cut -d " " -f 1' sh "$U"
t "real: the elapsed time" "yes" 0 sh -c '"$1" -p sleep 1 2>&1 | awk "/^real/ { print (\$2 >= 0.95 && \$2 < 30) ? \"yes\" : \$2 }"' sh "$U"
t "user and sys: the utility's processor time" "yes" 0 sh -c '"$1" -p python3 -c "
import time
t = time.process_time()
while time.process_time() - t < 0.5: pass" 2>&1 | awk "/^(user|sys)/ { n += \$2 } END { print (n >= 0.4) ? \"yes\" : n }"' sh "$U"
t "user and sys: the time of the utility's children too" "yes" 0 sh -c '"$1" -p sh -c "python3 -c \"
import time
t = time.process_time()
while time.process_time() - t < 0.5: pass\"" 2>&1 | awk "/^(user|sys)/ { n += \$2 } END { print (n >= 0.4) ? \"yes\" : n }"' sh "$U"
t "sys: a number" "yes" 0 sh -c '"$1" -p true 2>&1 | awk "/^sys/ { print (\$2 >= 0) ? \"yes\" : \$2 }"' sh "$U"
# the exit status
t "the utility's status" "" 7 "$U" sh -c 'exit 7'
t "the utility's status, -p" "" 0 "$U" -p true
t "killed by a signal: the same signal" "143" 0 sh -c '"$1" sh -c "kill -s TERM \$\$" 2>/dev/null; echo $?' sh "$U"
t "killed by a signal: the times are written" "real" 0 sh -c '"$1" -p sh -c "kill -s TERM \$\$" 2>&1 | grep -o "^real"' sh "$U"
t "SIGINT is ignored by time while it waits" "ok${nl}0" 0 sh -c '"$1" sh -c "kill -s INT \$PPID; sleep 1; echo ok" 2>/dev/null; echo $?' sh "$U"
t "SIGQUIT is ignored by time while it waits" "ok${nl}0" 0 sh -c '"$1" sh -c "kill -s QUIT \$PPID; sleep 1; echo ok" 2>/dev/null; echo $?' sh "$U"
t "the utility has SIGINT as time found it" "130" 0 sh -c '"$1" python3 -c "import os, signal; os.kill(os.getpid(), signal.SIGINT); signal.pause()" 2>/dev/null; echo $?' sh "$U"
t "the arguments" "a b${nl}c" 0 sh -c '"$1" printf "%s\n" "a b" c 2>/dev/null' sh "$U"
t "a script without #!: run by sh" "plain" 0 sh -c 'printf "echo plain\n" > plain && chmod +x plain && "$1" ./plain 2>/dev/null' sh "$U"
t "not found: 127" "" 127 "$U" no_such_utility_x9
t "not found: the times are written" "real" 0 sh -c '"$1" -p no_such_utility_x9 2>&1 | grep -o "^real"' sh "$U"
t "not executable: 126" "" 126 sh -c ': > nx && chmod 644 nx && "$1" ./nx' sh "$U"
$root || t "not found past an unsearchable directory: 127" "" 127 sh -c 'mkdir -p nd && chmod 0 nd && PATH=$PWD/nd:$PATH "$1" no_such_utility_here' sh "$U"
t "--" "hi" 0 sh -c '"$1" -- echo hi 2>/dev/null' sh "$U"
t "-p --" "hi" 0 sh -c '"$1" -p -- echo hi 2>/dev/null' sh "$U"
t "no utility" "" 125 "$U"
t "-p without a utility" "" 125 "$U" -p
t "an unknown option" "" 125 "$U" -x true
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: time printf %s -p: -p is printf's" "-p" 0 sh -c '"$1" printf %s -p 2>/dev/null' sh "$U"

done_tests time
