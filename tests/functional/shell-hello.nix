with import ./config.nix;

rec {
  hello = mkDerivation {
    name = "hello";
    outputs = [ "out" "dev" ];
    meta.outputsToInstall = [ "out" ];
    buildCommand =
      ''
        mkdir -p $out/bin $dev/bin

        cat > $out/bin/hello <<EOF
        #! ${shell}
        who=\$1
        echo "Hello \''${who:-World} from $out/bin/hello"
        EOF
        chmod +x $out/bin/hello

        cat > $dev/bin/hello2 <<EOF
        #! ${shell}
        echo "Hello2"
        EOF
        chmod +x $dev/bin/hello2
      '';
  };

  hello-symlink = mkDerivation {
    name = "hello-symlink";
    buildCommand =
      ''
        ln -s ${hello} $out
      '';
  };

  forbidden-symlink = mkDerivation {
    name = "forbidden-symlink";
    buildCommand =
      ''
        ln -s /tmp/foo/bar $out
      '';
  };

  salve-mundi = mkDerivation {
    name = "salve-mundi";
    outputs = [ "out" ];
    meta.outputsToInstall = [ "out" ];
    buildCommand =
      ''
        mkdir -p $out/bin

        cat > $out/bin/hello <<EOF
        #! ${shell}
        echo "Salve Mundi from $out/bin/hello"
        EOF
        chmod +x $out/bin/hello
      '';
  };
}
