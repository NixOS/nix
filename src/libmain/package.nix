{
  lib,
  mkMesonLibrary,

  openssl,

  nix-util,
  nix-store,
  nix-expr,

  # Configuration Options

  version,
}:

let
  inherit (lib) fileset;
in

mkMesonLibrary (finalAttrs: {
  pname = "nix-main";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../nix-meson-build-support
    ./nix-meson-build-support
    ../../.version
    ./.version
    ./meson.build
    ./include/nix/main/meson.build
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
  ];

  propagatedBuildInputs = [
    # FIXME: This is only here for the NIX_USE_BOEHMGC macro dependency
    #        Removing nix-expr will make the build more concurrent and is
    #        architecturally nice, perhaps.
    nix-expr
    nix-util
    nix-store
    openssl
  ];

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  };

})
