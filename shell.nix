{ useClang ? false }:

with import (builtins.fetchTarball https://github.com/NixOS/nixpkgs/archive/bb1013511e1e5edcf314df8321acf2f3c536df0d.tar.gz) {};

with import ./release-common.nix { inherit pkgs; };

(if useClang then clangStdenv else stdenv).mkDerivation {
  name = "nix";

  buildInputs = buildDeps ++ tarballDeps ++ perlDeps ++ [ pkgs.rustfmt ];

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
