. "$LIB"
mk <<'X'
.POSIX:
out:
	echo partial > out; sleep 10
dir:
	mkdir -p dir; sleep 10
X
echo "-- SIGINT removes the target being made"; sig INT 0.5 $M; ls out 2>&1
echo "-- an unchanged existing target stays"; echo old > out; T 100 out
mk m2 <<'X'
out: FORCE
	sleep 10
FORCE:
X
sig TERM 0.5 $M -f m2; cat out
echo "-- directories are not removed"; sig HUP 0.5 $M dir; ls -d dir
echo "-- .PRECIOUS targets are kept"; printf '.PRECIOUS: p\np:\n\techo x > p; sleep 10\n' > m4; sig INT 0.5 $M -f m4; ls p
echo "-- an ignored signal stays ignored"; printf 'q:\n\techo x > q; sleep 1\n' > m5; (trap '' INT; python3 -c 'import os,signal,sys,subprocess,time; p=subprocess.Popen(sys.argv[1:]); time.sleep(0.4); p.send_signal(signal.SIGINT); print("status", p.wait())' $M -f m5); ls q
echo "-- -n does not catch signals"; mk m3 <<'X'
x: ; +sleep 10
X
sig TERM 0.5 $M -n -f m3
