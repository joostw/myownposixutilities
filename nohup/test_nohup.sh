#!/bin/sh
# test_nohup.sh - tests for nohup (POSIX.1-2024).  Run: sh test_nohup.sh
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
U=${U:-$PWD/nohup}
T=${TMPDIR:-/tmp}/test_nohup.$$
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
# no terminal: nothing is redirected
t "no terminal: the output stays" "hi" 0 "$U" echo hi
t "no terminal: no nohup.out" "" 1 test -e nohup.out
t "no terminal: the standard error stays" "err" 0 sh -c '"$1" sh -c "echo err >&2" 2>&1 >/dev/null' sh "$U"
t "SIGHUP is ignored" "alive" 0 "$U" sh -c 'kill -s HUP $$; echo alive'
t "SIGHUP is ignored by the utility's children" "alive" 0 "$U" sh -c 'sh -c "kill -s HUP \$\$; echo alive"'
t "the utility's status" "" 7 "$U" sh -c 'exit 7'
t "the arguments" "a b${nl}c" 0 "$U" printf '%s\n' "a b" c
t "--" "hi" 0 "$U" -- echo hi
t "a script without #!: run by sh" "plain" 0 sh -c 'printf "echo plain\n" > plain && chmod +x plain && "$1" ./plain' sh "$U"
t "not found: 127" "" 127 "$U" no_such_utility_x9
t "not executable: 126" "" 126 sh -c ': > nx && chmod 644 nx && "$1" ./nx' sh "$U"
t "a directory: 126" "" 126 sh -c 'mkdir -p dir && "$1" ./dir' sh "$U"
$root || t "not found past an unsearchable directory: 127" "" 127 sh -c 'mkdir -p nd && chmod 0 nd && PATH=$PWD/nd:$PATH "$1" no_such_utility_here' sh "$U"
t "no utility: 127" "" 127 "$U"
t "only --: 127" "" 127 "$U" --
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: nohup printf %s -n: -n is printf's" "-n" 0 "$U" printf '%s' -n

# the standard output a terminal: nohup.out
mkdir o1 && cd o1 || exit 2
t "stdout a terminal: nothing on it" "" 0 sh -c 'python3 "$TL/ptyrun.py" 1 "$1" echo hi 2>err' sh "$U"
t "stdout a terminal: the output in nohup.out" "hi" 0 cat nohup.out
t "stdout a terminal: the message names nohup.out" "" 0 grep -q 'nohup\.out' err
t "nohup.out is created rw for the owner only" "600" 0 fstat %a nohup.out
t "nohup.out is appended to" "hi${nl}again" 0 sh -c 'python3 "$TL/ptyrun.py" 1 "$1" echo again 2>/dev/null; cat nohup.out' sh "$U"
t "an existing nohup.out keeps its mode" "644" 0 sh -c 'chmod 644 nohup.out; python3 "$TL/ptyrun.py" 1 "$1" true 2>/dev/null; "$2" %a nohup.out' sh "$U" "$T.fstat"
t "created 600 whatever the mask" "600" 0 sh -c 'rm nohup.out; umask 0; python3 "$TL/ptyrun.py" 1 "$1" true 2>/dev/null; "$2" %a nohup.out' sh "$U" "$T.fstat"
t "stdout a terminal: the standard error stays (not a terminal)" "err" 0 sh -c 'python3 "$TL/ptyrun.py" 1 "$1" sh -c "echo err >&2" 2>&1 | grep -v nohup.out' sh "$U"
cd .. && mkdir o2 && cd o2 || exit 2
t "both terminals: only the message on the terminal" "nohup: appending output to nohup.out" 0 ptyrun 12 "$U" sh -c 'echo out; echo err >&2'
t "both terminals: both in nohup.out" "out${nl}err" 0 cat nohup.out
t "both terminals: the status" "nohup: appending output to nohup.out" 3 ptyrun 12 "$U" sh -c 'exit 3'
cd .. && mkdir o3 && cd o3 || exit 2
t "stderr a terminal, stdout a file: stderr to that file" "out${nl}err" 0 sh -c 'python3 "$TL/ptyrun.py" 2 "$1" sh -c "echo out; echo err >&2" > f; cat f' sh "$U"
t "stderr a terminal, stdout a file: nothing on the terminal" "" 0 sh -c 'python3 "$TL/ptyrun.py" 2 "$1" sh -c "echo err >&2" > f' sh "$U"
t "stderr a terminal, stdout a file: no nohup.out" "" 1 test -e nohup.out
t "stderr a terminal, stdout a pipe: stderr to the pipe" "err" 0 sh -c 'python3 "$TL/ptyrun.py" 2 "$1" sh -c "echo err >&2" | cat' sh "$U"
t "stderr a terminal, stdout closed: stderr to nohup.out" "err" 0 sh -c 'python3 "$TL/ptyrun.py" 2 "$1" sh -c "echo err >&2" >&-; cat nohup.out' sh "$U"
t "stderr a terminal, stdout closed: stdout stays closed" "closed" 0 sh -c 'rm -f nohup.out; python3 "$TL/ptyrun.py" 2 "$1" sh -c "echo x 2>/dev/null || echo closed >&2" >&-; cat nohup.out' sh "$U"
cd .. && mkdir o4 && cd o4 || exit 2
t "stdin a terminal: not the utility's" "no" 0 sh -c 'python3 "$TL/ptyrun.py" 01 "$1" sh -c "test -t 0 && echo yes || echo no" 2>/dev/null; cat nohup.out' sh "$U"
# $HOME/nohup.out when the working directory will not do
cd .. && mkdir -p o5/home o5/wd/nohup.out && cd o5/wd || exit 2
t "\$HOME/nohup.out: the output" "hi" 0 sh -c 'HOME=$PWD/../home python3 "$TL/ptyrun.py" 1 "$1" echo hi 2>err; cat ../home/nohup.out' sh "$U"
t "\$HOME/nohup.out: the message names it" "" 0 grep -q 'home/nohup\.out' err
t "neither will do: 127" "" 127 sh -c 'mkdir -p ../h2/nohup.out; HOME=$PWD/../h2 python3 "$TL/ptyrun.py" 1 "$1" echo hi' sh "$U"
t "neither will do: the utility is not run" "" 1 sh -c 'HOME=$PWD/../h2 python3 "$TL/ptyrun.py" 1 "$1" sh -c ": > ran" 2>/dev/null; test -e ran' sh "$U"
t "no HOME: 127" "" 127 sh -c 'unset HOME; python3 "$TL/ptyrun.py" 1 "$1" echo hi' sh "$U"
cd "$T" || exit 2

done_tests nohup
