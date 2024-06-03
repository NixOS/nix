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

* Upload all flake inputs to a different machine for remote evaluation

  ```
  # nix flake archive --to ssh://some-machine
  ```

  On the remote machine the flake can then be accessed via its store path. That's computed like this:

  ```
  # nix flake metadata --json | jq -r '.path'
  ```

# Description

Copy a flake and all its inputs to a store. This is useful i.e. to evaluate flakes on a different host.

)""
