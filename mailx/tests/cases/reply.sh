. "$LIB"
cat > sys <<'X'
From alice@a.example Sat Oct 24 10:00:00 2026
From: Alice <alice@a.example>
To: ME@here, bob@b.example, Bob <bob@b.example>
Cc: carol@c.example, bob@b.example
Subject: plans
Message-ID: <p1@a.example>

let's meet

From dave@d.example Sat Oct 24 11:00:00 2026
From: dave@d.example
Reply-To: list@l.example
To: ME, other@o.example
Subject: Re: plans

reply-to set

X
edit sys "s/ME/$ME/g"
echo "-- reply: sender and all recipients, without me"; printf 'reply 1\nthanks\n' | $M -n -N; showsent | norm
echo "-- Reply: the sender only"; printf 'Reply 1\nthanks\n' | $M -n -N; showsent | norm
echo "-- Reply-To replaces From; Re: is not doubled"; printf 'reply 2\nok\n' | $M -n -N; showsent | norm
echo "-- Reply to several messages"; printf 'Reply 1 2\nboth\n' | $M -n -N; showsent | norm
echo "-- flipr swaps them"; printf 'set flipr\nr 1\nflipped\n' | $M -n -N; showsent | norm
echo "-- metoo keeps me"; printf 'set metoo\nreply 2\nme too\n' | $M -n -N; showsent | grep sendmail | norm
echo "-- alternates are removed"; printf 'alternates other@o.example\nreply 2\nalt\n' | $M -n -N; showsent | grep sendmail
echo "-- followup records in a file named after the author"; printf 'followup 1\nrecorded\n' | $M -n -N >/dev/null; showsent >/dev/null; grep -c '^From ' alice
echo "-- record"; printf 'set record=rec\nmail x@y\nmailed\n' | $M -n -N; showsent > /dev/null; grep -E '^(To|Subject)|mailed' rec
