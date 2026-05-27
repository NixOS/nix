{
  inputs.nix.url = "../..";
  inputs.nix.inputs.nixpkgs.url = "https://flakehub.com/f/DeterminateSystems/secure-packages-25.11/0";

  outputs = { self, nix }: nix;
}
