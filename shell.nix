{ useClang ? false }:

with import <nixpkgs> {};

with import ./release-common.nix { inherit pkgs; };

(if useClang then clangStdenv else stdenv).mkDerivation {
  name = "nix";

  buildInputs =
    [ curl bison flex libxml2 libxslt
      bzip2 xz brotli
      pkgconfig sqlite libsodium boehmgc
      docbook5 docbook5_xsl
      autoconf-archive
      (aws-sdk-cpp.override {
        apis = ["s3"];
        customMemoryManagement = false;
      })
      autoreconfHook

      # For nix-perl
      perl
      perlPackages.DBDSQLite
    ];

  inherit configureFlags;

  enableParallelBuilding = true;

  installFlags = "sysconfdir=$(out)/etc";

  shellHook =
    ''
      export prefix=$(pwd)/inst
      configureFlags+=" --prefix=$prefix"
      PKG_CONFIG_PATH=$prefix/lib/pkgconfig:$PKG_CONFIG_PATH
      PATH=$prefix/bin:$PATH
    '';
}
