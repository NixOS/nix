R""(

# Examples

* Fetch the inputs of the `hydra` flake:

  ```console
  # nix flake prefetch-inputs github:NixOS/hydra
  ```

# Description

Fetch the inputs of a flake. This ensures that they are already available for any subsequent evaluation of the flake.

This operation is recursive: it will fetch not just the direct inputs of the top-level flake, but also transitive inputs.

)""
