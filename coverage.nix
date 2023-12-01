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
    configureFlags
    nativeBuildInputs
    buildInputs
    #checkInputs
    ;

  enableParallelBuilding = true;

  dontInstall = false;

  doInstallCheck = true;
  installCheckTarget = "installcheck"; # work around buggy detection in stdenv

  lcovFilter = [ "*/boost/*" "*-tab.*" ];

  hardeningDisable = ["fortify"];

  NIX_CFLAGS_COMPILE = "-DCOVERAGE=1";
}
