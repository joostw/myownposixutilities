. "$LIB"
echo hi > f; touch -t $T0 f
echo "-- keyword=value goes to a global header"; $PAX -w -x pax -o comment=hello,uname=bob -f g.tar f; hdrs g.tar | sed 's/\.[0-9]*\.1 /.P.1 /;s/PaxHeaders\.[0-9]*/PaxHeaders.N/'
$PAX -v -f g.tar
echo "-- keyword:=value in each header"; $PAX -w -x pax -o 'gname:=staff' -o 'path:=other' -f l.tar f; hdrs l.tar | sed 's/PaxHeaders\.[0-9]*/PaxHeaders.N/'
echo "-- exthdr.name and globexthdr.name"; $PAX -w -x pax -o 'exthdr.name=H/%f%%,globexthdr.name=G.%n' -o comment=c -o 'comment:=d' -f n.tar f; hdrs n.tar | grep -v '^ '
echo "-- delete"; $PAX -w -x pax -o comment:=x,delete=comm* -f d.tar f; hdrs d.tar
echo "-- escaped comma"; $PAX -w -x pax -o 'comment:=a\,b' -f e.tar f; hdrs e.tar | grep comment
echo "-- read: -o path:= overrides"; mkdir o; (cd o && $PAX -r -o path:=renamed -f ../g.tar; ls)
echo "-- read: global header applies"; $PAX -f g.tar -o 'listopt=%(uname)s %(comment)s'
echo "-- unknown keyword"; $PAX -w -o frobnicate -f x.tar f; echo rc=$?
echo "-- invalid= values"; $PAX -w -x pax -o invalid=bypass -f x.tar f; echo rc=$?; $PAX -w -o invalid=nonsense -f x.tar f; echo rc=$?
