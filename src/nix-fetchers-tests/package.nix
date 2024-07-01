{ lib
, stdenv
, mkMesonDerivation
, releaseTools

, meson
, ninja
, pkg-config

, nix-fetchers
, nix-store-test-support

, rapidcheck
, gtest
, runCommand

# Configuration Options

, versionSuffix ? ""
}:

let
  inherit (lib) fileset;

  version = lib.fileContents ./.version + versionSuffix;
in

mkMesonDerivation (finalAttrs: {
  pname = "nix-fetchers-tests";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../build-utils-meson
    ./build-utils-meson
    ../../.version
    ./.version
    ./meson.build
    # ./meson.options
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
    nix-fetchers
    nix-store-test-support
    rapidcheck
    gtest
  ];

  preConfigure =
    # "Inline" .version so it's not a symlink, and includes the suffix.
    # Do the meson utils, without modification.
    ''
      chmod u+w ./.version
      echo ${version} > ../../.version
    '';

  mesonFlags = [
  ];

  env = lib.optionalAttrs (stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux")) {
    LDFLAGS = "-fuse-ld=gold";
  };

  enableParallelBuilding = true;

  separateDebugInfo = !stdenv.hostPlatform.isStatic;

  strictDeps = true;

  hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";

  passthru = {
    tests = {
      run = runCommand "${finalAttrs.pname}-run" {
      } ''
        PATH="${lib.makeBinPath [ finalAttrs.finalPackage ]}:$PATH"
        export _NIX_TEST_UNIT_DATA=${./data}
        nix-fetchers-tests
        touch $out
      '';
    };
  };

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  };

})
