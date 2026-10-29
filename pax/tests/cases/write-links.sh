. "$LIB"
mkdir -p d/x; echo a > d/x/f; ln -s x d/lx; ln -s nowhere d/dangling; ln -s d ld
find . -exec "$LTOUCH" -t $T0 {} +
echo "-- default: symlinks archived"; $PAX -w -f 1.tar ld d; $PAX -v -f 1.tar | cut -c1-10,46-
echo "-- -H follows operands only"; $PAX -w -H -f 2.tar ld; $PAX -v -f 2.tar | cut -c1-10,46-
echo "-- -L follows all"; $PAX -w -L -f 3.tar ld; $PAX -v -f 3.tar | cut -c1-10,46-
echo "-- -H -L last wins"; $PAX -w -L -H -f 4.tar ld; $PAX -f 4.tar | wc -l
echo "-- loop"; mkdir -p loop/a; ln -s .. loop/a/up; $PAX -w -L -f 5.tar loop; echo rc=$?
