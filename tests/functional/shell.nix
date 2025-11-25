{
  inNixShell ? false,
  contentAddressed ? false,
  fooContents ? "foo",
}:

let
  cfg = import ./config.nix;
in
with cfg;

let
  mkDerivation =
    if contentAddressed then
      args:
      cfg.mkDerivation (
        {
          __contentAddressed = true;
          outputHashMode = "recursive";
          outputHashAlgo = "sha256";
        }
        // args
      )
    else
      cfg.mkDerivation;
in

let
  pkgs = rec {
    setupSh = builtins.toFile "setup" ''
      export VAR_FROM_STDENV_SETUP=foo
      for pkg in $buildInputs; do
        export PATH=$PATH:$pkg/bin
      done

      declare -a arr1=(1 2 "3 4" 5)
      declare -a arr2=(x $'\n' $'x\ny')
      fun() {
        echo blabla
      }
      runHook() {
        eval "''${!1}"
      }
    '';

    stdenv =
      mkDerivation {
        name = "stdenv";
        buildCommand = ''
          mkdir -p $out
          ln -s ${setupSh} $out/setup
        '';
      }
      // {
        inherit mkDerivation;
      };

    shellDrv =
      mkDerivation {
        name = "shellDrv";
        builder = "/does/not/exist";
        VAR_FROM_NIX = "bar";
        ASCII_PERCENT = "%";
        ASCII_AT = "@";
        TEST_inNixShell = if inNixShell then "true" else "false";
        FOO = fooContents;
        inherit stdenv;
        outputs = [
          "dev"
          "out"
        ];
      }
      // {
        shellHook = abort "Ignore non-drv shellHook attr";
      };

    # https://github.com/NixOS/nix/issues/5431
    # See nix-shell.sh
    polo = mkDerivation {
      name = "polo";
      inherit stdenv;
      shellHook = ''
        echo Polo
      '';
    };

    # Shells should also work with fixed-output derivations
    fixed = mkDerivation {
      name = "fixed";
      FOO = "was a fixed-output derivation";
      outputHash = "1ixr6yd3297ciyp9im522dfxpqbkhcw0pylkb2aab915278fqaik";
      outputHashMode = "recursive";
      outputHashAlgo = "sha256";
      outputs = [ "out" ];
    };

    # Used by nix-shell -p
    runCommand =
      name: args: buildCommand:
      mkDerivation (
        args
        // {
          inherit name buildCommand stdenv;
        }
      );

    foo = runCommand "foo" { } ''
      mkdir -p $out/bin
      echo '#!${shell}' > $out/bin/foo
      echo 'echo ${fooContents}' >> $out/bin/foo
      chmod a+rx $out/bin/foo
      ln -s ${shell} $out/bin/bash
    '';

    bar = runCommand "bar" { } ''
      mkdir -p $out/bin
      echo '#!${shell}' > $out/bin/bar
      echo 'echo bar' >> $out/bin/bar
      chmod a+rx $out/bin/bar
    '';

    bash = shell;
    bashInteractive = runCommand "bash" { } ''
      mkdir -p $out/bin
      ln -s ${shell} $out/bin/bash
    '';

    # ruby "interpreter" that outputs "$@"
    ruby = runCommand "ruby" { } ''
      mkdir -p $out/bin
      echo '#!${shell}' > $out/bin/ruby
      echo 'printf %s "$*"' >> $out/bin/ruby
      chmod a+rx $out/bin/ruby
    '';

    inherit (cfg) shell;

    callPackage = f: args: f (pkgs // args);

    inherit pkgs;
  };
in
pkgs
