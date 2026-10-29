. "$LIB"
# count how many commands run at the same time
mk <<'X'
OBJS = 1 2 3 4 5 6 7 8 9 10 11 12
all: $(OBJS)
$(OBJS):
	@d=lock.$$$$; mkdir $$d; n=$$(ls -d lock.* 2>/dev/null | wc -l); echo $$n >> conc; sleep 1; rmdir $$d
X
for j in 1 3 5; do rm -f conc; $M -j $j; echo "-j $j: at most $(sort -n conc | tail -n 1) at once"; done
echo "-- sub-makes share the pool"
mk m2 <<'X'
all: s1 s2 s3
s1 s2 s3:
	@$(MAKE) -f Makefile
X
rm -f conc; $M -j 4 -f m2; echo "shared -j 4: at most $(sort -n conc | tail -n 1) at once"
echo "-- a different -j in the sub-make makes its own pool"
mk m3 <<'X'
all:
	@$(MAKE) -j 2 -f Makefile
X
rm -f conc; $M -j 6 -f m3; echo "-j 2 below -j 6: at most $(sort -n conc | tail -n 1) at once"
