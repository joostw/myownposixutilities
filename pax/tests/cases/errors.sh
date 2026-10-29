. "$LIB"; echo x > f; $PAX -w -f a.tar f
echo "-- empty archive"; $PAX < /dev/null; echo rc=$?
echo "-- not an archive"; awk 'BEGIN { while (i++ < 100) print "hello world" }' > junk; $PAX -f junk; echo rc=$?
echo "-- truncated"; head -c 700 a.tar > tr.tar; $PAX -f tr.tar; echo rc=$?
echo "-- only zeros"; printf '%01024d' 0 | tr 0 '\000' > z.tar; $PAX -f z.tar; echo rc=$?
echo "-- bad checksum after first member"; $PAX -w -f two.tar f a.tar; python3 -c "
import sys; d=bytearray(open('two.tar','rb').read()); d[1024+5]^=1; open('two.tar','wb').write(d)"; $PAX -f two.tar; echo rc=$?
echo "-- missing archive"; $PAX -f nosuch.tar; echo rc=$?
echo "-- bad options"; $PAX -Z 2>&1 | head -n 1; $PAX -x shar -w f >/dev/null; echo rc=$?; $PAX -f 2>&1 | head -n 1
