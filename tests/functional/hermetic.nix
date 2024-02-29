{ busybox
, seed
# If we want the final derivation output to have references to its
# dependencies. Some tests need/want this, other don't.
, withFinalRefs ? false
}:

with import ./config.nix;

let
  contentAddressedByDefault = builtins.getEnv "NIX_TESTS_CA_BY_DEFAULT" == "1";
  caArgs = if contentAddressedByDefault then {
    __contentAddressed = true;
    outputHashMode = "recursive";
    outputHashAlgo = "sha256";
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
    name = "hermetic-input-1";
    buildCommand = "echo hi-input1 seed=${toString seed}; echo FOO > $out";
  };

  input2 = mkDerivation {
    shell = busybox;
    name = "hermetic-input-2";
    buildCommand = "echo hi; echo BAR > $out";
  };

  input3 = mkDerivation {
    shell = busybox;
    name = "hermetic-input-3";
    buildCommand = ''
      echo hi-input3
      read x < ${input2}
      echo ${input2} $x BAZ > $out
    '';
  };

in

  mkDerivation {
    shell = busybox;
    name = "hermetic";
    passthru = { inherit input1 input2 input3; };
    buildCommand =
      ''
        read x < ${input1}
        read y < ${input3}
        echo ${if (builtins.trace withFinalRefs withFinalRefs) then "${input1} ${input3}" else ""} "$x $y" > $out
      '';
  }
