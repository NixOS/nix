export PATH=/bin:/usr/bin:$PATH

mkdir $out
mkdir $out/bla
echo "Hello World!" > $out/foo
ln -s foo $out/bar
