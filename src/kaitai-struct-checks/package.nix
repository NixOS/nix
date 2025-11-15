# Run with: nix build .#nix-kaitai-struct-checks
# or: `nix develop .#nix-kaitai-struct-checks` to enter a dev shell
{
  lib,
  mkMesonDerivation,
  gtest,
  meson,
  ninja,
  pkg-config,
  kaitai-struct-compiler,
  fetchzip,
  kaitai-struct-cpp-stl-runtime,
  # Configuration Options
  version,
}:
let
  inherit (lib) fileset;
in
mkMesonDerivation (finalAttrs: {
  pname = "nix-kaitai-struct-checks";
  inherit version;

  workDir = ./.;
  fileset = lib.fileset.unions [
    ../../nix-meson-build-support
    ./nix-meson-build-support
    ./.version
    ../../.version
    ../../doc/manual/source/protocols/nix-archive/nar.ksy
    ./nars
    ../../src/libutil-tests/data
    ./meson.build
    ./nar.ksy
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
  ];

  outputs = [ "out" ];

  buildInputs = [
    gtest
    kaitai-struct-cpp-stl-runtime
  ];

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    # This can go away when we bump up to 25.11
    (kaitai-struct-compiler.overrideAttrs (finalAttrs: {
      version = "0.11";
      src = fetchzip {
        url = "https://github.com/kaitai-io/kaitai_struct_compiler/releases/download/${version}/kaitai-struct-compiler-${version}.zip";
        sha256 = "sha256-j9TEilijqgIiD0GbJfGKkU1FLio9aTopIi1v8QT1b+A=";
      };
    }))
  ];

  doCheck = true;

  mesonCheckFlags = [ "--print-errorlogs" ];

  postInstall = ''
    touch $out
  '';

  meta = {
    platforms = lib.platforms.all;
  };
})
