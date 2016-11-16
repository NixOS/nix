# Test Nix builder sandbox.

{ system, nix }:

with import <nixpkgs/nixos/lib/testing.nix> { inherit system; };

let
  mkUtils = pkgs: pkgs.buildEnv {
    name = "sandbox-utils";
    paths = [ pkgs.coreutils pkgs.utillinux pkgs.bash ];
    pathsToLink = [ "/bin" "/sbin" ];
  };

  utils32 = mkUtils pkgs.pkgsi686Linux;
  utils64 = mkUtils pkgs;

  sandboxTestScript = pkgs.writeText "sandbox-testscript.sh" ''
    [ $(id -u) -eq 0 ]
    touch foo
    chown 1024:1024 foo
    touch "$out"
  '';

  testExpr = arch: pkgs.writeText "sandbox-test.nix" ''
    let
      utils = builtins.storePath
        ${if arch == "i686-linux" then utils32 else utils64};
    in derivation {
      name = "sandbox-test";
      system = "${arch}";
      builder = "''${utils}/bin/bash";
      args = ["-e" ${sandboxTestScript}];
      PATH = "''${utils}/bin";
    }
  '';

in makeTest {
  name = "nix-sandbox";

  machine = { pkgs, ... }: {
    nix.package = nix;
    nix.useSandbox = true;
    nix.binaryCaches = [];
    virtualisation.writableStore = true;
    virtualisation.pathsInNixDB = [ utils32 utils64 ];
  };

  testScript = ''
    $machine->waitForUnit("multi-user.target");
    $machine->succeed("nix-build ${testExpr "x86_64-linux"}");
    $machine->succeed("nix-build ${testExpr "i686-linux"}");
  '';
}
