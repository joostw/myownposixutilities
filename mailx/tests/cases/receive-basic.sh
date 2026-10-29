. "$LIB"
mkbox sys alice@example.com 'Bob B <bob@example.org>' carol@example.net
echo "-- summary and printing"
printf 'h\np\nn\nn\nn\n=\nt 1-2\nx\n' | $M -n
echo "-- -H and -N"
$M -H; printf '=\n' | $M -N
echo "-- -f with a file and an empty mailbox"
cp sys other; printf 'q\n' | $M -f other; : > empty; $M -f empty </dev/null; echo rc=$?; rm sys; $M </dev/null; echo rc=$?
echo "-- -e"
$M -e; echo "no mail: $?"; mkbox sys a@b; $M -e; echo "mail: $?"
