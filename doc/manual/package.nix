{ lib
, mkMesonDerivation

, meson
, ninja
, lowdown
, mdbook
, mdbook-linkcheck
, jq
, nix-ng

# Configuration Options

, version
}:

let
  inherit (lib) fileset;
in

mkMesonDerivation (finalAttrs: {
  pname = "nix-internal-api-docs";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ./.version
    ./meson.build
    ../../.version
    ../../doc/manual
  ];

  # Hack for sake of the dev shell
  passthru.baseNativeBuildInputs = [
    meson
    ninja
    (lib.getBin lowdown)
    mdbook
    mdbook-linkcheck
    jq
  ];

  nativeBuildInputs = finalAttrs.passthru.baseNativeBuildInputs ++ [
    nix-ng
  ];

  preConfigure =
    ''
      chmod u+w ./.version
      echo ${finalAttrs.version} > ./.version
    '';

  postInstall = ''
    mkdir -p ''${!outputDoc}/nix-support
    echo "doc manual ''${!outputDoc}/share/doc/nix/manual" >> ''${!outputDoc}/nix-support/hydra-build-products
  '';

  meta = {
    platforms = lib.platforms.all;
  };
})
