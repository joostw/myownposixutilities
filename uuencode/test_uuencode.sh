#!/bin/sh
# test_uuencode.sh - tests for uuencode (POSIX.1-2024).  Run: sh test_uuencode.sh
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
U=${U:-$PWD/uuencode}
T=${TMPDIR:-/tmp}/test_uuencode.$$
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
# the reference: Python's binascii (historical) and base64
python3 -c 'import os; open("rnd", "wb").write(os.urandom(5000))'
printf 'hello world\n' > hello
chmod 640 hello
cat > dec.py <<'PY'
import binascii, base64, sys
lines = sys.stdin.read().split("\n")
if lines[0].startswith("begin-base64 "):
    end = lines.index("====")
    sys.stdout.buffer.write(base64.b64decode("".join(lines[1:end])))
else:
    end = lines.index("end")
    sys.stdout.buffer.write(b"".join(binascii.a2b_uu(l) for l in lines[1:end - 1]))
PY
t "historical: hello" "begin 640 x${nl},:&5L;&\\@=V]R;&0*${nl}\`${nl}end" 0 "$U" hello x
t "Base64: hello" "begin-base64 640 x${nl}aGVsbG8gd29ybGQK${nl}====" 0 "$U" -m hello x
t "historical: decoded by Python" "same" 0 sh -c '"$1" rnd r | python3 dec.py | cmp -s - rnd && echo same' sh "$U"
t "Base64: decoded by Python" "same" 0 sh -c '"$1" -m rnd r | python3 dec.py | cmp -s - rnd && echo same' sh "$U"
t "historical: as Python encodes it, with \` for spaces" "same" 0 sh -c '"$1" rnd r | sed "1d;\$d" | sed "\$d" > a; python3 -c "
import binascii
d = open(\"rnd\", \"rb\").read()
for i in range(0, len(d), 45):
    print(binascii.b2a_uu(d[i:i + 45], backtick=True).decode(), end=\"\")" > b; cmp -s a b && echo same' sh "$U"
t "Base64: as Python encodes it" "same" 0 sh -c '"$1" -m rnd r | sed "1d;\$d" > a; python3 -c "
import base64
d = open(\"rnd\", \"rb\").read()
for i in range(0, len(d), 57):
    print(base64.b64encode(d[i:i + 57]).decode())" > b; cmp -s a b && echo same' sh "$U"
t "historical lines: 61 characters at most" "61" 0 sh -c '"$1" rnd r | awk "{ if (length(\$0) > m) m = length(\$0) } END { print m }"' sh "$U"
t "Base64 lines: 76 characters at most" "76" 0 sh -c '"$1" -m rnd r | awk "{ if (length(\$0) > m) m = length(\$0) } END { print m }"' sh "$U"
t "no trailing blanks" "" 1 sh -c '"$1" rnd r | grep " \$"' sh "$U"
t "an empty file" "begin 644 e${nl}\`${nl}end" 0 sh -c ': > e; chmod 644 e; "$1" e e' sh "$U"
t "an empty file, Base64" "begin-base64 644 e${nl}====" 0 sh -c '"$1" -m e e' sh "$U"
for n in 1 2 3 44 45 46 89 90 91; do
    t "$n bytes: both ways, decoded by Python" "same" 0 sh -c 'python3 -c "import os, sys; sys.stdout.buffer.write(os.urandom($2))" > b$2; for m in "" -m; do "$1" $m b$2 r | python3 dec.py | cmp -s - b$2 || exit 1; done; echo same' sh "$U" $n
done
t "the mode of the file" "begin 4755 x" 0 sh -c ': > s; chmod 4755 s; "$1" s x | sed 1q' sh "$U"
# opened before the chmod, so that it works for any user
t "the mode 0" "begin 000 x" 0 sh -c ': > z; { chmod 0 z; "$1" x | sed 1q; chmod 600 z; } < z' sh "$U"
t "the standard input, a file: its mode" "begin 640 x" 0 sh -c '"$1" x < hello | sed 1q' sh "$U"
t "the standard input, a pipe: 0666 less the mask" "begin 640 x" 0 sh -c 'umask 026; cat hello | "$1" x | sed 1q' sh "$U"
t "the decode pathname as given" "begin 640 a/b/c.txt" 0 sh -c '"$1" hello a/b/c.txt | sed 1q' sh "$U"
t "a missing file" "" 1 "$U" no_such_file x
t "no operands" "" 2 "$U"
t "three operands" "" 2 "$U" a b c
t "an unknown option" "" 2 "$U" -x hello x
# getopt() must not take an option after an operand for one (no permuting)
t "options after operands: uuencode hello -m: -m is the pathname" "begin 640 -m" 0 sh -c '"$1" hello -m | sed 1q' sh "$U"
[ -c /dev/full ] && t "a write error" "" 1 sh -c '"$1" hello x > /dev/full' sh "$U"

done_tests uuencode
