{ busybox }:

with import ./config.nix;

let

  mkDerivation = args:
    derivation ({
      inherit system;
      builder = busybox;
      args = ["sh" "-e" args.builder or (builtins.toFile "builder-${args.name}.sh" "if [ -e .attrs.sh ]; then source .attrs.sh; fi; eval \"$buildCommand\"")];
    } // removeAttrs args ["builder" "meta"])
    // { meta = args.meta or {}; };

  input1 = mkDerivation {
    shell = busybox;
    name = "build-remote-input-1";
    buildCommand = "echo FOO > $out";
    requiredSystemFeatures = ["foo"];
  };

  input2 = mkDerivation {
    shell = busybox;
    name = "build-remote-input-2";
    buildCommand = "echo BAR > $out";
  };

in

  mkDerivation {
    shell = busybox;
    name = "build-remote";
    buildCommand =
      ''
        read x < ${input1}
        read y < ${input2}
        echo $x$y > $out
      '';
  }
