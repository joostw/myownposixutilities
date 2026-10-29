. "$LIB"
printf 'int f(void){return 1;}\n' > f.c; printf 'int g(void){return 2;}\n' > g.c
mk <<'X'
.POSIX:
CC = cc
CFLAGS =
ARFLAGS = -rUc
lib.a: lib.a(f.o) lib.a(g.o)
	@echo "lib $@ updated with $?"
X
echo "-- build"; $M; ar t lib.a
echo "-- up to date"; $M
echo "-- one member out of date"; sleep 2; touch g.c; $M
echo "-- members with long names and BSD-style archives"
printf 'int h(void){return 3;}\n' > a_rather_long_member_name.c
mk m2 <<'X'
.POSIX:
CC = cc
CFLAGS =
ARFLAGS = -rUc
lib.a: lib.a(a_rather_long_member_name.o)
X
$M -f m2; $M -f m2; ar t lib.a
echo "-- -t touches a member"; sleep 2; touch f.c; $M -t; $M
echo "-- member macros"
mk m3 <<'X'
lib.a(f.o): f.c
	@echo '@=$@ %=$% <=$< *=$* ?=$? @D=$(@D) %F=$(%F)'
X
sleep 2; touch f.c; $M -f m3 'lib.a(f.o)'
