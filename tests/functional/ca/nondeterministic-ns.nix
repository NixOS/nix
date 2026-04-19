with import ./config.nix;

let
  mkCADerivation =
    args:
    mkDerivation (
      {
        __contentAddressed = true;
        outputHashMode = "recursive";
        outputHashAlgo = "sha256";
      }
      // args
    );
in

{
  singleOut = mkCADerivation {
    name = "time-single-out";
    buildCommand = ''
      mkdir $out
      date +%s.%N > $out/current-time
    '';
  };

  multiOut = mkCADerivation {
    name = "time-multi-out";
    outputs = [
      "out"
      "lib"
      "dev"
    ];
    buildCommand = ''
      mkdir $out
      date +%s.%N > $out/current-time
      echo out > $out/foo
      mkdir $lib
      date +%s.%N > $lib/current-time
      echo lib > $lib/foo
      mkdir $dev
      date +%s.%N > $lib/current-time
      echo dev > $dev/foo
    '';
  };
}
