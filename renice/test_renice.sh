#!/bin/sh
# test_renice.sh - tests for renice (POSIX.1-2024).  Run: sh test_renice.sh
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
U=${U:-$PWD/renice}
T=${TMPDIR:-/tmp}/test_renice.$$
mkdir -m 700 "$T" || exit 2
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# processes to renice: sleeps, each in a new process group (Python), so
# that renicing their group touches nothing else; their nice value by ps
spawn() { python3 -c 'import os, sys; os.setpgid(0, 0); os.execvp("sleep", ["sleep", "60"])' >/dev/null 2>&1 & echo $! >> pids; echo $!; }
nv() { ps -o nice= -p "$1" | tr -d ' '; }
trap 'kill $(cat "$T/pids" 2>/dev/null) 2>/dev/null; chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T"' EXIT
a=$(spawn) b=$(spawn) c=$(spawn)
sleep 1
base=$(nv $a)
lim() { _v=$(($1)); [ $_v -gt 19 ] && _v=19; [ $_v -lt -20 ] && _v=-20; echo $_v; }
t "a process ID (the default)" "$(lim "$base + 2")" 0 sh -c '"$1" -n 2 $2 && ps -o nice= -p $2 | tr -d " "' sh "$U" $a
t "-p" "$(lim "$base + 3")" 0 sh -c '"$1" -n 1 -p $2 && ps -o nice= -p $2 | tr -d " "' sh "$U" $a
t "-n first or last" "$(lim "$base + 4")" 0 sh -c '"$1" -p $2 -n 1 && ps -o nice= -p $2 | tr -d " "' sh "$U" $a
t "-n2: the increment in the same argument" "$(lim "$base + 2")" 0 sh -c '"$1" -n2 $2 && ps -o nice= -p $2 | tr -d " "' sh "$U" $b
t "-n +1" "$(lim "$base + 3")" 0 sh -c '"$1" -n +1 $2 && ps -o nice= -p $2 | tr -d " "' sh "$U" $b
t "several IDs" "$(lim "$base + 6") $(lim "$base + 5")" 0 sh -c '"$1" -n 2 $2 $3 && echo $(ps -o nice= -p $2) $(ps -o nice= -p $3)' sh "$U" $a $b
t "the highest value" "19" 0 sh -c '"$1" -n 100 $2 && ps -o nice= -p $2 | tr -d " "' sh "$U" $a
t "-g: a process group" "$(lim "$base + 7")" 0 sh -c '"$1" -n 7 -g $(ps -o pgid= -p $2) && ps -o nice= -p $2 | tr -d " "' sh "$U" $c
t "-g, then -p: each applies to the IDs after it" "$(lim "$base + 8") 19" 0 sh -c '"$1" -n 1 -g $(ps -o pgid= -p $2) -p $3 && echo $(ps -o nice= -p $2) $(ps -o nice= -p $3)' sh "$U" $c $a
t "-g: the group's other processes" "yes" 0 sh -c 'python3 -c "import os, time; os.setpgid(0, 0); os.fork(); time.sleep(5)" >/dev/null 2>&1 & sleep 1; g=$!; "$1" -n 3 -g $g; n=$(ps -o pgid=,nice= -A | awk -v g=$g "\$1 == g { print \$2 }" | sort -u | wc -l | tr -d " "); kill -- -$g; [ "$n" = 1 ] && echo yes' sh "$U"
if $root; then
    t "-n -5 (as root)" "$(lim "$base + 3")" 0 sh -c '"$1" -n -5 $2 && ps -o nice= -p $2 | tr -d " "' sh "$U" $c
    t "-u: the processes of a user (as root)" "$(lim "$base + 4")" 0 sh -c 'python3 -c "import os; os.setpgid(0, 0); os.setgid(65534); os.setuid(65534); os.execvp(\"sleep\", [\"sleep\", \"60\"])" >/dev/null 2>&1 & p=$!; echo $p >> pids; sleep 1; u=$(ps -o user= -p $p); "$1" -n 4 -u $u; r=$?; ps -o nice= -p $p | tr -d " "; exit $r' sh "$U"
    t "-u with a user ID (as root)" "$(lim "$base + 1")" 0 sh -c 'python3 -c "import os; os.setpgid(0, 0); os.setgid(65534); os.setuid(65534); os.execvp(\"sleep\", [\"sleep\", \"60\"])" >/dev/null 2>&1 & p=$!; echo $p >> pids; sleep 1; "$1" -n 1 -u 65534 >/dev/null 2>&1; ps -o nice= -p $p | tr -d " "; kill $p' sh "$U"
else
    t "-n -5 without privileges: an error" "" 1 "$U" -n -5 $c
    t "-n -5 without privileges: unchanged" "$(nv $c)" 0 sh -c '"$1" -n -5 $2 2>/dev/null; ps -o nice= -p $2 | tr -d " "' sh "$U" $c
fi
# errors: reported, the others still done
t "a process that does not exist" "" 1 "$U" -n 1 999999999
t "a process that does not exist, and one that does" "19" 1 sh -c '"$1" -n 1 999999999 $2; r=$?; ps -o nice= -p $2 | tr -d " "; exit $r' sh "$U" $a
t "not a process ID" "" 1 "$U" -n 1 abc
t "a negative process ID" "" 1 "$U" -n 1 -p -- -5
t "an unknown user" "" 1 "$U" -n 1 -u no_such_user_x9
t "no -n" "" 2 "$U" $a
t "-n without an increment" "" 2 "$U" $a -n
t "-n x" "" 2 "$U" -n x $a
t "-n 1x" "" 2 "$U" -n 1x $a
t "no IDs" "" 2 "$U" -n 1
t "an unknown option" "" 2 "$U" -x -n 1 $a
t "--: an ID that looks like an option" "" 1 "$U" -n 1 -- -1
# renice is excepted from Guideline 9: options may follow the IDs
t "options after operands: renice -n 1 PID -g PGID: -g applies after" "19 $(lim "$(nv $c) + 1")" 0 sh -c '"$1" -n 1 $2 -g $(ps -o pgid= -p $3) && echo $(ps -o nice= -p $2) $(ps -o nice= -p $3)' sh "$U" $a $c

done_tests renice
