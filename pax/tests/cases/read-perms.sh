. "$LIB"
mkdir -p s/locked; echo x > s/locked/f; echo y > s/suid; chmod 4755 s/suid; chmod 500 s/locked; chmod 1777 s
find s -exec touch -t $T0 {} +
$PAX -w -f a.tar s
umask 077
echo "-- default: umask applies, no setuid"; mkdir o1; (cd o1 && $PAX -r -f ../a.tar); fstat '%a %n' o1/s o1/s/locked o1/s/suid o1/s/locked/f
echo "-- -pp"; mkdir o2; (cd o2 && $PAX -r -pp -f ../a.tar); fstat '%a %n' o2/s o2/s/locked o2/s/suid
echo "-- -pe"; mkdir o3; (cd o3 && $PAX -r -pe -f ../a.tar); fstat '%a %n' o3/s o3/s/locked o3/s/suid
echo "-- mtime kept"; fstat '%y %n' o1/s/suid o1/s/locked
echo "-- -pm"; mkdir o4; (cd o4 && $PAX -r -pm -f ../a.tar); fstat '%Y' o4/s/suid | awk '{print ($1 > 1000000000) ? "current time" : "archived time"}'
echo "-- -pa keeps mtime"; mkdir o5; (cd o5 && $PAX -r -pa -f ../a.tar); fstat '%y' o5/s/suid
echo "-- bad -p"; $PAX -r -pz -f a.tar; echo rc=$?
chmod -R u+rwx o1 o2 o3 o4 o5
