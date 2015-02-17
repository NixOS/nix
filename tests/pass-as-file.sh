source common.sh

clearStore

outPath=$(nix-build --no-out-link -E "
with import ./config.nix;

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
