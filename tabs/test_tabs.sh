#!/bin/sh
# test_tabs.sh - tests for tabs (POSIX.1-2024).  Run: sh test_tabs.sh
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
U=${U:-$PWD/tabs}
T=${TMPDIR:-/tmp}/test_tabs.$$
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
# terminals for the tests, compiled here (../testlib/mkterminfo.py), with
# visible strings: tbc <C>, hts <H>, cr <R>; a space is "."
mk() { python3 "$TL/mkterminfo.py" "$T/ti" "$@"; }
export TERMINFO="$T/ti"
unset TERMINFO_DIRS
mk 'tt|test terminal' 'cols#20' 'tbc=<C>' 'hts=<H>' 'cr=<R>'
mk 'tw|wide test terminal' 'cols#80' 'tbc=<C>' 'hts=<H>' 'cr=<R>'
mk 'tn|no tab stops' 'cols#20' 'cr=<R>'
mk 'tc|clears only' 'cols#20' 'tbc=<C>' 'cr=<R>'
mk 'tx|no cr, no cols' 'tbc=<C>' 'hts=<H>'
# tb ARGS...: the output, spaces as "."
tb() { "$U" "$@" | tr ' ' .; }
t "no options: every 8 (-8)" "<C><R><H>........<H>........<H>...<R>" 0 tb -T tt
t "-8" "<C><R><H>........<H>........<H>...<R>" 0 tb -T tt -8
t "-4" "<C><R><H>....<H>....<H>....<H>....<H>...<R>" 0 tb -T tt -4
t "-1" "<C><R><H>.<H>.<H>.<H>.<H>.<H>.<H>.<H>.<H>.<H>.<H>.<H>.<H>.<H>.<H>.<H>.<H>.<H>.<H>.<H><R>" 0 tb -T tt -1
t "-9" "<C><R><H>.........<H>.........<H>.<R>" 0 tb -T tt -9
t "-0: clear only" "<C><R>" 0 tb -T tt -0
t "a list" "<C><R><H>..........<H>......<H><R>" 0 tb -T tt 1,11,17
t "a list separated by blanks" "<C><R><H>..........<H>......<H><R>" 0 tb -T tt "1 11 17"
t "a list with increments" "<C><R><H>..........<H>......<H><R>" 0 tb -T tt 1,+10,+6
t "a list with increments and blanks" "<C><R><H>..........<H>......<H><R>" 0 tb -T tt "1 +10 +6"
t "a list not starting at 1" "<C><R>....<H>....<H><R>" 0 tb -T tt 5,9
t "one stop" "<C><R>.........<H><R>" 0 tb -T tt 10
t "a stop past the last column: at the last" "<C><R>....<H>...............<H><R>" 0 tb -T tt 5,30
t "the list as several operands" "<C><R>....<H>....<H><R>" 0 tb -T tt 5 9
t "-a" "<C><R><H>.........<H>......<H>....................<H>....................................<H><R>" 0 tb -T tw -a
t "-a2" "<C><R><H>.........<H>......<H>........................<H>................................<H><R>" 0 tb -T tw -a2
t "-c" "<C><R><H>.......<H>....<H>....<H>....<H>...................................<H><R>" 0 tb -T tw -c
t "-c2" "<C><R><H>.....<H>....<H>....<H>...................................<H><R>" 0 tb -T tw -c2
t "-c3: its 17 stops" "17" 0 sh -c '"$1" -T tw -c3 | grep -o "<H>" | wc -l | tr -d " "' sh "$U"
t "-f" "<C><R><H>......<H>....<H>....<H>....<H>....<H><R>" 0 tb -T tw -f
t "-p: its 16 stops" "16" 0 sh -c '"$1" -T tw -p | grep -o "<H>" | wc -l | tr -d " "' sh "$U"
t "-s" "<C><R><H>.........<H>.............................................<H><R>" 0 tb -T tw -s
t "-u" "<C><R><H>...........<H>........<H>........................<H><R>" 0 tb -T tw -u
t "the stops are where a tab goes: column 1 + 8k for -8" "1 9 17 25 33 41 49 57 65 73" 0 sh -c '"$1" -T tw -8 | sed "s/<C><R>//" | awk "{ s = \$0; col = 1; out = \"\"; while (s != \"\") { if (substr(s, 1, 3) == \"<H>\") { out = out (out == \"\" ? \"\" : \" \") col; s = substr(s, 4) } else if (substr(s, 1, 1) == \" \") { col++; s = substr(s, 2) } else s = substr(s, 2) } print out }"' sh "$U"
t "the last option counts" "<C><R><H>....<H>....<H>....<H>....<H>...<R>" 0 tb -T tt -8 -4
t "TERM" "<C><R><H>....<H>....<H>....<H>....<H>...<R>" 0 sh -c 'TERM=tt "$1" -4 | tr " " .' sh "$U"
t "-T over TERM" "<C><R><H>....<H>....<H>....<H>....<H>...<R>" 0 sh -c 'TERM=tn "$1" -T tt -4 | tr " " .' sh "$U"
t "-Ttype" "<C><R><H>....<H>....<H>....<H>....<H>...<R>" 0 tb -Ttt -4
t "no cr: a carriage return; no cols: 80" "yes" 0 sh -c '"$1" -T tx | od -c | grep "\\\\r" >/dev/null && [ $("$1" -T tx | grep -o "<H>" | wc -l) = 10 ] && echo yes' sh "$U"
t "-- before the list" "<C><R>....<H>....<H><R>" 0 tb -T tt -- 5,9
# terminals that cannot
t "no tbc: an error" "" 1 "$U" -T tn
t "no tbc: a diagnostic" "" 0 sh -c '"$1" -T tn 2>&1 >/dev/null | grep -q .' sh "$U"
t "no hts: an error" "" 1 "$U" -T tc -8
t "no hts, -0: clearing will do" "<C><R>" 0 tb -T tc -0
t "an unknown terminal type" "" 1 "$U" -T no_such_terminal
# lists that are not valid; usage
t "not ascending" "" 2 "$U" -T tt 10,5
t "equal values" "" 2 "$U" -T tt 5,5
t "a zero" "" 2 "$U" -T tt 0
t "a + first" "" 2 "$U" -T tt +5
t "an empty value" "" 2 "$U" -T tt 1,,5
t "a trailing comma" "" 2 "$U" -T tt 1,5,
t "not a number" "" 2 "$U" -T tt 1,x
t "an unknown option" "" 2 "$U" -T tt -x
t "-12: not a single digit" "" 2 "$U" -T tt -12
t "-T without a type" "" 2 "$U" -T
# after the list there are only more list operands
t "options after operands: tabs 5 -8: -8 is not a list value" "" 2 "$U" -T tt 5 -8
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" -T tt > /dev/full' sh "$U"

done_tests tabs
