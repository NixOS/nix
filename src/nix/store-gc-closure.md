R""(

# Examples

* Delete the unreachable paths in the closure of the current development shell

  ```console
  # nix store gc-closure $(nix dev-env-path .#devShells.x86_64-linux.default)
  ```

# Description

This command deletes unreachable paths from the Nix store within the closure of the argument.

)""
