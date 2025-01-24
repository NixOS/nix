{
  lib,
  stdenv,
  mkMesonDerivation,
  releaseTools,

  meson,
  ninja,
  pkg-config,

  nix-store-test-support,
  nix-expr,

  rapidcheck,

  # Configuration Options

  version,
}:

let
  inherit (lib) fileset;
in

mkMesonDerivation (finalAttrs: {
  pname = "nix-util-test-support";
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

  outputs = [
    "out"
    "dev"
  ];

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
  ];

  propagatedBuildInputs = [
    nix-store-test-support
    nix-expr
    rapidcheck
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

  env =
    lib.optionalAttrs
      (stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux"))
      {
        LDFLAGS = "-fuse-ld=gold";
      };

  separateDebugInfo = !stdenv.hostPlatform.isStatic;

  hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  };

})
