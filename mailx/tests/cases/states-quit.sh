. "$LIB"
mkbox sys a1@x a2@x a3@x a4@x a5@x a6@x a7@x
printf 'p 1\np 2\nd 3\nsave 4 saved.box\nhold 5\nmbox 6\nh *\nq\n' | $M -n -N | norm
echo "-- the system mailbox keeps new, preserved and held messages"; grep -E '^(From |Subject|Status)' sys
echo "-- mbox has read and mbox-marked messages"; grep -E '^(Subject|Status)' mbox
echo "-- saved.box"; grep -E '^(Subject|Status)' saved.box
echo "-- a second session: unread"; printf 'h *\nx\n' | $M -n -N
