. "$LIB"
# a stand-in for the SCCS get utility: get [-s] -p file prints the "s." file's text
mkdir bin; cat > bin/get <<'X'
#!/bin/sh
for a; do f=$a; done
sed 1d "$f"
X
cp bin/get bin/sccs; chmod +x bin/get bin/sccs
cat > bin/sccs <<'X'
#!/bin/sh
# sccs [flags] get [flags] file: writes file from SCCS/s.file
for a; do f=$a; done
sed 1d "SCCS/s.$f" > "$f"; echo "sccs get $f"
X
chmod +x bin/sccs
export PATH="$PWD/bin:$PATH"
mk <<'X'
.POSIX:
.SUFFIXES: .sh .sh~
all: tool
X
printf 'header\necho tool from SCCS\n' > s.tool.sh
echo "-- .sh~ rule from s.tool.sh"; $M; ./tool
echo "-- .SCCS_GET for a missing file"
mkdir SCCS; printf 'header\ndata from SCCS\n' > SCCS/s.data
mk m2 <<'X'
show: data
	@cat data
X
$M -f m2
echo "-- a writable file is not retrieved"; chmod 666 data; echo local > data; T 100 data; $M -f m2
echo "-- makefile from SCCS"; rm Makefile; printf 'header\nall: ; @echo from s.makefile\n' > s.makefile; $M
