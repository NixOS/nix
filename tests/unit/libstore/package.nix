{ lib
, buildPackages
, stdenv
, mkMesonDerivation
, releaseTools

, meson
, ninja
, pkg-config

, nix-store
, nix-store-c
, nix-store-test-support
, sqlite

, rapidcheck
, gtest
, runCommand

# Configuration Options

, version
, filesetToSource
}:

let
  inherit (lib) fileset;
in

mkMesonDerivation (finalAttrs: {
  pname = "nix-store-tests";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../../build-utils-meson
    ./build-utils-meson
    ../../../.version
    ./.version
    ./meson.build
    # ./meson.options
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
  ];

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
  ];

  buildInputs = [
    nix-store
    nix-store-c
    nix-store-test-support
    sqlite
    rapidcheck
    gtest
  ];

  preConfigure =
    # "Inline" .version so it's not a symlink, and includes the suffix.
    # Do the meson utils, without modification.
    ''
      chmod u+w ./.version
      echo ${version} > ../../../.version
    '';

  mesonFlags = [
  ];

  env = lib.optionalAttrs (stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux")) {
    LDFLAGS = "-fuse-ld=gold";
  };

  separateDebugInfo = !stdenv.hostPlatform.isStatic;

  hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";

  passthru = {
    tests = {
      run = let
        # Some data is shared with the functional tests: they create it,
        # we consume it.
        data = filesetToSource {
          root = ../..;
          fileset = lib.fileset.unions [
            ./data
            ../../functional/derivation
          ];
        };
      in runCommand "${finalAttrs.pname}-run" {
        meta.broken = !stdenv.hostPlatform.emulatorAvailable buildPackages;
      } (lib.optionalString stdenv.hostPlatform.isWindows ''
        export HOME="$PWD/home-dir"
        mkdir -p "$HOME"
      '' + ''
        export _NIX_TEST_UNIT_DATA=${data + "/unit/libstore/data"}
        ${stdenv.hostPlatform.emulator buildPackages} ${lib.getExe finalAttrs.finalPackage}
        touch $out
      '');
    };
  };

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
    mainProgram = finalAttrs.pname + stdenv.hostPlatform.extensions.executable;
  };

})
