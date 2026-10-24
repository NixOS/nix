let
  path = "/nix/store/11111111111111111111111111111111-source";
in
builtins.derivationOf (
  builtins.appendContext path {
    ${path}.path = true;
  }
)
