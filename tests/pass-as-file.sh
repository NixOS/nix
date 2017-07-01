export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

outPath=$(nix-build --no-out-link -E "
with import $NIX_TEST_ROOT/config.nix;

mkDerivation {
  name = \"pass-as-file\";
  passAsFile = [ \"foo\" ];
  foo = [ \"xyzzy\" ];
  builder = builtins.toFile \"builder.sh\" ''
    [ \"\$(cat \$fooPath)\" = xyzzy ]
    touch \$out
  '';
}
")
