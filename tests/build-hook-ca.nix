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
    } // removeAttrs args ["builder" "meta"])
    // { meta = args.meta or {}; };

  input1 = mkDerivation {
    shell = busybox;
    name = "build-remote-input-1";
    buildCommand = "echo FOO > $out";
    requiredSystemFeatures = ["foo"];
    outputHash = "sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc=";
  };

  input2 = mkDerivation {
    shell = busybox;
    name = "build-remote-input-2";
    buildCommand = "echo BAR > $out";
    requiredSystemFeatures = ["bar"];
    outputHash = "sha256-XArauVH91AVwP9hBBQNlkX9ccuPpSYx9o0zeIHb6e+Q=";
  };

in

  mkDerivation {
    shell = busybox;
    name = "build-remote";
    buildCommand =
      ''
        read x < ${input1}
        read y < ${input2}
        echo "$x $y" > $out
      '';
    outputHash = "sha256-3YGhlOfbGUm9hiPn2teXXTT8M1NEpDFvfXkxMaJRld0=";
  }
