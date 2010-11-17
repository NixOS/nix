{ version }:

with import ./config.nix;

mkDerivation {
  name = "foo-${toString version}";
  builder = builtins.toFile "builder.sh"
    ''
      mkdir $out
      seq 1 1000000 > $out/foo
      ${if version == 2 then ''
        seq 1000000 1010000 >> $out/foo
      '' else ""}
    '';
}
