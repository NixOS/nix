{ lib
, stdenv

, meson
, ninja
, doxygen

# Configuration Options

, versionSuffix ? ""
}:

let
  inherit (lib) fileset;
in

stdenv.mkDerivation (finalAttrs: {
  pname = "nix-internal-api-docs";
  version = lib.fileContents ./.version + versionSuffix;

  src = fileset.toSource {
    root = ../..;
    fileset = let
      cpp = fileset.fileFilter (file: file.hasExt "cc" || file.hasExt "hh");
    in fileset.unions [
      ./meson.build
      ./doxygen.cfg.in
      # Source is not compiled, but still must be available for Doxygen
      # to gather comments.
      (cpp ../.)
      (cpp ../../tests/unit)
    ];
  };

  nativeBuildInputs = [
    meson
    ninja
    doxygen
  ];

  postUnpack = ''
    sourceRoot=$sourceRoot/src/internal-api-docs
  '';

  preConfigure =
    # "Inline" .version so it's not a symlink, and includes the suffix
    ''
      echo ${finalAttrs.version} > .version
    '';

  postInstall = ''
    mkdir -p ''${!outputDoc}/nix-support
    echo "doc internal-api-docs $out/share/doc/nix/internal-api/html" >> ''${!outputDoc}/nix-support/hydra-build-products
  '';

  enableParallelBuilding = true;

  strictDeps = true;

  meta = {
    platforms = lib.platforms.all;
  };
})
