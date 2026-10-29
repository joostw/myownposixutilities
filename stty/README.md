# stty — set the options for a terminal (POSIX.1-2024)

An implementation of the `stty` utility of POSIX.1-2024 (XCU stty), in C
with no dependencies beyond libc.

    stty [-a|-g]
    stty operand...

## Build and test

    make           # builds ./stty
    make test      # the tests (sh test_stty.sh)
    make install   # into $(PREFIX)/bin; PREFIX=/usr/local, DESTDIR= for staging
    make clean

## What is implemented

* All the operands of the standard: the control, input, output and local
  modes, the control characters (with ^c, ^?, ^- and undef), min and time,
  speeds (also ispeed and ospeed), the combination modes (evenp, oddp,
  parity, raw, cooked, nl, ek, sane, tabs, hup), saved settings, rows, cols
  and size.
* The operands are applied to a copy of the settings, which is set at once
  and read back.

## Choices where the standard leaves room

* No operands: the speed and what differs from sane. -a: everything, a line
  a group. -g: "stty1:" and the flags, speeds and control characters in
  hexadecimal.
* raw is what the standard says it is equivalent to (cs8, erase kill intr
  quit eof eol disabled, -opost, -inpck); it does not turn icanon or isig
  off as historical raw modes did. -raw and cooked set those characters back
  and opost.
* sane: the usual modes and characters; the character size, parity and speed
  are left as they are.
* The window size through the TIOCGWINSZ and TIOCSWINSZ requests of ioctl()
  (the system has no tcgetwinsize() yet).
* An unknown operand or a bad value: status 1 and nothing set. When the
  terminal does not take the input, output and local modes or control
  characters: status 1; the control modes are not checked (a pseudo-terminal
  keeps cs8, no parity).

## Testing

`test_stty.sh` runs the utility on prepared input and compares the output
and the exit status: on pseudo-terminals (`testlib/ptyrun.py`), every mode
set and cleared as the system's stty reads it back, the control characters,
min, time, speeds, the combination modes, -g round trips, the window size,
-a and the default output, errors that set nothing. The binary tested is
`./stty`, or `$U` if set.

## Files

| file | contents |
|------|----------|
| `stty.c` | the utility |
| `test_stty.sh` | the tests: `make test`, or `sh test_stty.sh` |
| `Makefile` | build, test, install, clean |
