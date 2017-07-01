export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

read -r -d '' scr3 <<EOF || true
  with import $NIX_TEST_ROOT/config.nix;

  mkDerivation {
    name = "placeholders";
    outputs = [ "out" "bin" "dev" ];
    buildCommand = "
      echo foo1 > \$out
      echo foo2 > \$bin
      echo foo3 > \$dev
      [[ \$(cat \${placeholder "out"}) = foo1 ]]
      [[ \$(cat \${placeholder "bin"}) = foo2 ]]
      [[ \$(cat \${placeholder "dev"}) = foo3 ]]
    ";
  }
EOF
nix-build --no-out-link -E "$scr3"

echo XYZZY
