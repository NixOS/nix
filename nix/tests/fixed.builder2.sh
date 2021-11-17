echo dummy: $dummy
if test -n "$dummy"; then sleep 2; fi
mkdir $out
mkdir $out/bla
echo "Hello World!" > $out/foo
ln -s foo $out/bar
