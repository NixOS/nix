let
  path = "/nix/store/00000000000000000000000000000000-missing.drv";
in
builtins.derivationOf (
  builtins.appendContext "" {
    ${path}.outputs = [
      "out"
      "dev"
    ];
  }
)
