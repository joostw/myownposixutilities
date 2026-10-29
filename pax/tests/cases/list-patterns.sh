. "$LIB"; mktree
$PAX -w -f a.tar src
echo "-- dir pattern selects hierarchy"; $PAX -f a.tar src/dir
echo "-- -d"; $PAX -f a.tar -d src/dir
echo "-- glob"; $PAX -f a.tar 'src/*/s*'
echo "-- bracket"; $PAX -f a.tar 'src/[eo]*'
echo "-- star does not match slash"; $PAX -f a.tar 'src*one'; echo rc=$?
echo "-- star does not match leading period"; mkdir -p p/.hid; touch p/.hid/x p/vis; $PAX -w -f p.tar p; $PAX -f p.tar 'p/*'
echo "-- -c"; $PAX -f a.tar -c 'src/dir' src/fifo
echo "-- -n first match only"; $PAX -f a.tar -n 'src/*'
echo "-- -n dir keeps hierarchy"; $PAX -f a.tar -n 'src/d*'
echo "-- unmatched"; $PAX -f a.tar src/one nosuch 'no*'; echo rc=$?
echo "-- -c unmatched not reported"; $PAX -f a.tar -c nosuch | wc -l; echo rc=$?
