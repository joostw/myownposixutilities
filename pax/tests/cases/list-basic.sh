. "$LIB"; mktree
(cd src && $PAX -w -f ../a.tar .); echo rc=$?
$PAX -f a.tar; echo rc=$?
$PAX -v -f a.tar; echo rc=$?
$PAX < a.tar | head -n 3
