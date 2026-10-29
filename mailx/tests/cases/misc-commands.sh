. "$LIB"
fresh() { mkbox sys a1@x a2@x a3@x; }
fresh; printf 'alias\nalias team a b\nalias team c\nalias\nalias team\nunalias team\nalias\nx\n' | $M -n -N
echo "-- ignore and retain"; fresh; printf 'ignore message-id To\nignore\np 1\nretain subject\np 2\nretain\nP 3\nx\n' | $M -n -N
echo "-- pipe"; fresh; printf 'pipe 1 "wc -l"\nset cmd="grep body"\npipe\n| 2-3 "grep -c second"\nset page\npipe 1 "od -c | tail -n 3"\nx\n' | $M -n -N
echo "-- size and top"; fresh; printf 'set toplines=3\ntop 1 2\nsize\nsize *\nx\n' | $M -n -N
echo "-- dp and dt"; fresh; printf 'dp 1\ndt\ndp\n=\nx\n' | $M -n -N | grep -v -E '^[A-Za-z-]+: |^body|^second|^$|^From '
echo "-- undelete and autoprint"; fresh; printf 'set autoprint\nd 2\nu\nx\n' | $M -n -N | grep -E 'Message|Subject'
echo "-- next after delete"; fresh; printf 'next 3\nd 3\nnext\nx\n' | $M -n -N | grep -E '^Message|^At EOF'
echo "-- headers and z"; mkbox sys m1@x m2@x m3@x m4@x m5@x; printf 'set screen=2\nh\necho z\nz\necho z\nz\necho z\nz\necho z -\nz -\necho h 5\nh 5\n=\nx\n' | $M -n -N
echo "-- cd and !"; fresh; mkdir sub; printf 'cd sub\n!pwd | sed "s,.*/,,"\nset bang\n!echo once\n!echo again: !\nx\n' | $M -n -N
echo "-- file and folder"; fresh; mkbox other o1@x o2@x; printf 'file other\nfile\nfile #\nfile &\nfolder %%\nx\n' | $M -n -N | sed "s,$HOME,HOME,"
echo "-- list and help"; printf 'list\n?\nx\n' | $M -n -N | head -n 3
echo "-- unknown command"; printf 'frobnicate\nx\n' | $M -n -N; echo rc=$?
echo "-- abbreviations"; printf 'ec abbreviated\nfo\nse x=1\nuns x\n=\nx\n' | $M -n -N 2>&1 | head -n 5
