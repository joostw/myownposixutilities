. "$LIB"; mktree
echo "-- ustar headers"; $PAX -w -f a.tar src/dir src/fifo; hdrs a.tar
echo "-- names from stdin"; printf 'src/one\n\nsrc/dir/two\n' | $PAX -w -f s.tar; $PAX -f s.tar
echo "-- -d"; $PAX -w -d -f d.tar src/dir; $PAX -f d.tar
echo "-- stdout"; $PAX -w src/one | $PAX
echo "-- missing operand"; $PAX -w -f m.tar nosuch src/one; echo rc=$?; $PAX -f m.tar
echo "-- archive not archived into itself"; (cd src && $PAX -w -f self.tar . ; echo rc=$?; $PAX -f self.tar | grep -c self)
echo "-- -s and -v"; $PAX -w -v -s ',^src,S,' -f v.tar src/one src/dir/sym
$PAX -f v.tar
