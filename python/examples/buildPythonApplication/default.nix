{
  system ? builtins.currentSystem,
  pkgs ? import (fetchTarball {
    url = "https://github.com/NixOS/nixpkgs/archive/545c7a31e5dedea4a6d372712a18e00ce097d462.tar.gz";
    sha256 = "1dbsi2ccq8x0hyl8n0hisigj8q19amvj9irzfbgy4b3szb6x2y6l";
  }) {
    config = {};
    overlays = [];
    inherit system;
  },
  nix ? (import ../../..).default,
  testScripts,
}:
let
  python = pkgs.python3;
  nixBindings = nix.python-bindings.override { inherit python; };
in python.pkgs.buildPythonApplication {
  pname = "hello-nix";
  version = "0.1";
  src = builtins.path {
    path = ./.;
    filter = path: type:
      pkgs.lib.hasPrefix (toString ./hello) path
      || path == toString ./setup.py;
  };
  propagatedBuildInputs = [ nixBindings ];
  doInstallCheck = true;
  nativeCheckInputs = [
    nix
  ];
  installCheckPhase = ''
    (
      source ${testScripts}/share/bash/nix-test.sh
      $out/bin/hello-nix
    )
  '';
}
