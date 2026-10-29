#!/bin/sh
# test_tput.sh - tests for tput (POSIX.1-2024).  Run: sh test_tput.sh
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
U=${U:-$PWD/tput}
T=${TMPDIR:-/tmp}/test_tput.$$
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
# terminals for the tests, compiled here (../testlib/mkterminfo.py); od -c
# shows the bytes written
mk() { python3 "$TL/mkterminfo.py" "$T/ti" "$@"; }
export TERMINFO="$T/ti"
unset TERMINFO_DIRS LINES COLUMNS
mk 'xt|test terminal one' am xenl 'cols#100' 'lines#30' 'it#8' 'colors#8' \
   'clear=\E[H\E[J' 'cup=\E[%i%p1%d;%p2%dH' 'cr=^M' 'tbc=\E[3g' 'hts=\EH' 'el=\E[K$<5>' \
   'is1=<is1>' 'is2=<is2>' 'is3=<is3>' 'rs1=<rs1>' 'rs3=<rs3>' 'hpa=\E[%i%p1%dG' \
   'sgr=%?%p1%t\E[7m%;%?%p2%t\E[4m%;' 'csr=\E[%i%p1%d;%p2%dr' 'cud=\E[%p1%dB' \
   'XT' 'U8#1' 'Ms=\E]52;%p1%s;%p2%s^G' 'Smulx=\E[4:%p1%dm'
mk 'xi|test terminal, tab stops every 4' 'cols#20' 'it#4' 'tbc=<tbc>' 'hts=<hts>' 'cr=<cr>' 'if=FILE'
mk 'xn|test terminal with nothing'
mk 'xw|32-bit numbers' -32 'cols#70000' 'lines#40' 'XN#123456'
mk 'xa|arithmetic' 'cud=%p1%p2%+%d,%p1%p2%-%d,%p1%p2%*%d,%p2%p1%/%d,%p2%p1%m%d' \
   'cub=%p1%p2%=%d%p1%p2%<%d%p1%p2%>%d%p1%p2%A%d%p1%{0}%O%d%p1%!%d%p1%~%d' \
   'cuf=%p1%{2}%&%d,%p1%{8}%|%d,%p1%{3}%\^%d,%'"'"'A'"'"'%c%p2%c' \
   'cuu=%p1%Pa%ga%ga%+%d,%p2%PZ%gZ%d' 'ich=%?%p1%{1}%=%tone%e%p1%{2}%=%ttwo%eother%;' \
   'il=%p1%02d|%p1%-3d|%p1%x|%p1%X|%p1%o|%p1%#x|%p1%:-4d|%p1%+d|%{65}%c' \
   'dch=%p1%l%d' 'dl=[%p1%s][%p1%10s][%p1%-6s]'
printf 'from the file\n' > FILE
t "clear" "033   [   H 033   [   J" 0 sh -c '"$1" -T xt clear | od -c | sed "s/^[0-7]* *//" | sed "/^\$/d"' sh "$U"
t "clear from TERM" "033   [   H 033   [   J" 0 sh -c 'TERM=xt "$1" clear | od -c | sed "s/^[0-7]* *//" | sed "/^\$/d"' sh "$U"
t "-T over TERM" "033   [   H 033   [   J" 0 sh -c 'TERM=dumb "$1" -T xt clear | od -c | sed "s/^[0-7]* *//" | sed "/^\$/d"' sh "$U"
t "clear: absent, no error" "" 0 "$U" -T xn clear
t "init: is1 is2, no tabs (it#8), is3" "<is1><is2><is3>" 0 "$U" -T xt init
t "reset: rs1, then is2 (no rs2), rs3" "<rs1><is2><rs3>" 0 "$U" -T xt reset
t "init: the tab stops every 8 when it is not 8, the init file" "<cr><tbc>        <hts>        <hts><cr>from the file" 0 "$U" -T xi init
t "init: nothing to write" "" 0 "$U" -T xn init
t "reset: nothing to write" "" 0 "$U" -T xn reset
t "several operands" "<is1><is2><is3><rs1><is2><rs3>" 0 "$U" -T xt init reset
# capability names
t "a boolean: true" "" 0 "$U" -T xt am
t "a boolean: false" "" 1 "$U" -T xt bw
t "a boolean: absent" "" 1 "$U" -T xn am
t "a number" "8" 0 "$U" -T xt colors
t "a number: absent" "-1" 0 "$U" -T xn colors
t "cols" "100" 0 "$U" -T xt cols
t "lines" "30" 0 "$U" -T xt lines
t "cols: COLUMNS first" "77" 0 env COLUMNS=77 "$U" -T xt cols
t "lines: LINES first" "33" 0 env LINES=33 "$U" -T xt lines
t "cols: the window size of a terminal" "55" 0 sh -c 'python3 "$TL/ptyrun.py" 1 sh -c "stty cols 55 rows 10 < /dev/tty; \"\$1\" -T xt cols" sh "$1"' sh "$U"
t "32-bit numbers" "70000${nl}123456" 0 "$U" -T xw cols XN
t "a string" "033   [   K" 0 sh -c '"$1" -T xt el | od -c | sed "s/^[0-7]* *//" | sed "/^\$/d"' sh "$U"
t "a string: absent" "" 1 "$U" -T xn el
t "a string with parameters: %i and %d" "033   [   4   ;   5   H" 0 sh -c '"$1" -T xt cup 3 4 | od -c | sed "s/^[0-7]* *//" | sed "/^\$/d"' sh "$U"
t "a string with parameters, none given: as it is" "$(printf '\033[%%i%%p1%%d;%%p2%%dH')" 0 "$U" -T xt cup
t "%? %t %;" "033   [   7   m" 0 sh -c '"$1" -T xt sgr 1 0 | od -c | sed "s/^[0-7]* *//" | sed "/^\$/d"' sh "$U"
t "%? %t %; twice" "033   [   7   m 033   [   4   m" 0 sh -c '"$1" -T xt sgr 1 1 | od -c | sed "s/^[0-7]* *//" | sed "/^\$/d"' sh "$U"
t "arithmetic: + - * / m" "9,5,14,0,2" 0 "$U" -T xa cud 7 2
t "comparisons and logic: = < > A O ! ~" "001110-8" 0 "$U" -T xa cub 7 2
t "& | ^, %'c'" "2,14,5,AB" 0 "$U" -T xa cuf 6 66
t "variables: %P and %g, dynamic and static" "14,9" 0 "$U" -T xa cuu 7 9
t "%e: else" "one two other" 0 sh -c 'echo $("$1" -T xa ich 1) $("$1" -T xa ich 2) $("$1" -T xa ich 3)' sh "$U"
t "printf conversions" "05|5  |ff|FF|377|0xff|255 |+255|A" 0 sh -c '"$1" -T xa il 5 | cut -d "|" -f 1-2 | tr "\n" "|"; "$1" -T xa il 255 | cut -d "|" -f 3-' sh "$U"
t "%s: a string parameter" "[ab][        ab][ab    ]" 0 "$U" -T xa dl ab
t "%l: its length" "5" 0 "$U" -T xa dch hello
t "an extended boolean" "" 0 "$U" -T xt XT
t "an extended number" "1" 0 "$U" -T xt U8
t "an extended string with string parameters" "033   ]   5   2   ;   c   ;   S   G   V   s   b   G   8   =  \a" 0 sh -c '"$1" -T xt Ms c SGVsbG8= | od -c | sed "s/^[0-7]* *//" | sed "/^\$/d"' sh "$U"
t "an extended string with a number" "033   [   4   :   3   m" 0 sh -c '"$1" -T xt Smulx 3 | od -c | sed "s/^[0-7]* *//" | sed "/^\$/d"' sh "$U"
t "padding is left out" "033   [   K" 0 sh -c '"$1" -T xt el | od -c | sed "s/^[0-7]* *//" | sed "/^\$/d"' sh "$U"
t "longname" "test terminal one" 0 "$U" -T xt longname
t "a capability and its parameters, then another" "033   [   2   B   d   u   m   b" 0 sh -c '{ "$1" -T xt cud 2 bw; printf dumb; } | od -c | sed "s/^[0-7]* *//" | sed "/^\$/d" | tr -d "\n" | sed "s/  */   /g"' sh "$U"
# the system's entries, where they are: a known one
if [ -f /usr/share/terminfo/d/dumb ] || [ -f /lib/terminfo/d/dumb ] || [ -f /etc/terminfo/d/dumb ]; then
    t "the system's dumb: cols" "80" 0 env TERMINFO=/nonexistent "$U" -T dumb cols
fi
t "no TERM: the default terminal type" "" 0 sh -c 'unset TERM; "$1" clear' sh "$U"
t "TERMINFO_DIRS" "100" 0 env TERMINFO=/nonexistent TERMINFO_DIRS=/nonexistent:"$T/ti" "$U" -T xt cols
t "a directory named in hexadecimal" "100" 0 sh -c 'mkdir -p h/78 && cp "$2/x/xt" h/78/xt && TERMINFO=$PWD/h "$1" -T xt cols' sh "$U" "$T/ti"
# errors
t "an unknown terminal type: 3" "" 3 "$U" -T no_such_terminal clear
t "a type with a slash: 3" "" 3 "$U" -T ../ti/x/xt clear
t "an entry that is not one: 3" "" 3 sh -c 'mkdir -p "$2/b" && echo garbage > "$2/b/bad" && "$1" -T bad clear' sh "$U" "$T/ti"
t "an unknown capability: 4" "" 4 "$U" -T xt no_such_cap
t "an unknown capability: nothing after it" "" 4 sh -c '"$1" -T xt no_such_cap clear' sh "$U"
t "a parameter that is not a number: 4" "" 4 "$U" -T xt cup x 1
t "no operand: 2" "" 2 "$U" -T xt
t "-T without a type: 2" "" 2 "$U" -T
t "an unknown option: 2" "" 2 "$U" -x clear
t "--" "<is1><is2><is3>" 0 "$U" -T xt -- init
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: tput init -T x: -T is a capability name" "<is1><is2><is3>" 4 "$U" -T xt init -T x
[ -c /dev/full ] && t "a write error: 5" "" 5 sh -c '"$1" -T xt init > /dev/full' sh "$U"

done_tests tput
