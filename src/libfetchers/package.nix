{ lib
, stdenv
, releaseTools
, fileset

, meson
, ninja
, pkg-config

, nix-util
, nix-store
, nlohmann_json
, libgit2
, man

# Configuration Options
#:
# This probably seems like too many degrees of freedom, but it
# faithfully reflects how the underlying configure + make build system
# work. The top-level flake.nix will choose useful combinations of these
# options to CI.

, pname ? "nix-fetchers"

, versionSuffix ? ""
, officialRelease ? false

# Check test coverage of Nix. Probably want to use with with at least
# one of `doCHeck` or `doInstallCheck` enabled.
, withCoverageChecks ? false

# Whether to use garbage collection for the Nix language evaluator.
#
# If it is disabled, we just leak memory, but this is not as bad as it
# sounds so long as evaluation just takes places within short-lived
# processes. (When the process exits, the memory is reclaimed; it is
# only leaked *within* the process.)
#
# Temporarily disabled on Windows because the `GC_throw_bad_alloc`
# symbol is missing during linking.
, enableGC ? !stdenv.hostPlatform.isWindows

# Avoid setting things that would interfere with a functioning devShell
, forDevShell ? false
}:

let
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
  inherit pname version;

  src = fileset.toSource {
    root = ./.;
    fileset = fileset.unions [
      ./meson.build
      # ./meson.options
      (fileset.fileFilter (file: file.hasExt "cc") ./.)
      (fileset.fileFilter (file: file.hasExt "hh") ./.)
      (fileset.fileFilter (file: file.hasExt "sb") ./.)
      (fileset.fileFilter (file: file.hasExt "md") ./.)
      (fileset.fileFilter (file: file.hasExt "sql") ./.)
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

  mesonFlags = [ ];

  env = {
    # Needed for Meson to find Boost.
    # https://github.com/NixOS/nixpkgs/issues/86131.
  } // lib.optionalAttrs (stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux")) {
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
