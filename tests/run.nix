with import ./config.nix;

{
  hello = mkDerivation {
    name = "hello";
    buildCommand =
      ''
        mkdir -p $out/bin
        cat > $out/bin/hello <<EOF
        #! ${shell}
        who=\$1
        echo "Hello \''${who:-World} from $out/bin/hello"
        EOF
        chmod +x $out/bin/hello
      '';
  };
}
