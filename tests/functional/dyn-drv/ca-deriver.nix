# From https://github.com/NixOS/nix/issues/16823
let
  pkgs = import <nixpkgs> { };
  innerNix = pkgs.writeText "inner.nix" ''
    derivation {
      name = "inner";
      system = builtins.currentSystem;
      builder = "''${builtins.storePath "${pkgs.busybox}"}/bin/sh";
      args = [ "-c" "echo hi > $out" ];
    }
  '';
in
pkgs.runCommand "inner.drv"
  {
    __contentAddressed = true;
    outputHashMode = "text";
    requiredSystemFeatures = [ "recursive-nix" ];
  }
  ''
    cp $(${pkgs.nix}/bin/nix-instantiate ${innerNix}) $out
  ''
