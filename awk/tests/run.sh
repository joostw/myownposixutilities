#!/bin/sh
# Regression tests: ./run.sh [--update]
# Each case in cases*.sh is run with AWK=../awk and compared with expected/NAME.
cd "$(dirname "$0")"
AWKBIN=$(pwd)/../awk
mkdir -p expected
update=0; [ "$1" = "--update" ] && update=1
pass=0; fail=0
# a UTF-8 locale for the multibyte cases, which are skipped without one (POSIX
# names none): $UTF8_LOCALE, or C.UTF-8, or any UTF-8 locale that locale -a lists
U8=${UTF8_LOCALE:-$(locale -a 2>/dev/null | grep -i -E '^c\.utf-?8$' | sed 1q)}
[ -n "$U8" ] || U8=$(locale -a 2>/dev/null | grep -i -E '\.utf-?8$' | sed 1q)
export U8
u8() { [ -n "$U8" ]; }
t() {
  name=$1; cmd=$2
  out=$(AWK=$AWKBIN timeout 20 sh -c "$cmd" 2>&1 </dev/null | sed "s|$AWKBIN|awk|g")
  if [ $update = 1 ]; then printf '%s\n' "$out" > expected/$name; pass=$((pass+1)); return; fi
  if [ "$out" = "$(cat expected/$name 2>/dev/null)" ]; then pass=$((pass+1))
  else fail=$((fail+1)); echo "FAIL: $name"; printf '%s\n' "$out" | diff expected/$name - | head -n 10; fi
}
for f in cases.sh cases2.sh; do . ./$f; done
echo "$pass passed, $fail failed"
[ $fail = 0 ]
