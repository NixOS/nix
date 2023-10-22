{ busybox, contentAddressed ? false }:

with import ./config.nix;

let

  caArgs = if contentAddressed then {
      outputHashMode = "recursive";
      outputHashAlgo = "sha256";
      __contentAddressed = true;
    } else {};

  mkDerivation = args:
    derivation ({
      inherit system;
      builder = busybox;
      args = ["sh" "-e" args.builder or (builtins.toFile "builder-${args.name}.sh" ''
        if [ -e "$NIX_ATTRS_SH_FILE" ]; then source $NIX_ATTRS_SH_FILE; fi;
        eval "$buildCommand"
      '')];
    } // removeAttrs args ["builder" "meta" "passthru"]
    // caArgs)
    // { meta = args.meta or {}; passthru = args.passthru or {}; };

  input1 = mkDerivation {
    shell = busybox;
    name = "build-remote-input-1";
    buildCommand = "echo hi-input1; echo FOO > $out";
    requiredSystemFeatures = ["foo"];
  };

  input2 = mkDerivation {
    shell = busybox;
    name = "build-remote-input-2";
    buildCommand = "echo hi; echo BAR > $out";
    requiredSystemFeatures = ["bar"];
  };

  input3 = mkDerivation {
    shell = busybox;
    name = "build-remote-input-3";
    buildCommand = ''
      echo hi-input3
      read x < ${input2}
      echo $x BAZ > $out
    '';
    requiredSystemFeatures = ["baz"];
  };

in

  mkDerivation {
    shell = busybox;
    name = "build-remote";
    passthru = { inherit input1 input2 input3; };
    buildCommand =
      ''
        read x < ${input1}
        read y < ${input3}
        echo "$x $y" > $out
      '';
  }
