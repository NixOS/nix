{ lib
, stdenv
, releaseTools

, meson
, ninja
, pkg-config

, nix-util
, nix-store
, nix-fetchers
, boost
, boehmgc
, nlohmann_json

# Configuration Options

, versionSuffix ? ""

# Check test coverage of Nix. Probably want to use with at least
# one of `doCheck` or `doInstallCheck` enabled.
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
  pname = "nix-expr";
  inherit version;

  src = fileset.toSource {
    root = ./.;
    fileset = fileset.unions [
      ./meson.build
      ./meson.options
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
    boost
  ];

  propagatedBuildInputs = [
    nix-util
    nix-store
    nix-fetchers
    nlohmann_json
  ] ++ lib.optional enableGC boehmgc;

  disallowedReferences = [ boost ];

  preConfigure =
    # "Inline" .version so it's not a symlink, and includes the suffix
    ''
      echo ${version} > .version
    '';

  mesonFlags = [
    (lib.mesonFeature "gc" enableGC)
  ];

  env = {
    # Needed for Meson to find Boost.
    # https://github.com/NixOS/nixpkgs/issues/86131.
    BOOST_INCLUDEDIR = "${lib.getDev boost}/include";
    BOOST_LIBRARYDIR = "${lib.getLib boost}/lib";
  } // lib.optionalAttrs (stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux")) {
    LDFLAGS = "-fuse-ld=gold";
  };

  enableParallelBuilding = true;

  postInstall =
    # Remove absolute path to boost libs that ends up in `Libs.private`
    # by default, and would clash with out `disallowedReferences`. Part
    # of the https://github.com/NixOS/nixpkgs/issues/45462 workaround.
    ''
      sed -i "$out/lib/pkgconfig/nix-expr.pc" -e 's, ${lib.getLib boost}[^ ]*,,g'
    '';

  separateDebugInfo = !stdenv.hostPlatform.isStatic;

  # TODO Always true after https://github.com/NixOS/nixpkgs/issues/318564
  strictDeps = !withCoverageChecks;

  hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  };

} // lib.optionalAttrs withCoverageChecks {
  lcovFilter = [ "*/boost/*" "*-tab.*" ];

  hardeningDisable = [ "fortify" ];
})
