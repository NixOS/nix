{ lib
, mkMesonDerivation

, meson
, ninja
, doxygen

# Configuration Options

, versionSuffix ? ""
}:

let
  inherit (lib) fileset;
in

mkMesonDerivation (finalAttrs: {
  pname = "nix-internal-api-docs";
  version = lib.fileContents ./.version + versionSuffix;

  workDir = ./.;
  fileset = let
    cpp = fileset.fileFilter (file: file.hasExt "cc" || file.hasExt "hh");
  in fileset.unions [
    ./.version
    ../../.version
    ./meson.build
    ./doxygen.cfg.in
    # Source is not compiled, but still must be available for Doxygen
    # to gather comments.
    (cpp ../.)
  ];

  nativeBuildInputs = [
    meson
    ninja
    doxygen
  ];

  preConfigure =
    ''
      chmod u+w ./.version
      echo ${finalAttrs.version} > ./.version
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
