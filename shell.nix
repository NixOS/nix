{ useClang ? false, crossSystem ? null }:

let
  pkgsSrc = builtins.fetchTarball https://github.com/NixOS/nixpkgs-channels/archive/nixos-19.09.tar.gz;
in

with import pkgsSrc { inherit crossSystem; };

with import ./release-common.nix { inherit pkgs; };

(if useClang then clangStdenv else stdenv).mkDerivation {
  name = "nix";

  nativeBuildInputs = nativeBuildDeps ++ tarballDeps;

  buildInputs = buildDeps ++ perlDeps;

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
