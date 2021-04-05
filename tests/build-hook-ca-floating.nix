{ busybox }:

with import ./config.nix;

let

  mkDerivation = args:
    derivation ({
      inherit system;
      builder = busybox;
      args = ["sh" "-e" args.builder or (builtins.toFile "builder-${args.name}.sh" "if [ -e .attrs.sh ]; then source .attrs.sh; fi; eval \"$buildCommand\"")];
      outputHashMode = "recursive";
      outputHashAlgo = "sha256";
      __contentAddressed = true;
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
    requiredSystemFeatures = ["bar"];
  };

  input3 = mkDerivation {
    shell = busybox;
    name = "build-remote-input-3";
    buildCommand = ''
      read x < ${input2}
      echo $x BAZ > $out
    '';
    requiredSystemFeatures = ["baz"];
  };

in

  mkDerivation {
    shell = busybox;
    name = "build-remote";
    buildCommand =
      ''
        read x < ${input1}
        read y < ${input3}
        echo "$x $y" > $out
      '';
  }
