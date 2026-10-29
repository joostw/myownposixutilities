# BC_LINE_LENGTH sets where long numbers are split (0: never)
for n in 10 70 0 1; do
    printf '2^120\n' | BC_LINE_LENGTH=$n $BC
done
