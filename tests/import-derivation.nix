with import ./config.nix;

let

  bar = mkDerivation {
    name = "bar";
    builder = builtins.toFile "builder.sh"
      ''
        echo 'builtins.add 123 456' > $out
      '';
  };

  value = import bar;

in

mkDerivation {
  name = "foo";
  builder = builtins.toFile "builder.sh"
    ''
      echo -n FOO${toString value} > $out
    '';
}
