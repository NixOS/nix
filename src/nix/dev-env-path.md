R""(

# Examples

* Print the store path of hello's build environment:

  ```console
  # nix dev-env-path nixpkgs#hello
  ```

* Print the store path of the default devShell:

  ```console
  # nix dev-env-path .#devShells.x86_64-linux.default
  ```

# Description

This command prints the store path of the file describing the build environment
of [*installable*](./nix.md#installables) used by `nix develop` and `nix
print-dev-env`. This is useful in combination with `nix store gc-closure`,
because `devShells` are never directly instantiated.

)""
