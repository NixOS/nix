{ lib
, stdenv
, releaseTools
, fileset

, meson
, ninja
, doxygen

# Configuration Options

, versionSuffix ? ""
}:

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

  enableParallelBuilding = true;

  strictDeps = true;

  meta = {
    platforms = lib.platforms.all;
  };
})
