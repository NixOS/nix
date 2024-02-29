R""(

# Examples

* Delete unreachable paths in the Nix store:

  ```console
  # nix store gc
  ```

* Delete up to 1 gigabyte of garbage:

  ```console
  # nix store gc --max 1G
  ```

* Delete the unreachable paths in the closure of the current development shell

  ```console
  # nix store gc .#devShells.default
  ```

# Description

This command deletes unreachable paths from the Nix store.

If called with no argument, it will delete all the unreachable paths from the store.
If called with an installable argument, it will delete the unreachable paths whithin the closure of that argument.

)""
