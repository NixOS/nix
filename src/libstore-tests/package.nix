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
  openssl,

  rapidcheck,
  gtest,
  gbenchmark,
  runCommand,

  # Configuration Options

  version,
  filesetToSource,
  withBenchmarks ? false,
  withUnitTests ? true,
  withFuzzTargets ? false,
}:

let
  inherit (lib) fileset;
  doBenchmarks = withUnitTests && withBenchmarks;
in

assert lib.assertMsg (
  withUnitTests || withFuzzTargets
) "nix-store-tests requires unit tests or fuzz targets";

mkMesonExecutable (finalAttrs: {
  pname = "nix-store-tests";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../nix-meson-build-support
    ./nix-meson-build-support
    ./fuzz/harnesses
    ../../.version
    ./.version
    ./meson.options
    (fileset.fileFilter (file: file.name == "meson.build") ./.)
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
  ];

  buildInputs = [
    nix-store
  ]
  ++ lib.optionals withUnitTests [
    sqlite
    rapidcheck
    gtest
    nix-store-c
    nix-store-test-support
  ]
  ++ lib.optionals doBenchmarks [
    gbenchmark
  ];

  mesonFlags = [
    (lib.mesonBool "unit-tests" withUnitTests)
    (lib.mesonBool "benchmarks" doBenchmarks)
    (lib.mesonBool "fuzzers" withFuzzTargets)
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
            meta.broken = withUnitTests && !stdenv.hostPlatform.emulatorAvailable buildPackages;
            nativeBuildInputs = lib.optionals withUnitTests [
              writableTmpDirAsHomeHook
              openssl
            ];
          }
          (
            lib.optionalString withUnitTests ''
              export _NIX_TEST_UNIT_DATA=${data + "/src/libstore-tests/data"}
              export NIX_REMOTE=$HOME/store
              ${stdenv.hostPlatform.emulator buildPackages} ${lib.getExe finalAttrs.finalPackage}
            ''
            + lib.optionalString doBenchmarks ''
              ${stdenv.hostPlatform.emulator buildPackages} ${lib.getExe' finalAttrs.finalPackage "nix-store-benchmarks"}
            ''
            + ''
              ${if withUnitTests then "test -x" else "test ! -e"} \
                "${finalAttrs.finalPackage}/bin/${finalAttrs.pname}${stdenv.hostPlatform.extensions.executable}"
              for target in fuzz-parse-derivation fuzz-parse-derivation-experimental; do
                ${if withFuzzTargets then "test -x" else "test ! -e"} \
                  "${finalAttrs.finalPackage}/bin/$target${stdenv.hostPlatform.extensions.executable}"
              done
              touch $out
            ''
          );
    };
  };

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  }
  // lib.optionalAttrs withUnitTests {
    mainProgram = finalAttrs.pname + stdenv.hostPlatform.extensions.executable;
  };

})
