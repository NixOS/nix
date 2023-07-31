with import ./config.nix;

let mkCADerivation = args: mkDerivation ({
    __contentAddressed = true;
    outputHashMode = "recursive";
    outputHashAlgo = "sha256";
} // args);
in

rec {
  currentTime = mkCADerivation {
    name = "current-time";
    buildCommand = ''
      mkdir $out
      echo $(date) > $out/current-time
    '';
  };
  dep = seed: mkCADerivation {
    name = "dep";
    inherit seed;
    buildCommand = ''
      echo ${currentTime} > $out
    '';
  };
  dep1 = dep 1;
  dep2 = dep 2;
  toplevel = mkCADerivation {
    name = "toplevel";
    buildCommand = ''
      test ${dep1} == ${dep2}
      touch $out
    '';
  };
}

