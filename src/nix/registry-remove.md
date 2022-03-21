R""(

# Examples

* Remove the entry `nixpkgs` from the user registry:

  ```console
  # nix registry remove nixpkgs
  ```

* Remove the entry `nixpkgs` from a custom registry:

  ```console
  # nix registry remove --registry ./custom-flake-registry.json nixpkgs
  ```

# Description

This command removes from the user registry any entry for flake
reference *url*.

)""
