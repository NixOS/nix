{
  name = "nix";

  description = "The purely functional package manager";

  epoch = 201906;

  inputs = [ "nixpkgs" ];

  outputs = inputs: rec {

    hydraJobs = import ./release.nix {
      nix = inputs.self;
      nixpkgs = inputs.nixpkgs;
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
      nixpkgs = inputs.nixpkgs;
    };
  };
}
