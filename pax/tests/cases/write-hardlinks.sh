. "$LIB"
mkdir d; echo data > d/a; ln d/a d/b; ln d/a d/c; touch -t $T0 d/* d
for fmt in ustar pax cpio; do
  echo "== $fmt"; $PAX -w -x $fmt -f a.$fmt d
  $PAX -v -f a.$fmt | cut -c1-10,46-
  rm -rf o; mkdir o; (cd o && $PAX -r -f ../a.$fmt d/c d/b); fstat '%h %s %n' o/d/*
done
echo "== pax linkdata"; $PAX -w -x pax -o linkdata -f l.tar d; hdrs l.tar
rm -rf o; mkdir o; (cd o && $PAX -r -f ../l.tar d/c); cat o/d/c
