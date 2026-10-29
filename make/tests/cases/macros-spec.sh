. "$LIB"
mk <<'X'
.POSIX:
A = 1
IMM ::= $(A) $$x
KEEP :::= $(A) $$x
DEL = $(A) $$x
A = 2
IA ::= a
IA += $(A) $$y
DA = a
DA += $(A) $$y
SH != printf '  lead\n\nmid\nend\n'
SH2 != exit 3
C ?= c1
C ?= c2
all:
	@echo 'IMM=[$(IMM)] KEEP=[$(KEEP)] DEL=[$(DEL)]'
	@echo 'IA=[$(IA)] DA=[$(DA)]'
	@echo 'SH=[$(SH)] SH2=[$(SH2)] C=[$(C)]'
X
$M; A=env $M; A=env $M -e
echo "-- 100 levels of indirection"
{ echo 'V0 = bottom'; for i in $(awk 'BEGIN { for (i = 1; i <= 120; i++) print i }'); do echo "V$i = \$(V$((i-1)))"; done; echo 'all: ; @echo $(V120)'; } > deep.mk; $M -f deep.mk
echo "-- self reference"; printf 'X = $(X) a\nall: ; @echo $(X)\n' > self.mk; $M -f self.mk; echo rc=$?
echo "-- substitution forms"
mk sub.mk <<'X'
L = a.c b.c lib%.c x.c.c
all:
	@echo '[$(L:.c=.o)] [$(L:%.c=%.o)] [$(L:lib%%.c=X%)] [$(L:%=%%)] [$(L:a.%=A.%)] [$(L:.c=.o=z)]'
X
$M -f sub.mk
