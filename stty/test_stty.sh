#!/bin/sh
# test_stty.sh - tests for stty (POSIX.1-2024).  Run: sh test_stty.sh
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
U=${U:-$PWD/stty}
T=${TMPDIR:-/tmp}/test_stty.$$
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
# every case runs on a new pseudo-terminal (the standard input); the
# system's stty reads back what this one set: its -a, a token a line
on() { python3 "$TL/ptyrun.py" 0 "$@"; }
sys_a() { stty -a | tr ';' '\n' | tr ' ' '\n' | sed '/^$/d'; }
# m NAME OPERANDS... -- TOKENS...: after OPERANDS, the system's stty -a has TOKENS
m() {
    _n=$1; shift
    _ops= ; while [ "$1" != -- ]; do _ops="$_ops $1"; shift; done; shift
    _want=$(printf '%s\n' "$@")
    t "$_n" "$_want" 0 on sh -c '"$1" $2 || exit; shift 2; stty -a | tr ";" "\n" | tr " " "\n" | sed "/^\$/d" > a; for x; do grep -x -e "$x" a || echo "missing $x"; done' sh "$U" "$_ops" "$@"
}
t "not a terminal" "" 1 "$U" < /dev/null
t "not a terminal: -a" "" 1 "$U" -a < /dev/null
t "-a: the speed" "yes" 0 on sh -c '"$1" -a | grep -E "^speed [0-9]+ baud;" >/dev/null && echo yes' sh "$U"
t "-a: the control characters" "intr = ^C;${nl}quit = ^\;${nl}erase = ^?;${nl}kill = ^U;${nl}eof = ^D;${nl}eol = <undef>;${nl}start = ^Q;${nl}stop = ^S;${nl}susp = ^Z;" 0 on sh -c '"$1" sane; "$1" -a | grep -o -E "(intr|quit|erase|kill|eof|eol|start|stop|susp) = [^;]*;"' sh "$U"
t "-a: min and time" "min = 1;${nl}time = 0;" 0 on sh -c '"$1" -a | grep -o -E "(min|time) = [0-9]+;"' sh "$U"
all="parenb parodd cs. hupcl cstopb cread clocal ignbrk brkint ignpar parmrk inpck istrip inlcr igncr icrnl ixon ixany ixoff opost onlcr ocrnl onocr onlret ofill ofdel cr. nl. tab. bs. ff. vt. isig icanon iexten echo echoe echok echonl noflsh tostop"
t "-a: every mode" "" 0 on sh -c '"$1" -a | tr " " "\n" | sed "s/^-//" > a; for x in $2; do grep -x "$x" a >/dev/null || echo "missing $x"; done' sh "$U" "$all"
t "-a: as the system's stty finds the modes" "same" 0 on sh -c '"$1" -a | tr " " "\n" | grep -v "[=;]" | grep . | sort > a; stty -a | tr " " "\n" | grep -v "[=;]" | grep . | sort > b; comm -23 a b | grep . || echo same' sh "$U"
t "no operands: the speed first" "yes" 0 on sh -c '"$1" | sed 1q | grep -E "^speed [0-9]+ baud;\$" >/dev/null && echo yes' sh "$U"
t "no operands: what differs from sane" "-echo" 0 on sh -c '"$1" sane; "$1" -echo; "$1" | sed 1d' sh "$U"
t "no operands, after sane: the speed only" "1" 0 on sh -c '"$1" sane -brkint; "$1" sane; "$1" | sed "s/-brkint//; s/brkint//" | grep -c .' sh "$U"
# the modes: set, then cleared, as the system's stty sees them
for f in brkint ignpar parmrk inpck istrip inlcr igncr icrnl ixon ixany ixoff ignbrk \
         opost onlcr ocrnl onocr onlret ofill ofdel \
         isig icanon iexten echo echoe echok echonl noflsh tostop hupcl; do
    m "$f" $f -- "$f"
    m "-$f" -$f -- "-$f"
done
m "cr1 nl1 tab2 bs1 ff1 vt1" cr1 nl1 tab2 bs1 ff1 vt1 -- cr1 nl1 tab2 bs1 ff1 vt1
m "cr3 tab3" cr3 tab3 -- cr3 tab3
m "cr0 nl0 tab0 bs0 ff0 vt0" cr2 tab1 cr0 nl0 tab0 bs0 ff0 vt0 -- cr0 nl0 tab0 bs0 ff0 vt0
m "tabs: tab0" tab3 tabs -- tab0
m "-tabs: tab3" -tabs -- tab3
m "hup" -hupcl hup -- hupcl
m "-hup" hupcl -hup -- -hupcl
m "several at once, in order" echo -echo icanon -- -echo icanon
# the control characters
t "a control character: ^A" "intr = ^A;" 0 on sh -c '"$1" intr ^A; stty -a | grep -o "intr = [^;]*;"' sh "$U"
t "a control character: ^a (lower case)" "kill = ^A;" 0 on sh -c '"$1" kill ^a; stty -a | grep -o "kill = [^;]*;"' sh "$U"
t "a control character: one character" "erase = x;" 0 on sh -c '"$1" erase x; stty -a | tr ";" "\n" | sed "s/^ *//" | grep -x "erase = .*" | sed "s/\$/;/"' sh "$U"
t "a control character: ^?" "eof = ^?;" 0 on sh -c '"$1" eof ^?; stty -a | grep -o "eof = [^;]*;"' sh "$U"
t "a control character: ^[" "quit = ^[;" 0 on sh -c '"$1" quit "^["; stty -a | grep -o "quit = [^;]*;"' sh "$U"
t "a control character: ^\\ ^] ^^ ^_" "eol = ^_;${nl}start = ^\;${nl}stop = ^];${nl}susp = ^^;" 0 on sh -c '"$1" start "^\\" stop "^]" susp "^^" eol "^_"; stty -a | grep -o -E "(start|stop|susp|eol) = [^;]*;"' sh "$U"
t "a control character: undef" "intr = <undef>;" 0 on sh -c '"$1" intr undef; stty -a | grep -o "intr = [^;]*;"' sh "$U"
t "a control character: ^-" "kill = <undef>;" 0 on sh -c '"$1" kill ^-; stty -a | grep -o "kill = [^;]*;"' sh "$U"
t "a control character, as this stty writes it" "intr = ^A;${nl}erase = x;${nl}kill = <undef>;" 0 on sh -c '"$1" intr ^A erase x kill undef; "$1" -a | grep -o -E "(intr|erase|kill) = [^;]*;"' sh "$U"
t "min and time" "min = 5;${nl}time = 7;" 0 on sh -c '"$1" -icanon min 5 time 7; stty -a | grep -o -E "(min|time) = [0-9]+;"' sh "$U"
# speeds
t "a speed" "speed 9600 baud;" 0 on sh -c '"$1" 9600; stty -a | grep -o "speed [0-9]* baud;"' sh "$U"
t "ospeed" "speed 2400 baud;" 0 on sh -c '"$1" ospeed 2400 ispeed 2400; stty -a | grep -o "speed [0-9]* baud;"' sh "$U"
t "a speed, as this stty writes it" "speed 4800 baud;" 0 on sh -c '"$1" 4800; "$1" | sed 1q' sh "$U"
t "not a speed" "" 1 on "$U" 12345
# the combination modes
t "raw: as the standard defines it" "erase = <undef>;${nl}kill = <undef>;${nl}intr = <undef>;${nl}quit = <undef>;${nl}eof = <undef>;${nl}eol = <undef>;${nl}cs8${nl}-inpck${nl}-opost${nl}icanon" 0 on sh -c '"$1" inpck raw; for x in erase kill intr quit eof eol; do stty -a | tr ";" "\n" | sed "s/^ *//" | grep "^$x ="; done | sed "s/\$/;/"; stty -a | tr " " "\n" | grep -x -e -opost -e -inpck -e cs8 -e icanon' sh "$U"
t "-raw: back" "intr = ^C;${nl}erase = ^?;${nl}opost" 0 on sh -c '"$1" raw; "$1" -raw; stty -a | tr ";" "\n" | sed "s/^ *//" | grep -E -x "(erase|intr) = .*" | sed "s/\$/;/"; stty -a | tr " " "\n" | grep -x opost' sh "$U"
t "cooked: as -raw" "eof = ^D;" 0 on sh -c '"$1" raw cooked; stty -a | grep -o "eof = [^;]*;"' sh "$U"
m "nl: -icrnl" nl -- -icrnl
m "-nl: icrnl -inlcr -igncr" inlcr igncr nl -nl -- icrnl -inlcr -igncr
t "ek" "erase = ^?;${nl}kill = ^U;" 0 on sh -c '"$1" erase x kill y ek; stty -a | tr ";" "\n" | sed "s/^ *//" | grep -E -x "(erase|kill) = .*" | sed "s/\$/;/"' sh "$U"
t "sane: back to the defaults" "same" 0 on sh -c '"$1" sane; stty -a > a; "$1" -echo -icrnl raw intr x tab3 tostop; "$1" sane; stty -a > b; cmp -s a b && echo same' sh "$U"
t "evenp, oddp, -parity: taken" "" 0 on "$U" evenp oddp parity -evenp -oddp -parity
t "cs5 to cs8: taken" "" 0 on "$U" cs5 cs6 cs7 cs8
t "parenb parodd cstopb cread clocal: taken" "" 0 on "$U" parenb -parenb parodd -parodd cstopb -cstopb cread clocal -clocal
# -g
t "-g: one line" "1" 0 on sh -c '"$1" -g | wc -l | tr -d " "' sh "$U"
t "-g: no blanks, *, ? or [" "" 1 on sh -c '"$1" -g | grep "[[:space:]*?[]"' sh "$U"
t "-g: back to the saved settings" "same" 0 on sh -c 's=$("$1" -g); stty -a > a; "$1" -echo raw intr x 9600 tab3 min 3 -icanon; "$1" "$s"; stty -a > b; cmp -s a b && echo same' sh "$U"
t "-g, as other operands" "same" 0 on sh -c 's=$("$1" -g); stty -a > a; "$1" -echo; "$1" -icrnl "$s"; stty -a > b; cmp -s a b && echo same' sh "$U"
t "saved settings that are not valid" "" 1 on "$U" stty1:1:2:3
# the window size
t "rows and cols, size" "30 100" 0 on sh -c '"$1" rows 30 cols 100; "$1" size' sh "$U"
t "rows and cols: the system's stty" "30 100" 0 on sh -c '"$1" rows 30 cols 100; stty size' sh "$U"
t "size: rows and columns" "40 90" 0 on sh -c 'stty rows 40 cols 90; "$1" size' sh "$U"
t "-a: the window size" "rows 40; columns 90;" 0 on sh -c 'stty rows 40 cols 90; "$1" -a | grep -o "rows [0-9]*; columns [0-9]*;"' sh "$U"
# errors: nothing is set
t "an unknown operand" "" 1 on "$U" nosuchmode
t "an unknown operand: nothing set" "icrnl -echo" 0 on sh -c '"$1" -echo; "$1" -icrnl nosuchmode; stty -a | tr " " "\n" | grep -x -e -echo -e icrnl -e -icrnl | tr "\n" " " | sed "s/ \$//"' sh "$U"
t "a mode that cannot be negated" "" 1 on "$U" -cs8
t "a control character without a value" "" 1 on "$U" intr
t "a control character that is two" "" 1 on "$U" intr ab
t "^ and a character that has no control" "" 1 on "$U" intr ^1
t "min 256" "" 1 on "$U" min 256
t "min x" "" 1 on "$U" min x
t "rows without a number" "" 1 on "$U" rows
t "-a and -g" "" 2 on "$U" -a -g
t "-ag" "" 2 on "$U" -ag
t "-g with an operand" "" 2 on "$U" -g echo
t "--" "-echo" 0 on sh -c '"$1" -- -echo; stty -a | tr " " "\n" | grep -x -- -echo' sh "$U"
# getopt() rules: an option after an operand is an operand
t "options after operands: stty echo -a: -a is an operand" "" 1 on "$U" echo -a
[ -c /dev/full ] && t "a write error" "" 1 on sh -c '"$1" -a > /dev/full' sh "$U"

done_tests stty
