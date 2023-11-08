{ busybox }:

with import ./config.nix;

let

  mkDerivation = args:
    derivation ({
      inherit system;
      builder = busybox;
      args = ["sh" "-e" args.builder or (builtins.toFile "builder-${args.name}.sh" ''
        if [ -e "$NIX_ATTRS_SH_FILE" ]; then source $NIX_ATTRS_SH_FILE; fi;
        eval "$buildCommand"
      '')];
      outputHashMode = "recursive";
      outputHashAlgo = "sha256";
    } // removeAttrs args ["builder" "meta" "passthru"])
    // { meta = args.meta or {}; passthru = args.passthru or {}; };

  input1 = mkDerivation {
    shell = busybox;
    name = "build-remote-input-1";
    buildCommand = "echo hi-input1; echo FOO > $out";
    requiredSystemFeatures = ["foo"];
    outputHash = "sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc=";
  };

  input2 = mkDerivation {
    shell = busybox;
    name = "build-remote-input-2";
    buildCommand = "echo hi; echo BAR > $out";
    requiredSystemFeatures = ["bar"];
    outputHash = "sha256-XArauVH91AVwP9hBBQNlkX9ccuPpSYx9o0zeIHb6e+Q=";
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
    outputHash = "sha256-daKAcPp/+BYMQsVi/YYMlCKoNAxCNDsaivwSHgQqD2s=";
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
    outputHash = "sha256-5SxbkUw6xe2l9TE1uwCvTtTDysD1vhRor38OtDF0LqQ=";
  }
