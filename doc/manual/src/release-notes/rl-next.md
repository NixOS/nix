# Release X.Y (202?-??-??)

* `<nix/fetchurl.nix>` now accepts an additional argument `impure` which
  defaults to `false`.  If it is set to `true`, the `hash` and `sha256`
  arguments will be ignored and the resulting derivation will have
  `__impure` set to `true`, making it an impure derivation.

* You can now use flake references in the old CLI, e.g.

  ```
  # nix-build flake:nixpkgs -A hello
  # nix-build -I nixpkgs=flake:github:NixOS/nixpkgs/nixos-22.05 \
      '<nixpkgs>' -A hello
  # NIX_PATH=nixpkgs=flake:nixpkgs nix-build '<nixpkgs>' -A hello
  ```
