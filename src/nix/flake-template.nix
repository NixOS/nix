{
  description = "A flake for building Hello World";

  edition = 201909;

  outputs = { self, nixpkgs }: {

    packages.x86_64-linux.hello = nixpkgs.packages.x86_64-linux.hello;

  };
}
