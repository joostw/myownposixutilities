. "$LIB"
echo 1 > a; echo 2 > b; echo 3 > c; touch -t $T0 a b c
echo "-- tar"; $PAX -w -f t.tar a; $PAX -w -a -f t.tar b; $PAX -w -a -f t.tar c; $PAX -f t.tar; fstat %s t.tar
echo "-- cpio"; $PAX -w -x cpio -f t.cpio a; $PAX -w -a -f t.cpio b; $PAX -f t.cpio
echo "-- pax onto ustar"; $PAX -w -a -x pax -f t.tar c; echo rc=$?
echo "-- cpio onto tar"; $PAX -w -a -x cpio -f t.tar c; echo rc=$?
echo "-- -u only newer"; touch -t 201001010000 b; $PAX -w -a -u -f t.tar a b; $PAX -v -f t.tar | cut -c1-10,46-
echo "-- -a without -f"; $PAX -w -a a > /dev/null; echo rc=$?
echo "-- extract appended archive"; mkdir o; (cd o && $PAX -r -f ../t.tar && cat a b c)
