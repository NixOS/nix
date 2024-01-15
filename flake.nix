{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  # A very basic flake to conveniently `nix run nix/2.3` for testing.
  # Note that this release has no built-in support for flakes,
  # and this flake declaration is provided on a best-effort basis.
  outputs = { self, nixpkgs, ... }:
    let
      inherit (nixpkgs) lib;
      inherit (lib) flip genAttrs mapAttrs substring;
      rev = self.sourceInfo.rev or self.sourceInfo.dirtyRev or "";
      revCount = self.sourceInfo.revCount or 0;
      shortRev = self.sourceInfo.shortRev or (substring 0 7 rev);
      release = import ./release.nix {
        nix = {
          outPath = ./.;
          inherit rev revCount shortRev;
        };
        nixpkgs = nixpkgs.outPath;
        officialRelease = false;
      };
    in
  {
    # inherit release;
    packages =
      mapAttrs
        (system: nix: {
          default = nix;
          nix = nix;
        })
        release.build;
    apps =
      mapAttrs
        (system: packages:
          let
            appFor = mainProgram: {
              type = "app";
              program = lib.getExe' packages.nix mainProgram;
            };
          in flip genAttrs appFor [
            "nix"
            "nix-build"
            "nix-channel"
            "nix-collect-garbage"
            "nix-copy-closure"
            "nix-daemon"
            "nix-env"
            "nix-hash"
            "nix-instantiate"
            "nix-prefetch-url"
            "nix-shell"
            "nix-store"
          ])
        self.packages;
  };
}

