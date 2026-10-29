#!/bin/sh
# test_uudecode.sh - tests for uudecode (POSIX.1-2024).  Run: sh test_uudecode.sh
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
U=${U:-$PWD/uudecode}
T=${TMPDIR:-/tmp}/test_uudecode.$$
mkdir -m 700 "$T" || exit 2
# fstat: exact file status, which POSIX ls does not show (../testlib/fstat.c)
cc=${CC:-}
[ -n "$cc" ] || for c in c17 c99 cc; do command -v $c >/dev/null 2>&1 && { cc=$c; break; }; done
trap 'chmod -R u+rwx "$T" 2>/dev/null; rm -rf "$T" "$T.fstat"' EXIT
$cc -o "$T.fstat" ../testlib/fstat.c || { echo "cannot compile ../testlib/fstat.c"; exit 2; }
fstat() { "$T.fstat" "$@"; }
cd "$T" || exit 2
export LC_ALL=C
root=false; [ "$(id -u)" = 0 ] && root=true
nl='
'
# encoded input: made by Python (binascii, base64), and by hand
python3 -c 'import os; open("rnd", "wb").write(os.urandom(3000))'
cat > enc.py <<'PY'
import binascii, base64, sys
kind, mode, name, src = sys.argv[1:5]
d = open(src, "rb").read()
if kind == "uu":
    print("begin %s %s" % (mode, name))
    for i in range(0, len(d), 45):
        print(binascii.b2a_uu(d[i:i + 45], backtick=len(sys.argv) > 5).decode(), end="")
    print("`" if len(sys.argv) > 5 else " ")
    print("end")
else:
    print("begin-base64 %s %s" % (mode, name))
    for i in range(0, len(d), 57):
        print(base64.b64encode(d[i:i + 57]).decode())
    print("====")
PY
enc() { python3 enc.py "$@"; }
enc uu 644 out1 rnd > in1
enc uu 644 out2 rnd bt > in2
enc b64 644 out3 rnd > in3
t "historical (spaces): the file" "same" 0 sh -c '"$1" in1 && cmp -s out1 rnd && echo same' sh "$U"
t "historical (backticks)" "same" 0 sh -c '"$1" in2 && cmp -s out2 rnd && echo same' sh "$U"
t "Base64" "same" 0 sh -c '"$1" in3 && cmp -s out3 rnd && echo same' sh "$U"
t "the standard input" "same" 0 sh -c 'rm -f out1; "$1" < in1 && cmp -s out1 rnd && echo same' sh "$U"
t "-o: another file" "same" 0 sh -c '"$1" -o other in1 && cmp -s other rnd && echo same' sh "$U"
t "-o -: the standard output" "same" 0 sh -c '"$1" -o - in3 | cmp -s - rnd && echo same' sh "$U"
t "-o /dev/stdout" "same" 0 sh -c '"$1" -o /dev/stdout in3 | cmp -s - rnd && echo same' sh "$U"
t "the pathname -" "hi" 0 sh -c 'printf "hi\n" > h; python3 enc.py b64 644 - h | "$1"' sh "$U"
t "the pathname /dev/stdout" "hi" 0 sh -c 'python3 enc.py uu 644 /dev/stdout h | "$1"' sh "$U"
t "the standard output not used otherwise" "" 0 sh -c '"$1" in1' sh "$U"
# the mode: exactly, the mask has no say
t "the mode, octal" "751" 0 sh -c 'umask 077; python3 enc.py uu 751 m1 h | "$1" && "$2" %a m1' sh "$U" "$T.fstat"
t "the mode, symbolic" "640" 0 sh -c 'umask 077; python3 enc.py uu u=rw,g=r m2 h | "$1" && "$2" %a m2' sh "$U" "$T.fstat"
t "the mode, symbolic, no who: all" "444" 0 sh -c 'python3 enc.py uu =r m3 h | "$1" && "$2" %a m3' sh "$U" "$T.fstat"
t "the mode, symbolic, a copy" "660" 0 sh -c 'python3 enc.py uu u=rw,g=u m4 h | "$1" && "$2" %a m4' sh "$U" "$T.fstat"
t "an existing file: overwritten, its mode set" "hi 600" 0 sh -c 'echo old > m5; chmod 666 m5; python3 enc.py uu 600 m5 h | "$1" && echo $(cat m5) $("$2" %a m5)' sh "$U" "$T.fstat"
if ! $root; then
    t "an existing file that cannot be written: an error" "old" 1 sh -c 'echo old > m6; chmod 444 m6; python3 enc.py uu 644 m6 h | "$1" 2>/dev/null; r=$?; cat m6; exit $r' sh "$U"
fi
# the input around the data
t "lines before the begin line are skipped" "hi" 0 sh -c '{ echo "From: someone"; echo; python3 enc.py uu 644 - h; } | "$1"' sh "$U"
t "what follows the end is not read" "hi" 0 sh -c '{ python3 enc.py uu 644 - h; echo junk; python3 enc.py uu 644 x2 rnd; } | "$1"; [ ! -e x2 ]' sh "$U"
t "Base64 with characters to ignore" "hi" 0 sh -c 'printf "begin-base64 644 -\naG\nk *K\n====\n" | "$1"' sh "$U"
t "CR LF lines" "hi" 0 sh -c 'python3 enc.py b64 644 - h | sed "s/\$/$(printf "\r")/" | "$1"' sh "$U"
t "an empty file, historical" "0" 0 sh -c ': > e; python3 enc.py uu 644 eo e | "$1" && wc -c < eo | tr -d " "' sh "$U"
t "an empty file, Base64" "0" 0 sh -c 'python3 enc.py b64 644 eo2 e | "$1" && wc -c < eo2 | tr -d " "' sh "$U"
t "a pathname with a space" "hi" 0 sh -c 'python3 enc.py uu 644 "a b" h | "$1" && cat "a b"' sh "$U"
# errors
t "no begin line" "" 1 sh -c 'echo nothing | "$1"' sh "$U"
t "no end line" "" 1 sh -c 'python3 enc.py uu 644 - h | sed "\$d" | "$1" > /dev/null' sh "$U"
t "no ==== line" "" 1 sh -c 'python3 enc.py b64 644 - h | sed "\$d" | "$1" > /dev/null' sh "$U"
t "a mode that is not valid: no begin line" "" 1 sh -c 'printf "begin 9z9 x\n\`\nend\n" | "$1"' sh "$U"
t "a missing file" "" 1 "$U" no_such_file
t "two operands" "" 2 "$U" in1 in2
t "an unknown option" "" 2 "$U" -x in1
# with uuencode (of this collection, when it is built)
E=${U%/uudecode/uudecode}/uuencode/uuencode
[ -x "$E" ] && t "uuencode, then uudecode: both encodings" "same" 0 sh -c 'for m in "" -m; do "$2" $m rnd back | "$1" -o - | cmp -s - rnd || exit 1; done; echo same' sh "$U" "$E"
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: uudecode in1 -o x: two operands" "" 2 "$U" in1 -o x

done_tests uudecode
