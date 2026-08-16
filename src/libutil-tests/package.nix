{
  lib,
  buildPackages,
  stdenv,
  mkMesonExecutable,

  nix-util,
  nix-util-c,
  nix-util-test-support,

  rapidcheck,
  gtest,
  zstd,
  runCommand,
  util-linux,

  # Configuration Options

  version,
  withUnitTests ? true,
  withFuzzTargets ? false,
}:

let
  inherit (lib) fileset;
in

assert lib.assertMsg (
  withUnitTests || withFuzzTargets
) "nix-util-tests requires unit tests or fuzz targets";

mkMesonExecutable (finalAttrs: {
  pname = "nix-util-tests";
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
    nix-util
  ]
  ++ lib.optionals withUnitTests [
    nix-util-c
    nix-util-test-support
    rapidcheck
    gtest
    zstd
  ]
  ++ lib.optionals (withUnitTests && stdenv.hostPlatform.isLinux) [
    util-linux
  ];

  mesonFlags = [
    (lib.mesonBool "unit-tests" withUnitTests)
    (lib.mesonBool "fuzzers" withFuzzTargets)
  ];

  passthru = {
    tests = {
      run =
        runCommand "${finalAttrs.pname}-run"
          {
            meta.broken = withUnitTests && !stdenv.hostPlatform.emulatorAvailable buildPackages;
          }
          (
            lib.optionalString (withUnitTests && stdenv.hostPlatform.isWindows) ''
              export HOME="$PWD/home-dir"
              mkdir -p "$HOME"
            ''
            + lib.optionalString withUnitTests ''
              export _NIX_TEST_UNIT_DATA=${./data}
              ${stdenv.hostPlatform.emulator buildPackages} ${lib.getExe finalAttrs.finalPackage}
            ''
            + ''
              ${if withUnitTests then "test -x" else "test ! -e"} \
                "${finalAttrs.finalPackage}/bin/${finalAttrs.pname}${stdenv.hostPlatform.extensions.executable}"
              for target in fuzz-parse-dump fuzz-parse-dump-case-hacked; do
                ${if withFuzzTargets then "test -x" else "test ! -e"} \
                  "${finalAttrs.finalPackage}/bin/$target${stdenv.hostPlatform.extensions.executable}"
              done
              touch $out
            ''
          );
    }
    //
      lib.optionalAttrs
        (
          withUnitTests && stdenv.hostPlatform.isLinux && stdenv.buildPlatform.canExecute stdenv.hostPlatform
        )
        {
          # Run the same tests with newer syscalls disabled via seccomp,
          # to exercise fallback paths (iterative openat for openat2,
          # /proc/self/fd for fchmodat2).
          run-without-new-syscalls =
            runCommand "${finalAttrs.pname}-run-without-new-syscalls"
              {
                meta.broken = !stdenv.hostPlatform.emulatorAvailable buildPackages;
                nativeBuildInputs = [ util-linux ];
              }
              ''
                export _NIX_TEST_UNIT_DATA=${./data}
                enosys \
                  --syscall openat2 \
                  --syscall fchmodat2 \
                  --syscall close_range \
                  -- ${lib.getExe finalAttrs.finalPackage}
                touch $out
              '';
        };
  };

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  }
  // lib.optionalAttrs withUnitTests {
    mainProgram = finalAttrs.pname + stdenv.hostPlatform.extensions.executable;
  };

})
