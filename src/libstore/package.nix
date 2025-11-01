{
  lib,
  stdenv,
  mkMesonLibrary,

  unixtools,
  darwin,

  nix-util,
  boost,
  curl,
  aws-crt-cpp,
  libseccomp,
  nlohmann_json,
  sqlite,

  busybox-sandbox-shell ? null,

  # Configuration Options

  version,

  embeddedSandboxShell ? stdenv.hostPlatform.isStatic,

  withAWS ?
    # Default is this way because there have been issues building this dependency
    stdenv.hostPlatform == stdenv.buildPlatform && (stdenv.isLinux || stdenv.isDarwin),
}:

let
  inherit (lib) fileset;
in

mkMesonLibrary (finalAttrs: {
  pname = "nix-store";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../nix-meson-build-support
    ./nix-meson-build-support
    ../../.version
    ./.version
    ./meson.build
    ./meson.options
    ./include/nix/store/meson.build
    ./linux/meson.build
    ./linux/include/nix/store/meson.build
    ./unix/meson.build
    ./unix/include/nix/store/meson.build
    ./windows/meson.build
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
    (fileset.fileFilter (file: file.hasExt "sb") ./.)
    (fileset.fileFilter (file: file.hasExt "md") ./.)
    (fileset.fileFilter (file: file.hasExt "sql") ./.)
  ];

  nativeBuildInputs = lib.optional embeddedSandboxShell unixtools.hexdump;

  buildInputs = [
    boost
    curl
    sqlite
  ]
  ++ lib.optional stdenv.hostPlatform.isLinux libseccomp
  ++ lib.optional withAWS aws-crt-cpp;

  propagatedBuildInputs = [
    nix-util
    nlohmann_json
  ];

  mesonFlags = [
    (lib.mesonEnable "seccomp-sandboxing" stdenv.hostPlatform.isLinux)
    (lib.mesonBool "embedded-sandbox-shell" embeddedSandboxShell)
    (lib.mesonEnable "s3-aws-auth" withAWS)
  ]
  ++ lib.optionals stdenv.hostPlatform.isLinux [
    (lib.mesonOption "sandbox-shell" "${busybox-sandbox-shell}/bin/busybox")
  ];

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  };

})
