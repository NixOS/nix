{
  name = "nix";

  description = "The purely functional package manager";

  requires = [ flake:nixpkgs ];

  provides = flakes: rec {

    hydraJobs = import ./release.nix {
      nix = flakes.nix; # => flakes.self?
      nixpkgs = flakes.nixpkgs;
    };

    packages.nix = hydraJobs.build.x86_64-linux;

    defaultPackage = packages.nix;
  };
}
