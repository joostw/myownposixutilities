. "$LIB"; mktree
$PAX -w -x pax -f a.tar src
$PAX -v -f a.tar -o 'listopt=%M %(uid)d %5(size)d %(mtime)T %L'
echo "--"; $PAX -v -f a.tar -o 'listopt=%(path)s|%(mtime=%Y-%m-%d %H:%M:%S)T|%.1M|%(typeflag)s|%(mode)s|%D\n' src/one src/fifo
echo "--"; $PAX -v -f a.tar -o listopt='%F %(name)s' -o 'listopt=[%(prefix,name)F]' src/dir/two
echo "-- cpio keywords"; $PAX -w -x cpio -f a.cpio src/one src/dir/sym
$PAX -v -f a.cpio -o 'listopt=%(c_name)s %(c_mode)o %(c_filesize)d %(c_nlink)d %(c_mtime)T %L'
echo "-- escapes"; $PAX -f a.tar -v -o 'listopt=%%\t%(path)s\\' src/one | od -c | head -n 2
