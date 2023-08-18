R""(

# Examples

* Pin `nixpkgs` to its most recent Git revision:

  ```console
  # nix registry pin nixpkgs
  ```

  Afterwards the user registry will have an entry like this:

  ```console
  nix registry list | grep '^user '
  user   flake:nixpkgs github:NixOS/nixpkgs/925b70cd964ceaedee26fde9b19cc4c4f081196a
  ```

  and `nix flake info` will say:

  ```console
  # nix flake info nixpkgs
  Resolved URL:  github:NixOS/nixpkgs/925b70cd964ceaedee26fde9b19cc4c4f081196a
  Locked URL:    github:NixOS/nixpkgs/925b70cd964ceaedee26fde9b19cc4c4f081196a
  …
  ```

* Pin `nixpkgs` in a custom registry to its most recent Git revision:

  ```console
  # nix registry pin --registry ./custom-flake-registry.json nixpkgs
  ```


# Description

This command adds an entry to the user registry that maps flake
reference *url* to the corresponding *locked* flake reference, that
is, a flake reference that specifies an exact revision or content
hash. This ensures that until this registry entry is removed, all uses
of *url* will resolve to exactly the same flake.

Entries can be removed using [`nix registry
remove`](./nix3-registry-remove.md).

)""
