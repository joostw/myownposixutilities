. "$LIB"
echo "-- missing separator"; printf 'all\n' > m1; $M -f m1; echo rc=$?
echo "-- commands before a target"; printf '\techo x\n' > m2; $M -f m2; echo rc=$?
echo "-- no targets"; : > m3; $M -f m3; echo rc=$?
echo "-- no makefile"; $M; echo rc=$?
echo "-- no makefile but a target"; $M foo; echo rc=$?
echo "-- missing -f file"; $M -f nosuch; echo rc=$?
echo "-- bad options"; $M -X 2>&1 | head -n 1; $M -j 2>&1 | head -n 1; $M -j 0 2>&1 | head -n 1
echo "-- failing command status"; printf 'all: ; @exit 7\n' > m4; $M -f m4; echo rc=$?
echo "-- killed command"; printf 'all: ; @kill -9 $$$$\n' > m5; $M -f m5; echo rc=$?
echo "-- -k keeps going with independent targets"; printf 'all: a b\na: ; @false\nb: ; @echo b made\n' > m6; $M -k -f m6; echo rc=$?
echo "-- -q with an error"; $M -q -f m6 nosuch; echo rc=$?
