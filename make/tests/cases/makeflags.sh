. "$LIB"
mk <<'X'
all:
	@echo "MAKEFLAGS=[$(MAKEFLAGS)] env=[$$MAKEFLAGS]"
	@echo "A=[$(A)] B=[$(B)]"
X
echo "-- options and macros are passed on"; $M -k -s -i A=1 'B=two words'
echo "-- letters format"; MAKEFLAGS=ks $M
echo "-- dashed format with macros"; MAKEFLAGS='-k -- A=from\ mf' $M
echo "-- command line beats MAKEFLAGS"; MAKEFLAGS='A=mf' $M A=cmd
echo "-- -S after -k"; MAKEFLAGS=k $M -S
echo "-- -n is passed on"; $M -n | head -n 1
echo "-- -f and -p are not passed on"; $M -f Makefile > o; head -n 1 o
echo "-- -j"; $M -j 3 | sed 's/--jobserver-auth=[0-9]*,[0-9]*/--jobserver-auth=R,W/g'
