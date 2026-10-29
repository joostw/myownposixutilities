# posixutilities - utilities of POSIX.1-2024 (XCU), each in its own directory
#
#   make              build all of them
#   make test         build, then run every test suite (run_tests.sh)
#   make difftest     compare with other implementations (see each README.md)
#   make install      install into $(PREFIX)/bin (and libl.a, liby.a into $(PREFIX)/lib)
#   make clean        remove everything that was built
#
# Macros given on the command line (CC=, CFLAGS=, PREFIX=, DESTDIR=) reach
# every directory.  One utility: cd sed && make.

.POSIX:

UTILS = at awk basename bc cal cat chgrp chmod chown cksum cmp comm \
	compress cp crontab csplit cut date dd diff dirname echo ed env expand \
	expr false file find fold fuser getconf grep head iconv id ipcrm ipcs \
	join lex link ln logger logname ls m4 mailx make mkdir mkfifo more mv \
	newgrp nice nl nohup od paste patch pathchk pax pr printf ps pwd \
	readlink realpath renice rm rmdir sccs sed sh sleep sort split strings \
	stty tabs tail tee test time timeout touch tput tr true tsort tty uname \
	unexpand uniq unlink uudecode uuencode vi wc xargs yacc

all:
	@for u in $(UTILS); do \
	    (cd $$u && $(MAKE)) || exit 1; \
	done

test: all
	sh ./run_tests.sh

difftest: all
	@for u in $(UTILS); do \
	    (cd $$u && $(MAKE) difftest) || exit 1; \
	done

install: all
	@for u in $(UTILS); do \
	    (cd $$u && $(MAKE) install) || exit 1; \
	done

clean:
	@for u in $(UTILS); do \
	    (cd $$u && $(MAKE) clean) || exit 1; \
	done

.PHONY: all test difftest install clean
