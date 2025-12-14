{
  lib,
  stdenv,
  mkMesonDerivation,
  pkg-config,
  perl,
  perlPackages,
  nix-store,
  version,
  curl,
  bzip2,
  libsodium,
}:

let
  inherit (lib) fileset;
in

perl.pkgs.toPerlModule (
  mkMesonDerivation (finalAttrs: {
    pname = "nix-perl";
    inherit version;

    workDir = ./.;
    fileset = fileset.unions (
      [
        ./.version
        ../../.version
        ./MANIFEST
        ./lib
        ./meson.build
        ./meson.options
      ]
      ++ lib.optionals finalAttrs.finalPackage.doCheck [
        ./.yath.rc.in
        ./t
      ]
    );

    nativeBuildInputs = [
      pkg-config
      perl
      curl
    ];

    buildInputs = [
      nix-store
      bzip2
      libsodium
      perlPackages.DBI
      perlPackages.DBDSQLite
    ];

    # `perlPackages.Test2Harness` is marked broken for Darwin
    doCheck = !stdenv.isDarwin;

    nativeCheckInputs = [
      perlPackages.Test2Harness
    ];

    preConfigure =
      # "Inline" .version so its not a symlink, and includes the suffix
      ''
        chmod u+w .version
        echo ${finalAttrs.version} > .version
      '';

    mesonFlags = [
      (lib.mesonEnable "tests" finalAttrs.finalPackage.doCheck)
    ];

    mesonCheckFlags = [
      "--print-errorlogs"
    ];

    strictDeps = false;

    meta = {
      platforms = lib.platforms.unix;
    };
  })
)
