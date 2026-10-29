. "$LIB"
echo "-- nested includes to depth 20"
for i in $(awk 'BEGIN { for (i = 1; i <= 20; i++) print i }'); do echo "include inc$((i+1)).mk" > inc$i.mk; echo "V$i = $i" >> inc$i.mk; done; echo 'LAST = deep' > inc21.mk
mk <<'X'
include inc1.mk
all: ; @echo $(V1) $(V20) $(LAST)
X
$M
echo "-- generated include runs even with -n and -q"
mk m2 <<'X'
.POSIX:
all:
gen.mk:
	@echo generating; echo 'G = yes' > gen.mk
include gen.mk
all: ; @echo G=$(G)
X
$M -n -f m2; rm gen.mk; $M -q -f m2; echo rc=$?
echo "-- -include of a file that cannot be made"; printf -- '-include $(NONE) a.mk b.mk\nall: ; @echo ok\n' > m3; $M -f m3
echo "-- several files and patterns on one line"; echo 'X1 = 1' > p1.mk; echo 'X2 = 2' > p2.mk; printf 'include p*.mk\nall: ; @echo $(X1)$(X2)\n' > m4; $M -f m4
echo "-- relative to the working directory"; mkdir sub; echo 'S = from-cwd' > s.mk; echo 'include s.mk' > sub/inner.mk; printf 'include sub/inner.mk\nall: ; @echo $(S)\n' > m5; $M -f m5
