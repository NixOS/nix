R""(

# Examples

* Update the `nixpkgs` and `nix` inputs of the flake in the current
  directory:

  ```console
  # nix flake update --update-input nixpkgs --update-input nix
  * Updated 'nix': 'github:NixOS/nix/9fab14adbc3810d5cc1f88672fde1eee4358405c' -> 'github:NixOS/nix/8927cba62f5afb33b01016d5c4f7f8b7d0adde3c'
  * Updated 'nixpkgs': 'github:NixOS/nixpkgs/3d2d8f281a27d466fa54b469b5993f7dde198375' -> 'github:NixOS/nixpkgs/a3a3dda3bacf61e8a39258a0ed9c924eeca8e293'
  ```

* Recreate the lock file (i.e. update all inputs) and commit the new
  lock file:

  ```console
  # nix flake update --recreate-lock-file --commit-lock-file
  …
  warning: committed new revision '158bcbd9d6cc08ab859c0810186c1beebc982aad'
  ```

# Description

This command updates the lock file of a flake (`flake.lock`) so that
it contains a lock for every flake input specified in
`flake.nix`. Note that every command that operates on a flake will
also update the lock file if needed, and supports the same
flags. Therefore,

```console
# nix flake update --update-input nixpkgs
# nix build
```

is equivalent to:

```console
# nix build --update-input nixpkgs
```

Thus, this command is only useful if you want to update the lock file
separately from any other action such as building.

> **Note**
>
> This command does *not* update locks that are already present unless
> you explicitly ask for it using `--update-input` or
> `--recreate-lock-file`. Thus, if the lock file already has locks for
> every input, then `nix flake update` (without arguments) does
> nothing.

)""
