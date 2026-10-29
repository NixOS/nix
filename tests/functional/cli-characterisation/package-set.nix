{
  greet = import ./function.nix { } // {
    meta.description = "A greeting";
  };
}
