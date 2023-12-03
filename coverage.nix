{ lib
, releaseTools
, nix
, stdenv
}:

let
  inherit (nix) version;

in

releaseTools.coverageAnalysis {
  name = "nix-coverage-${version}";

  inherit (nix)
    src
    buildInputs
    nativeBuildInputs
    propagatedBuildInputs
    configureFlags
    makeFlags
    installFlags
    doInstallCheck
    installCheckFlags
    installCheckTarget
    ;

  enableParallelBuilding = true;
}
