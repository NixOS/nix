{
  name = "hello";

  description = "A flake for building Hello World";

  epoch = 201906;

  requires = [ "nixpkgs" ];

  provides = deps: rec {

    packages.hello = deps.nixpkgs.provides.packages.hello;

  };
}
