R""(

# Examples

* Copy the `dwarffs` flake and its dependencies to a binary cache:

  ```console
  # nix flake archive --to file:///tmp/my-cache dwarffs
  ```

* Fetch the `dwarffs` flake and its dependencies to the local Nix
  store:

  ```console
  # nix flake archive dwarffs
  ```

* Print the store paths of the flake sources of NixOps without
  fetching them:

  ```console
  # nix flake archive --json --dry-run nixops
  ```

# Description

FIXME

)""
