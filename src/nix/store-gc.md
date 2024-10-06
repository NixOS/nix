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

* Keep deleting garbage even if an error occurs while deleting a path:

  ```console
  # nix store gc --keep-going
  ```

# Description

This command deletes unreachable paths in the Nix store.

)""
