. "$LIB"
echo "-- :: rules run independently"
mk <<'X'
t:: a
	@echo "rule 1: $? / $^"
t:: b
	@echo "rule 2: $? / $^"
t::
	@echo "rule 3 always"
X
touch a b; T 100 a b; T 200 t; $M; T 300 a; $M
echo "-- pattern rules"
mk m2 <<'X'
.SUFFIXES:
all: x.out y.out
%.out: %.in common.h
	@echo "$@ from $< ($^) stem $*"
%.in:
	@echo "no in for $@"
X
touch x.in common.h; $M -f m2
