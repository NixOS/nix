# A derivation that would certainly fail if several builders tried to
# build it at once.


with import ./config.nix;

mkDerivation {
  name = "simple";
  buildCommand = ''
    mkdir $out
    echo bar >> $out/foo
    sleep 3
    [[ "$(cat $out/foo)" == bar ]]
  '';
}
