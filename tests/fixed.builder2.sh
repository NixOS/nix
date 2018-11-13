echo dummy: $dummy
if test -n "$dummy"; then sleep 2; fi
mkdir $out
mkdir $out/bla
echo "Hello World!" > $out/foo

if [[ "$(uname)" =~ ^MINGW|^MSYS ]]; then
  nix ln foo $out/bar
else
  ln -s foo $out/bar
fi
