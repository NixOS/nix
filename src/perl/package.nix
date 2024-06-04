{ lib
, fileset
, stdenv
, perl
, perlPackages
, meson
, ninja
, pkg-config
, nix-store
, curl
, bzip2
, xz
, boost
, libsodium
, darwin
, versionSuffix ? ""
}:

let
  version = lib.fileContents ./.version + versionSuffix;
in

perl.pkgs.toPerlModule (stdenv.mkDerivation (finalAttrs: {
  pname = "nix-perl";
  inherit version;

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
  ];

  buildInputs = [
    nix-store
    curl
    bzip2
    xz
    perl
    boost
  ]
  ++ lib.optional (stdenv.isLinux || stdenv.isDarwin) libsodium
  ++ lib.optional stdenv.isDarwin darwin.apple_sdk.frameworks.Security;

  # `perlPackages.Test2Harness` is marked broken for Darwin
  doCheck = !stdenv.isDarwin;

  nativeCheckInputs = [
    perlPackages.Test2Harness
  ];

  preConfigure =
    # "Inline" .version so its not a symlink, and includes the suffix
    ''
      echo ${version} > .version
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
