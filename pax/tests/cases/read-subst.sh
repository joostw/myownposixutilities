. "$LIB"; mktree
$PAX -w -f a.tar src
mkdir o; cd o
echo "-- rename with p flag"; $PAX -r -s ',^src/,new/,p' -f ../a.tar src/one src/dir/hard
ls new new/dir; fstat '%h %n' new/one new/dir/hard
echo "-- first successful substitution wins"; $PAX -f ../a.tar -s ',one,ONE,' -s ',src,SRC,' src/one src/dir/two
echo "-- g flag and backrefs"; $PAX -f ../a.tar -s ',\([^/]*\)/\([^/]*\),\2+\1,g' src/dir/sub/big
echo "-- & and escaped delimiter"; $PAX -f ../a.tar -s '#o#[&]\##g' src/one
echo "-- empty result skips"; $PAX -f ../a.tar -s ',.*dir.*,,' -s ',src/,,'
echo "-- symlink contents need S"; $PAX -v -f ../a.tar -s ',one,uno,' src/dir/sym | sed 's/.* src/src/'
$PAX -v -f ../a.tar -s ',one,uno,S' src/dir/sym | sed 's/.* src/src/'
echo "-- hard link target follows renamed member"; rm -rf *; $PAX -r -s ',src/one,first,' -f ../a.tar; fstat '%h %n' first src/dir/hard
echo "-- bad expressions"; $PAX -f ../a.tar -s ',a,b'; echo rc=$?; $PAX -f ../a.tar -s ',\(,b,'; echo rc=$?; $PAX -f ../a.tar -s ',a,b,q'; echo rc=$?
