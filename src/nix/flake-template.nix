{
  name = "hello";

  description = "A flake for building Hello World";

  epoch = 2019;

  requires = [ "nixpkgs" ];

  provides = deps: rec {

    packages.hello = deps.nixpkgs.provides.packages.hello;

  };
}
