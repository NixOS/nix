{system}:

derivation {
  name = "nix-pull";
  builder = ./builder.sh;
  inherit system;
}
