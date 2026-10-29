# Cases for difftest.py.  'run' is a shell script (sh); $M is the make under
# test; T N files... sets the modification time of files to N seconds.
# 'differs' lists reference implementations whose behaviour differs for
# a documented reason (the case is then skipped for that reference).

P = r"""# T SECONDS file...: set the times to SECONDS (a fraction allowed) after the
# Epoch, in the form of POSIX touch -d (UTC)
T() {
    _d=$(awk -v s="$1" 'BEGIN {
        f = ""; if (index(s, ".")) { f = substr(s, index(s, ".")); s = substr(s, 1, index(s, ".") - 1) }
        days = int(s / 86400); r = s - days * 86400
        z = days + 719468; era = int(z / 146097); doe = z - era * 146097
        yoe = int((doe - int(doe / 1460) + int(doe / 36524) - int(doe / 146096)) / 365)
        y = yoe + era * 400; doy = doe - (365 * yoe + int(yoe / 4) - int(yoe / 100))
        mp = int((5 * doy + 2) / 153); d = doy - int((153 * mp + 2) / 5) + 1
        m = mp < 10 ? mp + 3 : mp - 9; if (m <= 2) y++
        printf "%04d-%02d-%02dT%02d:%02d:%02d%sZ", y, m, d, int(r / 3600), int(r % 3600 / 60), r % 60, f }')
    shift
    touch -d "$_d" "$@"
}
"""

def case(name, run, files=None, setup='', differs=(), env=None):
    c = dict(name=name, run=P + run, files=files or {}, setup=setup, differs=differs)
    if env:
        c['env'] = env
    return c

CASES = [
case('basic-rebuild', '''
T 100 a.in b.in; $M; echo rc=$?; $M; echo rc=$?; T 300 b.in; $M; echo rc=$?
''', {'Makefile': '''.POSIX:
out: a.mid b.mid
\tcat a.mid b.mid > out
a.mid: a.in
\tcp a.in a.mid
b.mid: b.in
\tcp b.in b.mid
'''}, setup='echo a > a.in; echo b > b.in'),

case('internal-macros', '''
mkdir -p d; T 100 d/x.h y.c; T 50 tgt.o; $M tgt.o; $M -f m2 lib.a
''', {'Makefile': '''.POSIX:
tgt.o: d/x.h y.c d/x.h
\t@echo '@=$@ <=$< ?=$? ^=$^ +=$+ *=$*'
\t@echo 'D: $(@D) $(<D) $(?D) $(^D)'
\t@echo 'F: $(@F) $(<F) $(?F) ${^F} ${+F}'
''', 'm2': '''.POSIX:
lib.a: lib.a(m.o)
lib.a(m.o): m.o
\t@echo '@=$@ %=$% <=$< ?=$?'
m.o: ; @echo making m.o
'''}),

case('macro-forms', '''
$M; $M A=cmd
''', {'Makefile': '''.POSIX:
A = 1
I ::= $(A)
D = $(A)
K :::= $(A) $$HOME
S != echo  "  shell out" ; printf 'x\\ny\\n'
Q ?= first
Q ?= second
DA = a
DA += $(A)
IA ::= i
IA += $(A)
E =
A = 2
N = A
all:
\t@echo 'I=$(I) D=$(D) K=$(K) S=[$(S)] Q=$(Q) DA=$(DA) IA=$(IA) E=[$(E)] nested=$($(N)) $${A}'
'''}),

case('macro-substitution', '''
$M
''', {'Makefile': '''.POSIX:
L = a.c  b.c sub/c.c d.h
EQ = x=y
P = .c
all:
\t@echo '$(L:.c=.o)|$(L:%.c=obj/%.o)|$(L:sub/%=%)|$(L:%=[%])|$(L:.c=)|$(L:$P=.o)|${L:b.c=B}'
\t@echo '$(L:a%=%)|$(L:%.c=%)|$(L:%h=H%)|$(EMPTY:.c=.o)|$(L:x=y)'
'''}),

case('macro-nested-names', '''
$M
''', {'Makefile': '''.POSIX:
V1 = one
V2 = two
N = 1
X = V$(N)
all:
\t@echo $($(X)) $(V$(N)) ${V$(N):one=uno} $(V$(N:1=2))
'''}),

case('macro-precedence', '''
export FROMENV=env OVER=env
$M; echo --; $M OVER=cmd; echo --; $M -e; echo --; MAKEFLAGS='OVER=mf' $M; echo --; MAKEFLAGS='OVER=mf' $M OVER=cmd
''', {'Makefile': '''.POSIX:
OVER = file
all:
\t@echo "FROMENV=$(FROMENV) OVER=$(OVER) shell=$$OVER"
'''}),

case('comments-and-continuation', '''
$M
''', {'Makefile': '''.POSIX:
# a comment \\
  that continues
f=  bar baz\\
    biz
X = value # a comment
all: one \\
     two
\techo ==$f== "[$(X)]" # passed to the shell
\techo a \\
\tb \\
c
one two: ; @echo $@ # stays in the command

\t# a comment line inside commands goes to the shell
'''}),

case('prefixes', '''
$M; echo rc=$?; $M -n; echo rc=$?; $M -s; echo rc=$?; $M -i bad; echo rc=$?
''', {'Makefile': '''.POSIX:
all:
\techo shown
\t@echo hidden
\t-false
\t@-false
\t-@echo both
\t+echo plus
bad:
\tfalse
\techo after
'''}),

case('errors-and-k', '''
$M; echo rc=$?; $M -k; echo rc=$?; $M -k -S; echo rc=$?; $M -i; echo rc=$?
''', {'Makefile': '''.POSIX:
all: a b c
a: ; false
b: ; echo b
c: a ; echo c
'''}),

case('ignore-and-silent-targets', '''
$M; echo rc=$?
''', {'Makefile': '''.POSIX:
.IGNORE: x
.SILENT: y
all: x y z
x: ; false
y: ; echo y
z: ; echo z
'''}),

case('global-silent-ignore', '''
$M; echo rc=$?
''', {'Makefile': '''.POSIX:
.SILENT:
.IGNORE:
all: ; false; echo still
'''}),

case('dash-q', '''
T 100 src; T 200 out; $M -q; echo rc=$?; T 300 src; $M -q; echo rc=$?; cat out; $M -q nosuch; echo rc=$?
''', {'Makefile': '''.POSIX:
out: src
\techo made > out
\t+echo plus line
'''}, setup='echo x > src; echo y > out'),

case('dash-t', '''
T 100 src; T 50 out; $M -t; echo rc=$?; cat out; $M; $M -t ph; ls ph 2>&1
''', {'Makefile': '''.POSIX:
.PHONY: ph
out: src
\techo made > out
\t+echo plus
ph: ; echo phony
'''}, setup='echo x > src; echo old > out'),

case('dash-n-recursive', '''
$M -n; echo rc=$?; ls made 2>&1
''', {'Makefile': '''.POSIX:
all:
\t@echo outer
\t$(MAKE) -f sub.mk
''', 'sub.mk': '''inner:
\ttouch made
\t@echo inner done
'''}),

case('recursion-makeflags', '''
$M -k -s A=1 'B=x y'; $M -n -f Makefile -j 1 t2
''', {'Makefile': '''all:
\t@$(MAKE) -f sub.mk
t2:
\t@$(MAKE) -f sub.mk show
''', 'sub.mk': '''show:
\t@echo "A=$(A) B=$(B)"
\techo 'echoed unless -s'
'''}),

case('inference-builtin-c', '''
$M prog; ./prog; echo rc=$?; $M prog; T 100 prog; $M prog
''', {'prog.c': 'int main(void){return 3;}\n', 'Makefile': '''.POSIX:
CC = cc
CFLAGS = -O
'''}),

case('inference-c-o', '''
$M; $M; T 100 b.o prog; $M
''', {'a.c': 'int main(void){return 0;}\n', 'b.c': 'int b;\n', 'Makefile': '''.POSIX:
CC = cc
CFLAGS =
prog: a.o b.o
\t$(CC) -o $@ a.o b.o
'''}),

case('inference-custom-suffixes', '''
T 100 x.in y.in; $M; echo; $M -r 2>&1; echo rc=$?
''', {'Makefile': '''.POSIX:
.SUFFIXES: .in .out .txt
all: x.out y.txt z
.in.out:
\t@echo "in->out $< $@ $*"; cp $< $@
.out.txt:
\t@echo "out->txt $<"
.in.txt:
\t@echo "in->txt $< $@ $* $?"; cp $< $@
.in:
\t@echo "single $< $@ $*"; cp $< $@
'''}, setup='echo x > x.in; echo y > y.in; echo z > z.in'),

case('inference-suffix-order', '''
T 100 f.a1 f.a2; $M f.b; $M -f m2 f.b
''', {'Makefile': '''.POSIX:
.SUFFIXES: .b .a1 .a2
.a1.b: ; @echo from a1
.a2.b: ; @echo from a2
''', 'm2': '''.POSIX:
.SUFFIXES:
.SUFFIXES: .b .a2 .a1
.a1.b: ; @echo from a1
.a2.b: ; @echo from a2
'''}, setup='touch f.a1 f.a2'),

case('inference-with-target-prereqs', '''
T 100 foo.c; T 200 foo.o; T 300 foo.h; $M foo.o; T 400 foo.c; $M foo.o
''', {'Makefile': '''.POSIX:
.SUFFIXES: .c .o
foo.o: foo.h
.c.o:
\t@echo '<=$< ?=$? ^=$^'
'''}, setup='touch foo.c foo.o foo.h'),

case('empty-inference-rule', '''
T 100 a.x; $M a.y; echo rc=$?
''', {'Makefile': '''.POSIX:
.SUFFIXES: .x .y
.x.y: ;
'''}, setup='touch a.x'),

case('default-target', '''
$M; echo rc=$?; $M -f m2; echo rc=$?
''', {'Makefile': '''.POSIX:
.SUFFIXES: .q .r
.q.r: ; @echo infer
.PHONY: first
first: ; @echo first
second: ; @echo second
''', 'm2': '''.POSIX:
'''}),

case('dot-default', '''
$M; echo rc=$?
''', {'Makefile': '''.POSIX:
all: missing exists
.DEFAULT:
\t@echo "default for $@ <$<>"
'''}, setup='touch exists'),

case('no-rule', '''
$M; echo rc=$?; $M nosuch; echo rc=$?; $M -k; echo rc=$?
''', {'Makefile': '''.POSIX:
all: a missing b
a: ; @echo a
b: ; @echo b
'''}),

case('phony', '''
touch clean all; $M; $M clean; $M -f m2 x
''', {'Makefile': '''.POSIX:
.PHONY: all clean
all: ; @echo all runs
clean: ; @echo clean runs
''', 'm2': '''.POSIX:
.PHONY: p
x: p ; @echo x runs
p: ; @echo p runs
'''}, setup='touch x'),

case('wait-serial', '''
$M
''', {'Makefile': '''.POSIX:
all: one .WAIT two three
one two three: ; @echo $@
'''}),

case('multiple-targets', '''
$M a b; $M -f m2 c
''', {'Makefile': '''.POSIX:
a b: dep
\t@echo "build $@ from $?"
dep: ; @echo dep
''', 'm2': '''.POSIX:
c: p1
c: p2
c: p3
\t@echo $^
p1 p2 p3: ; @echo $@
'''}),

case('include', '''
$M; echo rc=$?; $M -f m2; echo rc=$?; $M -f m3; echo rc=$?
''', {'Makefile': '''.POSIX:
include inc1.mk inc2.mk
-include nosuch.mk
all: ; @echo $(A) $(B)
''', 'inc1.mk': 'A = from-inc1\n', 'inc2.mk': 'B = from-inc2\ninclude inc3.mk\n', 'inc3.mk': 'C = c\n',
'm2': '''.POSIX:
include nosuch.mk
all: ; @echo not reached
''', 'm3': '''.POSIX:
FILES = inc1.mk inc2.mk # comment
include $(FILES)
all: ; @echo $(A) $(B) $(C)
'''}),

case('include-generated', '''
$M; echo rc=$?; $M; echo rc=$?
''', {'Makefile': '''.POSIX:
gen.mk: gen.src
\techo 'G = generated' > gen.mk
include gen.mk
all: ; @echo $(G)
'''}, setup='touch gen.src'),

case('library-members', '''
$M; echo rc=$?; ar t lib.a; $M; echo rc=$?
''', {'a.c': 'int a(void){return 1;}\n', 'b.c': 'int b(void){return 2;}\n', 'Makefile': '''.POSIX:
CC = cc
CFLAGS =
ARFLAGS = -rUc
lib.a: lib.a(a.o) lib.a(b.o)
\t@echo $@ is up to date
'''}),

case('library-member-rule', '''
$M; ar t lib.a; $M
''', {'Makefile': '''.POSIX:
ARFLAGS = rUc
lib.a: lib.a(x.o)
lib.a(x.o): x.o
\t@echo "rule for $@ member $%"
\tar rUc $@ $%
x.o: ; @echo made > x.o
'''}),

case('circular', '''
$M; echo rc=$?
''', {'Makefile': '''a: b ; @echo a
b: c ; @echo b
c: a ; @echo c
'''}),

case('override-commands', '''
$M 2>/dev/null
''', {'Makefile': '''.POSIX:
x: ; @echo first
x: ; @echo second
'''}),

case('stdin-and-multiple-f', '''
printf 'all: ; @echo from stdin $(A)\\n' | $M -f -; printf 'A = a\\n' > one.mk; printf 'all: ; @echo $(A) two\\n' > two.mk; $M -f one.mk -f two.mk
''', {}),

case('shell-macro', '''
printf '#!/bin/sh\\nshift $(($# - 1)); echo "myshell ran: $1"\\n' > myshell; chmod +x myshell
SHELL=/bin/false $M; $M SHELL=$PWD/myshell | sed "s,$PWD,PWD,"
''', {'Makefile': '''all:
\t@echo "shell is $(SHELL)"
'''}),

case('curdir', '''
CURDIR=/elsewhere $M | sed "s,$PWD,PWD,"
''', {'Makefile': '''all:
\t@echo $(CURDIR) $$CURDIR
'''}),

case('make-macro', '''
$M; MAKE=mymake $M
''', {'Makefile': '''all:
\t@echo "$(MAKE)" | sed 's,.*/,,'
'''}, differs=('gnu',)),

case('target-without-commands', '''
T 100 x.h; T 200 x; $M; T 300 x.h; $M
''', {'Makefile': '''.POSIX:
x: hdrs
\t@echo rebuild x
hdrs: x.h
'''}, setup='touch x.h x'),

case('nonexistent-no-commands', '''
T 100 out; $M
''', {'Makefile': '''.POSIX:
out: FORCE
\t@echo forced
FORCE:
'''}, setup='touch out'),

case('double-colon', '''
T 100 a b; T 200 t; $M; T 300 a; $M; T 400 t; T 500 b; $M
''', {'Makefile': '''t:: a
\t@echo from a: $?
t:: b
\t@echo from b: $?
'''}, setup='touch a b t'),

case('pattern-rules', '''
T 100 x.src; $M x.dst; $M -f m2 out/y.o
''', {'Makefile': '''%.dst: %.src
\t@echo "pattern $< -> $@ stem $*"
''', 'm2': '''out/%.o: src/%.c
\t@echo "$< -> $@"
'''}, setup='touch x.src; mkdir -p src out; touch src/y.c'),

case('parallel-jobs', '''
$M -j 4 2>&1 | sort; echo; $M -j4 -f m2 | tr '\\n' ' '
''', {'Makefile': '''all: a b c d
a b c d:
\t@sleep 1; echo $@
''', 'm2': '''all: a b
\t@echo all
a: ; @sleep 1; echo a
b: ; @echo b
'''}),

case('parallel-wait', '''
$M -j 4
''', {'Makefile': '''all: slow .WAIT after
slow: ; @sleep 1; echo slow
after: ; @echo after
'''}),

case('notparallel', '''
$M -j 4
''', {'Makefile': '''.NOTPARALLEL:
all: a b
a: ; @sleep 1; echo a
b: ; @echo b
'''}),

case('parallel-error', '''
$M -j 3; echo rc=$?
''', {'Makefile': '''all: a b
a: ; @sleep 1; echo a
b: ; @false
'''}),

case('jobserver-submake', '''
$M -j 3 2>&1 | sort
''', {'Makefile': '''all: s1 s2
s1 s2:
\t@$(MAKE) -f sub.mk N=$@
''', 'sub.mk': '''all: x y
x y:
\t@sleep 1; echo $(N) $@
'''}),

case('cmdline-macro-forms', '''
$M 'A::=$(B)' B=b 'C:::=$$x $(B)' 'D=$(B)'
''', {'Makefile': '''all:
\t@echo "A=[$(A)] C=[$(C)] D=[$(D)]"
'''}, differs=('gnu',)),

case('macro-in-target-line', '''
$M
''', {'Makefile': '''.POSIX:
T = goal
P = pre1 pre2
$(T): $(P)
\t@echo $@: $^
pre1 pre2: ; @echo $@
'''}),

case('dollar-dollar', '''
$M
''', {'Makefile': '''.POSIX:
all:
\t@echo $$$$ | tr 0-9 N | cut -c1; x=val; echo $$x
'''}),

case('escaped-newline-command', '''
$M
''', {'Makefile': '''.POSIX:
all:
\t@echo one \\
\t  two; \\
\techo three
'''}),

case('env-exported', '''
export E=env; $M; $M E=cmd; echo "after: $E"
''', {'Makefile': '''E = file
N = notexported
all:
\t@echo "E=$$E N=[$$N]"
'''}),

case('up-to-date-messages', '''
T 100 a; T 200 b; $M b; $M c; $M -f m2
''', {'Makefile': '''b: a ; @echo b
c:
''', 'm2': 'all: ;\n'}, setup='touch a b'),

case('special-in-rule-list', '''
$M
''', {'Makefile': '''.POSIX:
all: x
x: ; @echo x
.PHONY: x
.PRECIOUS: x
'''}),

case('suffixes-cleared', '''
T 100 a.c; $M a.o; echo rc=$?
''', {'Makefile': '''.POSIX:
.SUFFIXES:
'''}, setup='touch a.c'),

case('sh-single-suffix', '''
$M script; ./script; ls -l script | cut -c1-10
''', {'script.sh': 'echo running script\n'}),

case('first-target-skips-inference', '''
$M
''', {'Makefile': '''.POSIX:
.SUFFIXES: .a1 .b1
.a1.b1: ; @echo no
.c.o: ; @echo no
real: ; @echo real
'''}),

case('dollar-at-in-prereq-list', '''
$M
''', {'Makefile': '''.POSIX:
x = 1
all: a$(x) ; @echo $@ $<
a1: ; @echo a1
'''}),

case('signal-delete', '''
( $M & sleep 1; kill -s TERM $! ; wait ) 2>&1; ls out 2>&1; ( $M -f m2 & sleep 1; kill -s TERM $!; wait ) 2>&1; ls keep 2>&1
''', {'Makefile': '''out:
\techo partial > out; sleep 5
''', 'm2': '''.PRECIOUS: keep
keep:
\techo partial > keep; sleep 5
'''}),
]

# Known, deliberate differences: case -> {reference: reason}
KNOWN = {
    'macro-forms': {'gnu': 'GNU make 4.3 has no :::='},
    'cmdline-macro-forms': {'gnu': 'GNU make 4.3 has no :::='},
    'make-macro': {'gnu': 'GNU make ignores MAKE from the environment'},
    'global-silent-ignore': {'gnu': 'GNU make keeps sh -e when errors are ignored by .IGNORE'},
    'dash-q': {'gnu': 'GNU make does not run + lines under -q; the standard requires it'},
    'inference-builtin-c': {'gnu': 'GNU built-in rules are written differently'},
    'inference-c-o': {'gnu': 'GNU built-in rules are written differently'},
    'library-members': {'gnu': 'GNU built-in .c.a rule differs from the standard text'},
    'sh-single-suffix': {'gnu': 'GNU .sh rule uses cat instead of cp'},
    'wait-serial': {'gnu': 'GNU make 4.3 has no .WAIT'},
    'parallel-wait': {'gnu': 'GNU make 4.3 has no .WAIT'},
    'multiple-targets': {'gnu': 'GNU make lists the prerequisites of the rule with commands first'},
    'include': {'gnu': 'GNU make remakes include files after reading (other diagnostic)'},
}
for c in CASES:
    k = KNOWN.get(c['name'], {})
    c['differs'] = tuple(set(c['differs']) | set(k))
