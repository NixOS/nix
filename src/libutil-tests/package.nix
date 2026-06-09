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
}:

let
  inherit (lib) fileset;
in

mkMesonExecutable (finalAttrs: {
  pname = "nix-util-tests";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../nix-meson-build-support
    ./nix-meson-build-support
    ../../.version
    ./.version
    ./meson.build
    ./unix/meson.build
    # ./meson.options
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
  ];

  buildInputs = [
    nix-util
    nix-util-c
    nix-util-test-support
    rapidcheck
    gtest
    zstd
  ]
  ++ lib.optionals stdenv.hostPlatform.isLinux [
    util-linux
  ];

  mesonFlags = [
  ];

  passthru = {
    tests = {
      run =
        runCommand "${finalAttrs.pname}-run"
          {
            meta.broken = !stdenv.hostPlatform.emulatorAvailable buildPackages;
          }
          (
            lib.optionalString stdenv.hostPlatform.isWindows ''
              export HOME="$PWD/home-dir"
              mkdir -p "$HOME"
            ''
            + ''
              export _NIX_TEST_UNIT_DATA=${./data}
              ${stdenv.hostPlatform.emulator buildPackages} ${lib.getExe finalAttrs.finalPackage}
              touch $out
            ''
          );
    }
    //
      lib.optionalAttrs
        (stdenv.hostPlatform.isLinux && stdenv.buildPlatform.canExecute stdenv.hostPlatform)
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
                  -- ${lib.getExe finalAttrs.finalPackage}
                touch $out
              '';
        };
  };

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
    mainProgram = finalAttrs.pname + stdenv.hostPlatform.extensions.executable;
  };

})
