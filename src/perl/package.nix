{ lib
, stdenv
, perl
, perlPackages
, meson
, ninja
, pkg-config
, nix-store
, darwin
, versionSuffix ? ""
}:

let
  inherit (lib) fileset;
in

perl.pkgs.toPerlModule (stdenv.mkDerivation (finalAttrs: {
  pname = "nix-perl";
  version = lib.fileContents ./.version + versionSuffix;

  src = fileset.toSource {
    root = ./.;
    fileset = fileset.unions ([
      ./MANIFEST
      ./lib
      ./meson.build
      ./meson.options
    ] ++ lib.optionals finalAttrs.doCheck [
      ./.yath.rc.in
      ./t
    ]);
  };

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    perl
  ];

  buildInputs = [
    nix-store
  ];

  # `perlPackages.Test2Harness` is marked broken for Darwin
  doCheck = !stdenv.isDarwin;

  nativeCheckInputs = [
    perlPackages.Test2Harness
  ];

  preConfigure =
    # "Inline" .version so its not a symlink, and includes the suffix
    ''
      echo ${finalAttrs.version} > .version
    '';

  mesonFlags = [
    (lib.mesonOption "dbi_path" "${perlPackages.DBI}/${perl.libPrefix}")
    (lib.mesonOption "dbd_sqlite_path" "${perlPackages.DBDSQLite}/${perl.libPrefix}")
    (lib.mesonEnable "tests" finalAttrs.doCheck)
  ];

  mesonCheckFlags = [
    "--print-errorlogs"
  ];

  enableParallelBuilding = true;
}))
