with import ./config.nix;

{
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
}
