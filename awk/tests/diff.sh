#!/bin/sh
# differential runner: compare ./awk with a reference awk
REF=${REF:-"gawk --posix"}
cd "$(dirname "$0")"
pass=0; fail=0
t() {
  name=$1; cmd=$2
  a=$(AWK=$(pwd)/../awk timeout 10 sh -c "$cmd" 2>&1 </dev/null)
  b=$(AWK="$REF" timeout 10 sh -c "$cmd" 2>&1 </dev/null)
  if [ "$a" = "$b" ]; then pass=$((pass+1)); else fail=$((fail+1)); printf '=== %s\n--- ours\n%s\n--- ref\n%s\n' "$name" "$a" "$b"; fi
}
. ./${CASES:-cases.sh}
echo "pass=$pass fail=$fail"
