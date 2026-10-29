. "$LIB"
mkbox sys a1@x a2@x a3@x
echo "-- hold keeps read messages"; printf 'set hold\np 1\nsave 2 f\nq\n' | $M -n -N >/dev/null; grep -c '^From ' sys; ls mbox 2>&1
echo "-- keepsave moves saved messages to mbox"; mkbox sys a1@x a2@x; rm -f f; printf 'set keepsave\nsave 1 f\nq\n' | $M -n -N; grep Subject mbox
echo "-- append puts them at the end of mbox"; mkbox sys b1@x; printf 'set append\np\nq\n' | $M -n -N >/dev/null; grep Subject mbox
echo "-- noappend puts them first"; mkbox sys c1@x; printf 'p\nq\n' | $M -n -N >/dev/null; grep Subject mbox
echo "-- touch moves unless hold; mbox even with hold"; mkbox sys t1@x t2@x; printf 'set hold\ntouch 1\nmbox 2\nq\n' | $M -n -N; grep Subject sys
echo "-- keep truncates an empty mailbox"; printf 'set keep\nd *\nq\n' | $M -n -N; ls -l sys | cut -d' ' -f5
echo "-- exit keeps everything"; mkbox sys e1@x; printf 'd 1\nexit\n' | $M -n -N; grep -c '^From ' sys
