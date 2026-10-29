# mailx — process messages (POSIX.1-2024)

A from-scratch implementation of the mailx utility as specified in XCU
mailx (POSIX.1-2024), including the User Portability Utilities option
(Receive Mode), in C with no dependencies beyond libc.

## Build and test

    make           # builds ./mailx
    make test      # the tests (sh test_mailx.sh): regression and terminal tests
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

Send Mode (mailx [-E] [-s subject] address...) and Receive Mode
(-e, -f [file], -u user, -H, -i, -N, -n, -F).

Commands: all of those in the standard, with their abbreviations.
alias/group, alternates, cd/chdir, copy/Copy, delete, discard/ignore,
dp/dt, echo, edit, exit/xit, file/folder, folders, followup/Followup,
from, headers, help/?, hold/preserve, if/else/endif, list, mail, mbox,
next, pipe/|, Print/Type, print/type, quit, reply/respond,
Reply/Respond, retain, save/Save, set, shell, size, source, top, touch,
unalias, undelete, unset, visual, write, z, !, # and =.

Message lists: n, n-m, + - . ^ $ *, address, /string and :d :n :o :r :u.

Message states: new, unread, read, deleted, preserved and saved, with
the quit processing of the standard.
  * Read messages move from the system mailbox to mbox (at the front,
    or at the end with append), unless hold is set.
  * mbox overrides hold; touch is overridden by it.
  * Saved messages are removed, or moved to mbox with keepsave.
  * New and unread messages stay, marked "Status: O", so that they are
    unread in the next session.
  * An empty mailbox is removed, or truncated with keep.

Command escapes: ~! ~. ~: ~_ ~? ~A ~a ~b ~c ~d ~e ~f ~F ~h ~i ~m ~M ~p ~q
~r ~< ~s ~t ~v ~w ~x ~|, and ~~ for a line starting with the escape
character.  The escape variable changes or disables the character.

Internal variables: all of those listed, with their defaults (asksub,
header, save, prompt="? ", toplines=5, `indentprefix=<tab>`).  ask is a
synonym of asksub.  DEAD, EDITOR, MBOX, LISTER, PAGER, SHELL and VISUAL
are imported from the environment.

Other behaviour:
  * Quoting rules for arguments, backslash-newline continuation, and
    filename expansion: +file, %, %user, #, &, and shell word expansion.
  * Start-up files: /etc/mailx.rc (skipped with -n), then MAILRC or
    ~/.mailrc.  The commands the standard forbids in them are refused,
    and after an error the rest of the file is ignored.
  * Interrupts: in command mode an interrupt aborts the command.  In
    input mode it asks for a second interrupt before the letter is
    killed; the letter goes to DEAD when save is set.  With ignore set,
    "@" is written and the line is discarded.  In Send Mode with input
    that is not a terminal, signals take their default action.
  * Pagination through PAGER (default more) when crt is set and
    standard output is a terminal.
  * Records of outgoing mail with record, -F, followup/Followup and
    outfolder.  dead.letter and records are in mbox format, with body
    lines starting with "From " escaped as ">From ".

## Mailboxes and delivery

* System mailbox: $MAIL, or `/var/mail/<login>` (/var/spool/mail when
  /var/mail does not exist).  -u user reads `/var/mail/<user>`.
* Mailboxes are mbox files.  A message starts at a "From " line at the
  start of the file or after an empty line, as the standard describes.
* When a mailbox is rewritten, it is locked with fcntl and, where the
  directory allows it, with a .lock file.  Mail that arrived while the
  mailbox was open is kept.
* Delivery runs the mailer as "sendmail -oi -- recipients..." with the
  message on standard input.  The mailer is the sendmail variable
  (an extension), or /usr/sbin/sendmail or /usr/lib/sendmail.
  Without sendwait it runs in the background.  With sendwait, mailx
  waits for it, and a failure is reported, saved in DEAD and gives a
  nonzero exit status.
* Outgoing messages get To, Cc, Subject and Date header fields (Bcc
  recipients are not shown), plus In-Reply-To and References in replies.
  Text that is not ASCII gets MIME-Version, Content-Type with the
  locale's character set, and 8bit encoding.
* debug prints the message and does not deliver it.

## Choices where the standard leaves room

* Formats follow BSD mailx:
  - the header summary is ">N  1 sender  date  lines/bytes  subject";
  - print and type write "Message N:" before each message;
  - top counts the header lines it shows within toplines;
  - size writes "N: bytes";
  - saving reports "file" [New file] lines/chars.
  The state letters are N new, U unread, R read in this session, blank
  read earlier, * saved, P preserved and M marked for mbox.
* The command prompt and the "(continue)", "EOT" and "!" messages are
  written only when standard input is a terminal, as in the BSD and
  System V implementations.  Command escapes are processed from any
  input, so scripts can use them.
* reply with a Reply-To field answers the Reply-To, To and Cc
  addresses.  Without Reply-To it answers From, To and Cc.
  Addresses from To go in the reply's To field; duplicates and Cc
  addresses already in To are dropped.  "Me" means the bare login
  name, login@<this host>, and the alternates; with allnet, any
  address with the same login part.  Recipients taken from a message
  are not alias-expanded.
* An empty system mailbox in Receive Mode gives "No mail for user" and
  status 0; mailx -e gives status 1.
* ~h offers the current value for editing by typing it into the
  terminal (TIOCSTI) where the system allows that.  Otherwise it shows
  the value in brackets, and an empty reply keeps it.
* edit and visual work on temporary copies; the mailbox is not changed.
* pipe sends the messages as print shows them (with discard/retain
  applied).
* Encoded words (RFC 2047) are decoded in header summaries.  Message
  bodies are shown as they are; MIME parts are not decoded.
* Errors in command escapes are reported but do not stop the message
  from being sent, and do not change the exit status.  Errors in
  command mode make the final exit status 1.

## Testing

* tests/run.py: 11 regression scripts with frozen output.  They cover
  message states and quit processing, hold/keepsave/append/keep/touch,
  every message-list form, save/copy/write and Save/Copy with folder
  and outfolder, reply and Reply recipient rules (flipr, metoo,
  alternates, followup), variables and conditionals, every command
  escape, delivery failures and debug, the remaining commands, and
  mail arriving during a session.  A stand-in sendmail records what is
  delivered.
* tests/pty_tests.py: 16 interactive tests on a pseudo-terminal:
  Subject/Cc/Bcc prompts, one and two interrupts, ignore, save,
  ignoreeof, dot, interrupting a command, the prompt, ~h, ~p, ~e, the
  pager with crt, edit, and !.
* Compared with bsd-mailx: identical mailbox and mbox contents after
  sessions using print, delete, save, hold, preserve, mbox, touch,
  keepsave and exit.  Reply recipients are the same except where the
  standard is followed (Reply uses Reply-To) or leaves the choice open.
* Delivery through Postfix to /var/mail and reading the result back.
* All tests pass under AddressSanitizer and UBSan.  Fuzzing with
  4,800 mutated mailboxes and random command and escape sequences
  (9,600 runs) found one crash, a stale current message after a failed
  mailbox change, which is fixed.

## Files

| file | contents |
|------|----------|
| `main.c` | options, start-up, Send and Receive Mode |
| `cmd.c` | command mode: input, interrupts, the command table, commands |
| `msg.c` | mailboxes, message states, summaries, message lists, quit |
| `send.c` | input mode, command escapes, aliases, recording, delivery |
| `var.c` | internal variables, aliases, header field lists |
| `util.c` | strings, quoting, filenames, commands, pager, addresses |
| `tests/` | the test suites and their data (see Testing) |
| `test_mailx.sh` | runs the tests: `make test`, or `sh test_mailx.sh` |
| `Makefile` | build, test, difftest, install, clean |
