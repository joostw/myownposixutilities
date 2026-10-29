# the scale of sqrt is the larger of scale and the argument's scale, also
# for 0 and 1 (GNU bc gives those scale 0)
printf 'scale=3; scale(sqrt(0)); scale(sqrt(1)); sqrt(1); sqrt(1.00000); scale(sqrt(4))\n' | $BC
