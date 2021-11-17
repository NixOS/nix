source common.sh

clearStore

outPath=$(nix-build --no-out-link -E "
with import ./config.nix;

mkDerivation {
  name = \"pass-as-file\";
  passAsFile = [ \"foo\" ];
  foo = [ \"xyzzy\" ];
  builder = builtins.toFile \"builder.sh\" ''
    [ \"\$(basename \$fooPath)\" = .attr-1bp7cri8hplaz6hbz0v4f0nl44rl84q1sg25kgwqzipzd1mv89ic ]
    [ \"\$(cat \$fooPath)\" = xyzzy ]
    touch \$out
  '';
}
")
