with import ./config.nix;

rec {
    a = mkDerivation {
        name = "nar-index-a";
        builder = builtins.toFile "builder.sh"
      ''
        mkdir $out
        mkdir $out/foo
        touch $out/foo-x
        touch $out/foo/bar
        touch $out/foo/baz
        touch $out/qux
        mkdir $out/zyx

        cat >$out/foo/data <<EOF
        lasjdöaxnasd
asdom 12398
ä"§Æẞ¢«»”alsd
EOF
      '';
    };
}