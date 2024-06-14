{ lib
, stdenv
, releaseTools
, fileset

, meson
, ninja
, pkg-config

, nix-util
, boost
, curl
, aws-sdk-cpp
, libseccomp
, nlohmann_json
, man
, sqlite

, busybox-sandbox-shell ? null

# Configuration Options

, versionSuffix ? ""
, officialRelease ? false

# Check test coverage of Nix. Probably want to use with at least
# one of `doCheck` or `doInstallCheck` enabled.
, withCoverageChecks ? false

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
  pname = "nix-store";
  inherit version;

  src = fileset.toSource {
    root = ./.;
    fileset = fileset.unions [
      ./meson.build
      ./meson.options
      ./linux/meson.build
      ./unix/meson.build
      ./windows/meson.build
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
    boost
    curl
    sqlite
  ] ++ lib.optional stdenv.hostPlatform.isLinux libseccomp
    # There have been issues building these dependencies
    ++ lib.optional (stdenv.hostPlatform == stdenv.buildPlatform && (stdenv.isLinux || stdenv.isDarwin))
      (aws-sdk-cpp.override {
        apis = ["s3" "transfer"];
        customMemoryManagement = false;
      })
  ;

  propagatedBuildInputs = [
    nix-util
    nlohmann_json
  ];

  disallowedReferences = [ boost ];

  preConfigure =
    # "Inline" .version so it's not a symlink, and includes the suffix
    ''
      echo ${version} > .version
    '';

  mesonFlags = [
    (lib.mesonEnable "seccomp-sandboxing" stdenv.hostPlatform.isLinux)
    (lib.mesonBool "embedded-sandbox-shell" stdenv.hostPlatform.isStatic)
  ] ++ lib.optionals stdenv.hostPlatform.isLinux [
    (lib.mesonOption "sandbox-shell" "${busybox-sandbox-shell}/bin/busybox")
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
      sed -i "$out/lib/pkgconfig/nix-store.pc" -e 's, ${lib.getLib boost}[^ ]*,,g'
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
