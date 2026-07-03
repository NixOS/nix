{
  # Pinned to the exact same revision the impure benchmark uses via NIX_PATH,
  # so the only variable between pure and impure is the evaluation machinery.
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/3aa71a66694b02eebbcb563cb1dc06a641d8c2d4";
  outputs =
    { self, nixpkgs }:
    {
      nixosConfigurations.bench = nixpkgs.lib.nixosSystem {
        system = "x86_64-linux";
        modules = [ ./configuration.nix ];
      };
      # Force the whole module system + derivation graph to evaluate.
      top = self.nixosConfigurations.bench.config.system.build.toplevel.drvPath;
    };
}
