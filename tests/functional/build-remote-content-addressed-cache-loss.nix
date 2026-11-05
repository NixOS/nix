{ busybox }:

let
  inherit (import ./config.nix) system;
  mk = name: script: derivation {
    inherit name system;
    builder = busybox;
    args = [ "sh" "-eu" "-c" script ];
    __contentAddressed = true;
    outputHashMode = "recursive";
    outputHashAlgo = "sha256";
    requiredSystemFeatures = [ "cache-loss" ];
  };
  a = mk "cache-loss-a" ''
    echo CACHE-LOSS-A-BUILT >&2
    echo input > "$out"
  '';
  b = mk "cache-loss-b" ''
    read value < ${a}
    echo "$value first" > "$out"
  '';
  c = mk "cache-loss-c" ''
    read value < ${a}
    echo "$value second" > "$out"
  '';
in
{ inherit a b c; }
