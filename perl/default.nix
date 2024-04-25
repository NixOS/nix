{ lib
, fileset
, stdenv
, perl
, perlPackages
, meson
, ninja
, pkg-config
, nix
, curl
, bzip2
, xz
, boost
, libsodium
, darwin
}:

perl.pkgs.toPerlModule (stdenv.mkDerivation (finalAttrs: {
  name = "nix-perl-${nix.version}";

  src = fileset.toSource {
    root = ./.;
    fileset = fileset.unions ([
      ./MANIFEST
      ./lib
      ./meson.build
      ./meson_options.txt
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
    nix
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

  mesonFlags = [
    (lib.mesonOption "version" (builtins.readFile ../.version))
    (lib.mesonOption "dbi_path" "${perlPackages.DBI}/${perl.libPrefix}")
    (lib.mesonOption "dbd_sqlite_path" "${perlPackages.DBDSQLite}/${perl.libPrefix}")
    (lib.mesonEnable "tests" finalAttrs.doCheck)
  ];

  mesonCheckFlags = [
    "--print-errorlogs"
  ];

  enableParallelBuilding = true;
}))
