# Build Nix 1.11.x from the 1.11-maintenance branch for compatibility testing.
# Not directly exposed, for testing only.
# See .#checks.x86_64-linux.installTests.against-1_11
{
  lib,
  stdenv,
  fetchFromGitHub,
  autoreconfHook,
  autoconf-archive,
  bison,
  flex,
  curl,
  perl,
  perlPackages,
  bzip2,
  xz,
  openssl,
  pkg-config,
  sqlite,
  boehmgc,
  libseccomp,
  libsodium,
}:

stdenv.mkDerivation {
  pname = "nix";
  version = "1.11.16";

  src = fetchFromGitHub {
    owner = "NixOS";
    repo = "nix";
    rev = "c6e15c43222cbec54969322b863c9eb426f58499"; # 1.11.16
    hash = "sha256-hGZ/Dvm2s6QIQsyDLlxDetC7qGExxDwMkWYk5E2+oj8=";
  };

  nativeBuildInputs = [
    autoreconfHook
    autoconf-archive
    bison
    flex
    pkg-config
  ];

  buildInputs = [
    curl
    perl
    bzip2
    xz
    openssl
    sqlite
    boehmgc
  ]
  ++ lib.optional stdenv.hostPlatform.isLinux libseccomp
  ++ lib.optional (stdenv.hostPlatform.isLinux || stdenv.hostPlatform.isDarwin) libsodium;

  configureFlags = [
    "--disable-init-state"
    "--with-dbi=${perlPackages.DBI}/${perl.libPrefix}"
    "--with-dbd-sqlite=${perlPackages.DBDSQLite}/${perl.libPrefix}"
    "--with-www-curl=${perlPackages.WWWCurl}/${perl.libPrefix}"
    "--enable-gc"
    "--sysconfdir=${placeholder "out"}/etc"
  ];

  makeFlags = [
    "profiledir=$(out)/etc/profile.d"
  ];

  installFlags = [
    "sysconfdir=$(out)/etc"
  ];

  enableParallelBuilding = true;

  patches = [
    # Support NIX_DAEMON_SOCKET_PATH for compatibility tests
    ./nix-1.11-socket-path.patch
  ];

  postPatch = ''
    # -Wno-unneeded-internal-declaration and -Wno-deprecated-register are clang-only
    sed -i 's/-Wno-unneeded-internal-declaration//' local.mk
    sed -i 's/-Wno-deprecated-register//' src/libexpr/local.mk
    # Missing <cstdint> for uint32_t/uint64_t (implicit headers changed in GCC 11+)
    sed -i '1i #include <cstdint>' src/libutil/serialise.hh src/libexpr/attr-set.hh
    # Don't build docs (missing docbook dependencies)
    sed -i '/doc\/manual/d' Makefile
  '';
  # Shift-count-overflow warnings in serialise.hh (32-bit time_t code paths)
  env.NIX_CFLAGS_COMPILE = "-Wno-error=shift-count-overflow";
  # bdw-gc C++ support (GC_throw_bad_alloc) is in separate library
  env.NIX_LDFLAGS = "-lgccpp";

  # Tests require a functional /nix/store
  doCheck = false;
  doInstallCheck = false;

  meta = {
    description = "Nix 1.11.x for compatibility testing";
    homepage = "https://nixos.org/nix/";
    license = lib.licenses.lgpl21Plus;
    platforms = lib.platforms.unix;
  };
}
