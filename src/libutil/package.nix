{
  lib,
  stdenv,
  mkMesonLibrary,

  boost,
  brotli,
  libarchive,
  libblake3,
  libcpuid,
  libsodium,
  nlohmann_json,
  openssl,
  tbb_2021_11,
  # TODO: Remove this once we have https://github.com/NixOS/nixpkgs/pull/390458 backported
  tbb_2022_0 ? tbb_2021_11,
  # Configuration Options

  version,
}:

let
  inherit (lib) fileset;
in

mkMesonLibrary (finalAttrs: {
  pname = "nix-util";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../nix-meson-build-support
    ./nix-meson-build-support
    ../../.version
    ./.version
    ./widecharwidth
    ./meson.build
    ./meson.options
    ./linux/meson.build
    ./unix/meson.build
    ./windows/meson.build
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
  ];

  buildInputs = [
    brotli
    libblake3
    libsodium
    openssl
    tbb_2022_0
  ] ++ lib.optional stdenv.hostPlatform.isx86_64 libcpuid;

  propagatedBuildInputs = [
    boost
    libarchive
    nlohmann_json
  ];

  mesonFlags = [
    (lib.mesonEnable "cpuid" stdenv.hostPlatform.isx86_64)
  ];

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  };

})
