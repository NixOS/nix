args:
let
  pkgs = import ../shell.nix args;
in
{
  bashInteractive = pkgs.runCommand "bash" { } ''
    mkdir -p $out/bin
    echo "echo 2" > $out/bin/bash
    chmod +x $out/bin/bash
  '';
}
