{ useClang ? false }:

with import <nixpkgs> {};

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
      readline
    ];

  configureFlags =
    [ "--disable-init-state"
      "--enable-gc"
    ];

  enableParallelBuilding = true;

  installFlags = "sysconfdir=$(out)/etc";

  shellHook =
    ''
      configureFlags+=" --prefix=$(pwd)/inst"
    '';
}
