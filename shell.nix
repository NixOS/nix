{ useClang ? false }:

with import <nixpkgs> {};

(if useClang then clangStdenv else stdenv).mkDerivation {
  name = "nix";

  buildInputs =
    [ curl bison flex perl libxml2 libxslt
      bzip2 xz brotli
      pkgconfig sqlite libsodium boehmgc
      docbook5 docbook5_xsl
      autoconf-archive
      (aws-sdk-cpp.override {
        apis = ["s3"];
        customMemoryManagement = false;
      })
      autoreconfHook
      perlPackages.DBDSQLite
    ];

  configureFlags =
    [ "--disable-init-state"
      "--enable-gc"
      "--with-dbi=${perlPackages.DBI}/${perl.libPrefix}"
      "--with-dbd-sqlite=${perlPackages.DBDSQLite}/${perl.libPrefix}"
    ];

  enableParallelBuilding = true;

  installFlags = "sysconfdir=$(out)/etc";

  shellHook =
    ''
      configureFlags+=" --prefix=$(pwd)/inst"
    '';
}
