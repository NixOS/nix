{
  name = "nix";

  description = "The purely functional package manager";

  epoch = 2019;

  requires = [ "nixpkgs" ];

  provides = deps: rec {

    hydraJobs = import ./release.nix {
      nix = deps.self;
      nixpkgs = deps.nixpkgs;
    };

    checks.binaryTarball = hydraJobs.binaryTarball.x86_64-linux;

    packages = {
      nix = hydraJobs.build.x86_64-linux;
      nix-perl-bindings = hydraJobs.perlBindings.x86_64-linux;
    };

    defaultPackage = packages.nix;

    devShell = import ./shell.nix {
      nixpkgs = deps.nixpkgs;
    };
  };
}
