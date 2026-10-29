. "$LIB"
long=$(printf 'abcdefghij%.0s' 1 2 3 4 5 6 7 8 9 10 11 12)
mkdir -p "d/$long"; echo x > "d/$long/f"; ln -s "$long/f" d/sl; printf 'u' > "$(printf 'd/caf\303\251')"
find d -exec "$LTOUCH" -t $T0 {} +
"$LTOUCH" -d '2000-01-01 00:00:00.25' d/sl
echo "-- ustar refuses"; $PAX -w -x ustar -f u.tar d; echo rc=$?; $PAX -f u.tar
echo "-- pax records"; $PAX -w -x pax -f p.tar d; hdrs p.tar | sed 's/PaxHeaders\.[0-9]*\/\(abcdefghij\)\{3,\}[a-j]*/PaxHeaders.N\/BASE/;t;s/PaxHeaders\.[0-9]*/PaxHeaders.N/'
echo "-- round trip"; mkdir o; (cd o && $PAX -r -f ../p.tar); treecmp d o/d
echo "-- -o times"; $PAX -w -x pax -o times -f t.tar d/sl; hdrs t.tar | grep -E 'time|^[0-9a-z] '| sed 's/PaxHeaders\.[0-9]*/PaxHeaders.N/;s/[0-9]* atime=[0-9.]*/NN atime=A/;s/\(PaxHeaders.N\/sl\) [0-9]*/\1 SIZE/'
