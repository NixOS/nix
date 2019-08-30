{
  description = "The purely functional package manager";

  edition = 201909;

  outputs = { self, nixpkgs }: rec {

    hydraJobs = import ./release.nix {
      nix = self;
      nixpkgs = nixpkgs;
    };

    checks = {
      binaryTarball = hydraJobs.binaryTarball.x86_64-linux;
      perlBindings = hydraJobs.perlBindings.x86_64-linux;
      inherit (hydraJobs.tests) remoteBuilds nix-copy-closure;
      setuid = hydraJobs.tests.setuid.x86_64-linux;
    };

    packages = {
      nix = hydraJobs.build.x86_64-linux;
      nix-perl-bindings = hydraJobs.perlBindings.x86_64-linux;
    };

    defaultPackage = packages.nix;

    devShell = import ./shell.nix {
      inherit nixpkgs;
    };
  };
}
