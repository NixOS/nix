R""(

# Examples

* Recreate the lock file (i.e. update all inputs) and commit the new
  lock file:

  ```console
  # nix flake update
  * Updated 'nix': 'github:NixOS/nix/9fab14adbc3810d5cc1f88672fde1eee4358405c' -> 'github:NixOS/nix/8927cba62f5afb33b01016d5c4f7f8b7d0adde3c'
  * Updated 'nixpkgs': 'github:NixOS/nixpkgs/3d2d8f281a27d466fa54b469b5993f7dde198375' -> 'github:NixOS/nixpkgs/a3a3dda3bacf61e8a39258a0ed9c924eeca8e293'
  …
  warning: committed new revision '158bcbd9d6cc08ab859c0810186c1beebc982aad'
  ```

# Description

This command recreates the lock file of a flake (`flake.lock`), thus
updating the lock for every mutable input (like `nixpkgs`) to its
current version. This is equivalent to passing `--recreate-lock-file`
to any command that operates on a flake. That is,

```console
# nix flake update
# nix build
```

is equivalent to:

```console
# nix build --recreate-lock-file
```

)""
