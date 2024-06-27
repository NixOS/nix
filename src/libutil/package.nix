{ lib
, stdenv
, mkMesonDerivation
, releaseTools

, meson
, ninja
, pkg-config

, boost
, brotli
, libarchive
, libcpuid
, libsodium
, nlohmann_json
, openssl

# Configuration Options

, versionSuffix ? ""

# Check test coverage of Nix. Probably want to use with at least
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

mkMesonDerivation (finalAttrs: {
  pname = "nix-util";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../.version
    ./.version
    ./meson.build
    ./meson.options
    ./linux/meson.build
    ./unix/meson.build
    ./windows/meson.build
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
  ];

  outputs = [ "out" "dev" ];

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
  ];

  buildInputs = [
    brotli
    libsodium
    openssl
  ] ++ lib.optional stdenv.hostPlatform.isx86_64 libcpuid
  ;

  propagatedBuildInputs = [
    boost
    libarchive
    nlohmann_json
  ];

  preConfigure =
    # TODO: change release process to add `pre` in `.version`, remove it before tagging, and restore after.
    ''
      chmod u+w ./.version
      echo ${version} > ../../.version
    '';

  mesonFlags = [
    (lib.mesonEnable "cpuid" stdenv.hostPlatform.isx86_64)
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
