{ lib
, stdenv
, mkMesonLibrary

, nix-util
, nix-util-c

, rapidcheck

# Configuration Options

, version
}:

let
  inherit (lib) fileset;
in

mkMesonLibrary (finalAttrs: {
  pname = "nix-util-test-support";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../nix-meson-build-support
    ./nix-meson-build-support
    ../../.version
    ./.version
    ./meson.build
    # ./meson.options
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
  ];

  propagatedBuildInputs = [
    nix-util
    nix-util-c
    rapidcheck
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

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  };

})