with import <nixpkgs> {};

stdenv.mkDerivation {
  name = "nix";

  buildInputs =
    [ curl bison flex perl libxml2 libxslt bzip2 xz
      pkgconfig sqlite libsodium boehmgc
      docbook5 docbook5_xsl
      autoconf-archive
      libseccomp
      (aws-sdk-cpp.override {
        apis = ["s3"];
        customMemoryManagement = false;
      })
      autoreconfHook
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
