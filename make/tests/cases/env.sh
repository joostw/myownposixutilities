. "$LIB"
mk <<'X'
FROMENV = file
CMD = file
all:
	@echo "macro=$(FROMENV) env=$$FROMENV cmd=$$CMD empty=[$(EMPTY)] defined-null=[$${NULLV-unset}]"
	@echo "SHELL macro=$(SHELL) env=$$SHELL"
X
export FROMENV=env NULLV=; SHELL=/bin/false $M CMD=cmdline; echo "-- -e"; SHELL=/bin/false $M -e
echo "-- SHELL from the command line is used to run commands"; printf 'all:\n\t@echo $$0\n' > m2; printf '#!/bin/sh\necho "myshell $*"\n' > myshell; chmod +x myshell; $M -f m2 SHELL=$PWD/myshell
