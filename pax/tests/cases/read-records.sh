. "$LIB"
echo "-- size, uid, mtime records override the header"
python3 - <<'PY'
import tarfile
t = tarfile.TarInfo('huge'); t.size = 9000000000; t.uid = 4000000000; t.mtime = 1234567890.5
t.uname = 'some user'; t.pax_headers = {'comment': 'x'}
open('huge.tar', 'wb').write(t.tobuf(format=tarfile.PAX_FORMAT))
PY
$PAX -f huge.tar -o 'listopt=%(path)s %(size)d %(uid)u %(uname)s %(mtime)T %(comment)s'; echo rc=$?
echo "-- truncated data is detected"; $PAX -v -f huge.tar > /dev/null; echo rc=$?
echo "-- records with an empty value delete the field"
python3 - <<'PY'
import tarfile, io
data = b'abc'
t = tarfile.TarInfo('name-in-header'); t.size = 3
t.pax_headers = {'path': 'from-record', 'uname': ''}
t.uname = 'hdruser'
open('r.tar', 'wb').write(t.tobuf(format=tarfile.PAX_FORMAT) + data + bytes(509) + bytes(1024))
PY
$PAX -v -f r.tar | cut -c1-30,52-
echo "-- global records apply to later members"
python3 - <<'PY'
import tarfile
f = tarfile.open('g.tar', 'w', format=tarfile.PAX_FORMAT, pax_headers={'uname': 'globaluser'})
for n in ('a', 'b'):
    t = tarfile.TarInfo(n); f.addfile(t)
f.close()
PY
$PAX -v -f g.tar | cut -c1-30,52-
