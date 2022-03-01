{ inNixShell ? false, contentAddressed ? false, fooContents ? "foo" }:

let cfg = import ./config.nix; in
with cfg;

let
  mkDerivation =
    if contentAddressed then
      args: cfg.mkDerivation ({
        __contentAddressed = true;
        outputHashMode = "recursive";
        outputHashAlgo = "sha256";
      } // args)
    else cfg.mkDerivation;
in

let pkgs = rec {
  setupSh = builtins.toFile "setup" ''
    export VAR_FROM_STDENV_SETUP=foo
    for pkg in $buildInputs; do
      export PATH=$PATH:$pkg/bin
    done

    # mimic behavior of stdenv for `$out` etc. for structured attrs.
    if [ -n "''${NIX_ATTRS_SH_FILE}" ]; then
      for o in "''${!outputs[@]}"; do
        eval "''${o}=''${outputs[$o]}"
        export "''${o}"
      done
    fi

    declare -a arr1=(1 2 "3 4" 5)
    declare -a arr2=(x $'\n' $'x\ny')
    fun() {
      echo blabla
    }
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
    ASCII_PERCENT = "%";
    ASCII_AT = "@";
    TEST_inNixShell = if inNixShell then "true" else "false";
    inherit stdenv;
    outputs = ["dev" "out"];
  };

  # Used by nix-shell -p
  runCommand = name: args: buildCommand: mkDerivation (args // {
    inherit name buildCommand stdenv;
  });

  foo = runCommand "foo" {} ''
    mkdir -p $out/bin
    echo 'echo ${fooContents}' > $out/bin/foo
    chmod a+rx $out/bin/foo
    ln -s ${shell} $out/bin/bash
  '';

  bar = runCommand "bar" {} ''
    mkdir -p $out/bin
    echo 'echo bar' > $out/bin/bar
    chmod a+rx $out/bin/bar
  '';

  bash = shell;
  bashInteractive = runCommand "bash" {} ''
    mkdir -p $out/bin
    ln -s ${shell} $out/bin/bash
  '';

  # ruby "interpreter" that outputs "$@"
  ruby = runCommand "ruby" {} ''
    mkdir -p $out/bin
    echo 'printf %s "$*"' > $out/bin/ruby
    chmod a+rx $out/bin/ruby
  '';

  inherit pkgs;
}; in pkgs
