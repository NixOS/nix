{ }:

with import ./config.nix;

rec {
  setupSh = builtins.toFile "setup" ''
    export VAR_FROM_STDENV_SETUP=foo
    for pkg in $buildInputs; do
      export PATH=$PATH:$pkg/bin
    done
  '';

  stdenv = mkDerivation {
    name = "stdenv";
    buildCommand = ''
      mkdir -p $out
      ln -s ${setupSh} $out/setup
    '';
  };

  shellDrv = mkDerivation {
    name = "shellDrv";
    builder = "/does/not/exist";
    VAR_FROM_NIX = "bar";
    inherit stdenv;
  };

  # Used by nix-shell -p
  runCommand = name: args: buildCommand: mkDerivation (args // {
    inherit name buildCommand stdenv;
  });

  foo = runCommand "foo" {} ''
    mkdir -p $out/bin
    echo 'echo foo' > $out/bin/foo
    chmod a+rx $out/bin/foo
    ln -s ${shell} $out/bin/bash
  '';

  bar = runCommand "bar" {} ''
    mkdir -p $out/bin
    echo 'echo bar' > $out/bin/bar
    chmod a+rx $out/bin/bar
  '';

  bash = shell;
}
