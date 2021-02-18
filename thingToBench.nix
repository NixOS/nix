let
  # pkgs = (builtins.getFlake "github:NixOS/nixpkgs?rev=ad0d20345219790533ebe06571f82ed6b034db31").legacyPackages.x86_64-linux;
  pkgs = import <nixpkgs> {};
in
  pkgs.runCommandNoCC "foo" {
    buildInputs = [
      pkgs.firefox
      pkgs.pandoc
      # pkgs.nixosTests.xfce
    ];
  }
  "echo bar > $out"
