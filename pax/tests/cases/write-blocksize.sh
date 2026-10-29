. "$LIB"; echo x > f
for b in "" 512 1b 2k 3x2b 20b; do
  $PAX -w ${b:+-b $b} -f a.tar f; echo "-b ${b:-default}: $(fstat %s a.tar)"
done
$PAX -w -x cpio -f a.cpio f; echo "cpio default: $(fstat %s a.cpio)"
$PAX -w -x cpio -b 100 -f a.cpio f; echo "cpio -b 100: $(fstat %s a.cpio)"
$PAX -w -b 100 -f a.tar f; echo rc=$?
$PAX -w -b 0 -f a.tar f; echo rc=$?
$PAX -w -b 1q -f a.tar f; echo rc=$?
echo "-- reading with a different blocking"; $PAX -w -b 20b f | dd bs=7 2>/dev/null | $PAX -v | cut -c1-10
