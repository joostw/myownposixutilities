. "$LIB"
echo old > f; touch -t 200001010000 f
$PAX -w -f a.tar f
mkdir o; cd o
echo newer > f; touch -t 201001010000 f
echo "-- -u keeps newer file"; $PAX -r -u -f ../a.tar; cat f
echo "-- -k keeps existing"; $PAX -r -k -f ../a.tar; cat f
echo "-- plain replaces"; $PAX -r -f ../a.tar; cat f
echo "-- -u replaces older"; echo older > f; touch -t 199001010000 f; $PAX -r -u -f ../a.tar; cat f
echo "-- dir over file"; cd ..; mkdir -p d2/f; touch -t $T0 d2/f; (cd d2 && $PAX -w -f ../d.tar f); cd o; $PAX -r -f ../d.tar; ls -ld f | cut -c1
echo "-- file over empty dir"; $PAX -r -f ../a.tar; cat f
