. "$LIB"
mkbox sys 'Alice A <alice@a.example>' bob@b.example
printf 'save 1 out\ncopy 2 out\nwrite 2 body.txt\nSave 2\nCopy 1\nh *\nx\n' | $M -n -N
echo "-- out"; grep -E '^(From |Subject|Status)' out
echo "-- body.txt"; cat body.txt
echo "-- files named after the authors"; ls bob alice
echo "-- save with no file goes to mbox"; printf 'save\nx\n' | $M -n -N; grep Subject mbox
echo "-- + and folder"; mkdir Mail; printf 'set folder=Mail\nsave 2 +kept\nfolders\nset outfolder\nSave 1\nx\n' | $M -n -N; ls Mail
