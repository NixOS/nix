{
  inputs.nix.url = "../..";
  inputs.nix.inputs.nixpkgs.url =
    "https://flakehub.com/f/DeterminateSystems/secure-packages-rolling/0";

  outputs = { self, nix }: nix;
}
