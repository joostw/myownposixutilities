. "$LIB"
printf 'set a b=1 c="two words" d=\\"q\\" ask\nset\nunset b\nset noc\nset\nset noasksub\nset\n' | $M -n -f /dev/null 2>&1 | grep -v -E '^(header|indentprefix|prompt|save|sendmail|sendwait|toplines)'
echo "-- if s / if r"; printf 'if r\necho receive\nelse\necho send\nendif\nif s\necho no\nendif\n' > src; printf 'source src\n' | $M -n -f /dev/null
printf 'if s\nset sign=from-send\nendif\n' >> rc; printf '~a\nbody\n' | $M -s x a@b; showsent | tail -n 3
echo "-- echo"; printf 'echo "a\\tb" "c  d" "e\\c" f\necho\n' | $M -n -f /dev/null | od -c | head -n 3
echo "-- startup file errors"; printf 'set x=1\nbogus command\nset y=2\n' > badrc; MAILRC=badrc $M -n -f /dev/null </dev/null; printf 'set\n' | MAILRC=badrc $M -n -f /dev/null 2>/dev/null | grep -E '^(x|y)'
echo "-- not allowed in start-up files"; printf 'mail a@b\n' > badrc2; MAILRC=badrc2 $M -n -f /dev/null </dev/null
