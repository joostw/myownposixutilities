#!/bin/sh
# test_env.sh - tests for env (POSIX.1-2024).  Run: sh test_env.sh
cd "$(dirname "$0")" || exit 2
pass=0 fail=0
# t NAME EXPECTED-STDOUT EXPECTED-STATUS COMMAND...  (stdout compared without trailing newlines)
t() {
    _n=$1 _e=$2 _s=$3; shift 3
    _o=$("$@" 2>/dev/null); _r=$?
    if [ "$_o" = "$_e" ] && [ "$_r" = "$_s" ]; then pass=$((pass+1))
    else fail=$((fail+1)); printf 'FAIL %s\n  got:  [%s] status %s\n  want: [%s] status %s\n' "$_n" "$_o" "$_r" "$_e" "$_s"; fi
}
done_tests() { echo "$1: $pass passed, $fail failed"; [ "$fail" = 0 ]; }
U=${U:-$PWD/env}
T=${TMPDIR:-/tmp}/test_env.$$
mkdir -m 700 "$T" || exit 2
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 2
root=false; [ "$(id -u)" = 0 ] && root=true
SH=$(command -v sh)

# writing the environment
t "-i: empty" "" 0 "$U" -i
t "-i with assignments" "A=1
B=2" 0 "$U" -i A=1 B=2
t "a later assignment replaces" "A=2" 0 "$U" -i A=1 A=2
t "an inherited variable" "1" 0 sh -c 'FOO_T=1 "$1" | grep -c "^FOO_T=1\$"' sh "$U"
t "an inherited variable replaced" "FOO_T=2" 0 sh -c 'FOO_T=1 "$1" FOO_T=2 | grep "^FOO_T="' sh "$U"
t "an empty value" "E=" 0 "$U" -i E=
t "a value with =" "E=a=b" 0 "$U" -i E=a=b
t "a value with a newline" "E=a
b" 0 "$U" -i "E=a
b"
t "- acts as -i" "X=1" 0 "$U" - X=1
t "-ii" "" 0 "$U" -ii
t "-- ends the options" "X=1" 0 "$U" -i -- X=1
t "a bad option" "" 125 "$U" -x
[ -c /dev/full ] && t "a write error" "" 125 sh -c '"$1" >/dev/full' sh "$U"

# running a utility
t "a utility with arguments" "a b" 0 "$U" "$SH" -c 'echo "$1 $2"' sh a b
t "the new environment" "v" 0 "$U" V_T=v "$SH" -c 'echo "$V_T"'
t "-i: only the assignments" "1 []" 0 "$U" -i A=1 "$SH" -c 'echo "$A [$HOME]"'
t "the utility's options are its own" "-i" 0 "$U" "$SH" -c 'echo "$1"' sh -i
t "the exit status of the utility" "" 3 "$U" "$SH" -c 'exit 3'
t "not found" "" 127 "$U" no_such_utility_here
t "not found, a pathname" "" 127 "$U" ./no_such_file
# a directory in PATH that cannot be searched does not make it 126
$root || t "not found past an unsearchable directory: 127" "" 127 sh -c 'mkdir -p nd && chmod 0 nd && PATH=$PWD/nd:$PATH "$1" no_such_utility_here' sh "$U"
mkdir bin; printf '#!%s\necho script "$@"\n' "$SH" > bin/s1; chmod 755 bin/s1
printf 'echo plain "$@"\n' > bin/s2; chmod 755 bin/s2
: > bin/noexec; chmod 644 bin/noexec
t "PATH from an assignment" "script x" 0 "$U" PATH="$T/bin" s1 x
t "PATH: a later entry" "script" 0 "$U" PATH="/nonexistent:$T/bin" s1
t "PATH: an empty entry is the current directory" "script" 0 sh -c 'cd "$1/bin" && "$2" PATH=":/nonexistent" s1' sh "$T" "$U"
t "a script without #!: run by sh" "plain y" 0 "$U" PATH="$T/bin" s2 y
t "-i: PATH unset, the standard one" "ok" 0 "$U" -i sh -c 'echo ok'
t "found, cannot be run" "" 126 "$U" PATH="$T/bin" noexec
t "a directory named as the utility" "" 126 "$U" "$T/bin"
t "the utility in a pathname" "script" 0 "$U" bin/s1
t "the first non-assignment is the utility" "B=2" 0 "$U" -i A=1 "$SH" -c 'echo B=2' C=3

done_tests env
