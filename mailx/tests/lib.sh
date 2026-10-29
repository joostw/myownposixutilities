# helpers for the mailx regression cases
# a stand-in for sendmail: records its arguments and the message
cat > fakesend <<'X'
#!/bin/sh
{ echo "== sendmail $*"; sed 's/^Date: .*/Date: DATE/'; } >> "$HOME/sent"
exit ${FAKESEND_STATUS:-0}
X
chmod +x fakesend
printf 'set sendmail=%s/fakesend sendwait quiet\n' "$PWD" > rc
showsent() { cat sent 2>/dev/null; rm -f sent; }
# edit FILE SED-SCRIPT: change a file in place (POSIX sed has no -i)
edit() { sed "$2" "$1" > "$1.new" && mv "$1.new" "$1"; }
# a mailbox with N messages from different senders
mkbox() {
    f=$1; shift
    : > "$f"
    i=0
    for who in "$@"; do
        i=$((i+1))
        printf 'From %s Sat Oct 24 1%d:00:00 2026\nFrom: %s\nTo: joe@here.example\nSubject: message %d\nMessage-ID: <m%d@example>\n\nbody of message %d\nsecond line\n\n' \
            "${who#*<}" "$i" "$who" "$i" "$i" "$i" >> "$f"
    done
    edit "$f" 's/>\( Sat\)/\1/'
}
ME=$(id -un)
# normalise the user's name and the current date in output
# (the user's name as a word: not inside a longer name)
norm() {
    sed -e "s/^$ME\$/ME/" -e "s/^$ME\([^[:alnum:]_]\)/ME\1/" -e "s/\([^[:alnum:]_]\)$ME\$/\1ME/" \
        -e ":a" -e "s/\([^[:alnum:]_]\)$ME\([^[:alnum:]_]\)/\1ME\2/" -e "ta" \
        -e 's/^From \([^[:space:]]*\) ... ... .. ..:..:.. ....$/From \1 DATE/' -e 's/^Date: .*/Date: DATE/'
}
