. "$LIB"
mk <<'X'
.POSIX:
all: out
out: in
	echo build > out
	+echo plus line runs
	@echo at line
sub:
	$(MAKE) -f sub.mk
X
mk sub.mk <<'X'
x:
	@echo sub-make ran with MAKEFLAGS=$(MAKEFLAGS)
X
echo i > in; T 100 out; T 200 in
echo "-- -n prints, runs + lines and \$(MAKE) lines"; $M -n; echo rc=$?; $M -n sub; ls out 2>&1 | head -n 1
echo "-- -q: + lines run, status 1"; $M -q; echo rc=$?
echo "-- -t: touches, + lines run"; $M -t; echo rc=$?; cat out
echo "-- -q after -t"; $M -q; echo rc=$?
echo "-- -t on \$(MAKE) lines runs them"; $M -t sub
echo "-- -s silences touch messages"; T 100 out; T 300 in; $M -t -s; echo rc=$?; $M -q; echo rc=$?
