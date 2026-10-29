. "$LIB"
mkbox sys 'Alice <alice@a.example>'
printf 'file text\n' > inc.txt
printf 'set sign="-- \\nJoe\\tX" Sign=Big\n' >> rc
{
printf 'first\n~r inc.txt\n~< !echo from command\n~t t2@x\n~c c1@x c2@x\n~b b1@x\n~s new subject\n~a\n~A\n~i Sign\n~i nosuchvar\n'
printf '~| tr a-z A-Z\n~| exit 3\n~w draft.txt\n~p\n~! echo shell ran\n~: set\n~?\nlast\n'
} | $M -s orig t1@x 2>&1 | grep -v -E '^(header|indentprefix|prompt|save|sendmail|sendwait|toplines|asksub|sign|Sign)|^~'
showsent
echo "-- draft.txt"; cat draft.txt
echo "-- ~f, ~m, ~F in receive mode"; printf 'set indentprefix="> "\nignore message-id to\nm x@y\n~f 1\n~m 1\n~M 1\n.\n' | $M -n -N; showsent | grep -v '^Date'
echo "-- ~q saves the letter, ~x does not"; printf 'unsaved\n~q\n' | $M -s q a@b; echo rc=$?; awk '/^Subject/ { n = 3 } n > 0 { print; n-- }' dead.letter | norm; rm dead.letter; printf 'gone\n~x\n' | $M -s x a@b; echo rc=$?; ls dead.letter 2>&1
echo "-- ~d reads it back"; printf 'dead body\n~q\n' | $M -s d a@b >/dev/null; printf '~d\n' | $M -s again a@b; showsent | tail -n 6 | norm
echo "-- a disabled escape"; printf 'set escape=\n' >> rc; printf '~s not a command\n' | $M -s plain a@b; showsent | tail -n 2
echo "-- another escape character"; printf 'set escape=!\n' >> rc; printf '!s via bang\n~s literal\n' | $M a@b; showsent | grep -E 'Subject|literal'
