{ version }:

with import ./config.nix;

mkDerivation {
  name = "foo-${toString version}";
  builder = builtins.toFile "builder.sh"
    ''
      mkdir $out
      (for ((n = 1; n < 100000; n++)); do echo $n; done) > $out/foo
      ${if version != 1 then ''
        (for ((n = 100000; n < 110000; n++)); do echo $n; done) >> $out/foo
      '' else ""}
      ${if version == 3 then ''
        echo foobar >> $out/foo
      '' else ""}
    '';
}
