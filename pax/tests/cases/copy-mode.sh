. "$LIB"; mktree
echo "-- copy"; mkdir d1; $PAX -rw src d1; echo rc=$?; treecmp src d1/src
echo "-- -l links files"; mkdir d2; $PAX -rw -l src d2; fstat '%h %n' d2/src/one src/dir/sub/big
echo "-- -v"; mkdir d3; $PAX -rw -v src/one src/dir/sym d3
echo "-- -k and -u"; echo changed > d1/src/one; touch -t 201001010000 d1/src/one; $PAX -rw -k src d1; cat d1/src/one; $PAX -rw -u src d1; cat d1/src/one; $PAX -rw src d1; cat d1/src/one
echo "-- -s"; mkdir d4; $PAX -rw -s ',^src,copy,' src/dir d4; ls d4/copy
echo "-- names from stdin"; mkdir d5; printf 'src/one\nsrc/dir/two\n' | $PAX -rw d5; find d5 | sort
echo "-- bad destinations"; $PAX -rw src nosuch; echo rc=$?; $PAX -rw src src/one; echo rc=$?
echo "-- into itself"; mkdir -p t/in; echo z > t/z; $PAX -rw t t/in; echo rc=$?; find t | sort
echo "-- no operands"; $PAX -rw; echo rc=$?
