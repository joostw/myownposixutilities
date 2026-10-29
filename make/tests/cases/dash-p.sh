. "$LIB"
mk <<'X'
.POSIX:
NAME = value
IMM ::= $(NAME)
t: p1 p2
	echo $@
p1 p2:
.SUFFIXES: .q
.q.o:
	cmd $<
X
$M -p -r t | grep -E '^(NAME|IMM) |^t:|^\.q\.o:|^\.SUFFIXES|^	' | sort
echo "-- built-in rules: -p -f /dev/null"; $M -p -f /dev/null 2>/dev/null | grep -c '^\.c\.o:'; $M -p -f /dev/null 2>/dev/null | grep '^\.SUFFIXES'
