. "$LIB"; mktree
for fmt in ustar pax cpio; do
  echo "== $fmt"
  rm -rf out; mkdir out
  (cd src && $PAX -w -x $fmt -f ../a.$fmt .); echo rc=$?
  (cd out && $PAX -r -f ../a.$fmt); echo rc=$?
  treecmp src out
  fstat '%h %n' out/one out/dir/hard
  readlink out/dir/sym
done
