source common.sh

clearStore

nix-build --no-out-link -E '
  with import ./config.nix;

  mkDerivation {
    name = "placeholders";
    outputs = [ "out" "bin" "dev" ];
    buildCommand = "
      echo foo1 > $out
      echo foo2 > $bin
      echo foo3 > $dev
      [[ $(cat ${placeholder "out"}) = foo1 ]]
      [[ $(cat ${placeholder "bin"}) = foo2 ]]
      [[ $(cat ${placeholder "dev"}) = foo3 ]]
    ";
  }
'
