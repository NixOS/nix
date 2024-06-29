{ lib
, stdenv
, releaseTools

, meson
, ninja
, pkg-config

, nix-util
, nix-store
, nlohmann_json
, libgit2
, man

# Configuration Options

, versionSuffix ? ""

# Check test coverage of Nix. Probably want to use with with at least
# one of `doCheck` or `doInstallCheck` enabled.
, withCoverageChecks ? false

}:

let
  inherit (lib) fileset;

  version = lib.fileContents ./.version + versionSuffix;

  mkDerivation =
    if withCoverageChecks
    then
      # TODO support `finalAttrs` args function in
      # `releaseTools.coverageAnalysis`.
      argsFun:
         releaseTools.coverageAnalysis (let args = argsFun args; in args)
    else stdenv.mkDerivation;
in

mkDerivation (finalAttrs: {
  pname = "nix-fetchers";
  inherit version;

  src = fileset.toSource {
    root = ./.;
    fileset = fileset.unions [
      ./meson.build
      (fileset.fileFilter (file: file.hasExt "cc") ./.)
      (fileset.fileFilter (file: file.hasExt "hh") ./.)
    ];
  };

  outputs = [ "out" "dev" ];

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
  ];

  buildInputs = [
    libgit2
  ];

  propagatedBuildInputs = [
    nix-store
    nix-util
    nlohmann_json
  ];

  preConfigure =
    # "Inline" .version so its not a symlink, and includes the suffix
    ''
      echo ${version} > .version
    '';

  env = lib.optionalAttrs (stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux")) {
    LDFLAGS = "-fuse-ld=gold";
  };

  enableParallelBuilding = true;

  postInstall =
    # Remove absolute path to boost libs
    ''
    '';

  separateDebugInfo = !stdenv.hostPlatform.isStatic;

  # TODO `releaseTools.coverageAnalysis` in Nixpkgs needs to be updated
  # to work with `strictDeps`.
  strictDeps = !withCoverageChecks;

  hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  };

} // lib.optionalAttrs withCoverageChecks {
  lcovFilter = [ "*-tab.*" ];

  hardeningDisable = ["fortify"];
})
