. "$LIB"
mk <<'X'
.POSIX:
# macro names may contain - and .
my-var.x = dash-dot
sp   =   spaces kept at the end   # comment
semi = a;b # c
t1: ; @echo 'after ; a # is part of the command'
t2: p ; @echo "t2 $^"
p:
t3:
	@echo one \
	two
	@echo 'tab then \
	two lines' | cat
t4: a\
    b
	@echo $^
a b:
X
$M t1 t2 t3 t4
printf 'all:\n\t@echo "[$(my-var.x)] [$(sp)] [$(semi)]"\n' > m2; cat Makefile m2 > m3; $M -f m3 all
echo "-- a line with only a comment does not end the commands"
printf 'x:\n\t@echo one\n# comment\n\n\t@echo two\n' > m4; $M -f m4
echo "-- include as a macro name"; printf 'include = inc\nall: ; @echo $(include)\n' > m5; $M -f m5
echo "-- .POSIX may be anywhere"; printf 'all: ; @echo ok\n.POSIX:\n' > m6; $M -f m6
