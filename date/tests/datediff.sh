#!/bin/sh
# datediff.sh - compare date with GNU date (as "date" in PATH): every
# conversion of strftime(), with the flags and widths of POSIX.1-2024, in
# several timezones, with and without -u.  Both are run until they write
# the same %s, so that they format the same second.
# usage: sh tests/datediff.sh ./date
D=$(realpath "${1:-./date}") || exit 2
export LC_ALL=C
pass=0 fail=0
for tz in UTC0 EST5EDT "<+0530>-5:30" "CET-1CEST,M3.5.0,M10.5.0/3" "NZST-12NZDT,M9.5.0,M4.1.0/3" ""; do
    for u in "" -u; do
        for f in %a %A %b %B %c %C %d %D %e %F %g %G %h %H %I %j %m %M %n %p %r %R %S %t %T %u \
                 %U %V %w %W %x %X %y %Y %z %Z %% %Ec %EC %Ex %EX %Ey %EY %Od %Oe %OH %OI %Om \
                 %OM %OS %Ou %OU %OV %Ow %OW %Oy %+4Y %+5Y %+6Y %05Y %6Y %+2C %+3C %04C %+10F \
                 %+11F %+12F %010F %012F %+6G %07G "a %Y b" "%Y%" "%" ""; do
            i=0
            while :; do
                a=$(TZ=$tz "$D" $u "+%s $f"); ra=$?
                b=$(TZ=$tz date $u "+%s $f"); rb=$?
                [ "${a%% *}" = "${b%% *}" ] || [ $i -ge 5 ] || { i=$((i+1)); continue; }
                break
            done
            if [ "$a" = "$b" ] && [ "$ra" = "$rb" ]; then pass=$((pass+1))
            else fail=$((fail+1)); echo "DIFF: TZ=$tz date $u +$f: [$b], ours [$a]"; fi
        done
        a=$(TZ=$tz "$D" $u); b=$(TZ=$tz date $u)
        [ "$a" = "$b" ] || { a=$(TZ=$tz "$D" $u); b=$(TZ=$tz date $u); }
        if [ "$a" = "$b" ]; then pass=$((pass+1))
        else fail=$((fail+1)); echo "DIFF: TZ=$tz date $u: [$b], ours [$a]"; fi
    done
done
echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
