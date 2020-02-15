{ useClang ? false }:

with import (builtins.fetchTarball https://github.com/NixOS/nixpkgs-channels/archive/nixos-19.03.tar.gz) {};

with import ./release-common.nix { inherit pkgs; };

(if useClang then clangStdenv else stdenv).mkDerivation {
  name = "nix";

  buildInputs = buildDeps ++ propagatedDeps ++ tarballDeps ++ perlDeps;

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
