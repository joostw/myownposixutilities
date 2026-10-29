# limits of scale, ibase and obase: out-of-range values are clamped with
# a warning; values are truncated to integers
t() { printf '%b' "$1" | $BC 2>&1; }
t 'scale = -1; scale\n'
t 'ibase = 1; ibase\n'
t 'ibase = 17; ibase\n'
t 'obase = 1; obase\n'
t 'obase = 1000000000; obase\n'
t 'scale = 3.9; scale; ibase = 8.7; ibase; obase = 9.9; obase\n'
t 'scale = 2147483647; scale\n'
