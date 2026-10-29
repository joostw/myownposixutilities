. "$LIB"
mkbox sys a1@x a2@x
echo "-- mail that arrives while the mailbox is open is kept"
{ printf 'p 1\n'; sleep 2; printf 'q\n'; } | $M -n -N > /dev/null &
sleep 1; printf 'From late@x Sat Oct 24 12:00:00 2026\nFrom: late@x\nSubject: late\n\nlate\n\n' >> sys; wait
grep Subject sys; grep Subject mbox
