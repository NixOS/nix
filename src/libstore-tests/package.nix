{
  lib,
  buildPackages,
  stdenv,
  mkMesonExecutable,
  writableTmpDirAsHomeHook,

  nix-store,
  nix-store-c,
  nix-store-test-support,
  sqlite,

  rapidcheck,
  gtest,
  gbenchmark,
  runCommand,

  # Configuration Options

  version,
  filesetToSource,
  withBenchmarks ? false,
}:

let
  inherit (lib) fileset;
in

mkMesonExecutable (finalAttrs: {
  pname = "nix-store-tests";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../nix-meson-build-support
    ./nix-meson-build-support
    ../../.version
    ./.version
    ./meson.build
    ./meson.options
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
  ];

  buildInputs = [
    sqlite
    rapidcheck
    gtest
    nix-store
    nix-store-c
    nix-store-test-support
  ]
  ++ lib.optionals withBenchmarks [
    gbenchmark
  ];

  mesonFlags = [
    (lib.mesonBool "benchmarks" withBenchmarks)
  ];

  passthru = {
    tests = {
      run =
        let
          # Some data is shared with the functional tests: they create it,
          # we consume it.
          data = filesetToSource {
            root = ../..;
            fileset = lib.fileset.unions [
              ./data
              ../../tests/functional/derivation
            ];
          };
        in
        runCommand "${finalAttrs.pname}-run"
          {
            meta.broken = !stdenv.hostPlatform.emulatorAvailable buildPackages;
            buildInputs = [ writableTmpDirAsHomeHook ];
          }
          (
            ''
              export _NIX_TEST_UNIT_DATA=${data + "/src/libstore-tests/data"}
              export NIX_REMOTE=$HOME/store
              ${stdenv.hostPlatform.emulator buildPackages} ${lib.getExe finalAttrs.finalPackage}
            ''
            + lib.optionalString withBenchmarks ''
              ${stdenv.hostPlatform.emulator buildPackages} ${lib.getExe' finalAttrs.finalPackage "nix-store-benchmarks"}
            ''
            + ''
              touch $out
            ''
          );
    };
  };

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
    mainProgram = finalAttrs.pname + stdenv.hostPlatform.extensions.executable;
  };

})
