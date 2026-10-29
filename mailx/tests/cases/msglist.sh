. "$LIB"
mkbox sys alice@a.example bob@b.example alice@a.example carol@c.example
edit sys 's/Subject: message 2/Subject: Important News/'
for spec in '2' '2-3' '^' '$' '.' '*' '+' '-' 'alice' 'alice@a.example' '/news' '/NEWS' ':n' ':d' ':r' '1 3' '9' '0' '3-1' 'nobody' ':x'; do
  printf '%s: ' "$spec"; printf 'f %s\nx\n' "$spec" | $M -n -N 2>&1 | awk '{printf "%s ", ($1 ~ /^mailx/) ? $0 : $2} END {print ""}'
done
echo "-- after deleting and reading"; printf 'd 2\np 1\nf :d\nf :r\nf :n\nf *\nu\nf *\n' | $M -n -N 2>&1 | grep -v -e '^[A-Z][a-z-]*:' -e '^$' -e '^body' -e '^second' -e '^From '
