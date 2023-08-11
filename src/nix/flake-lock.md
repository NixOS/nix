R""(

# Examples

* Update the `nixpkgs` and `nix` inputs of the flake in the current
  directory:

  ```console
  # nix flake lock --update-input nixpkgs --update-input nix
  warning: creating lock file '/home/myself/repos/testflake/flake.lock':
  • Added input 'nix':
      'github:NixOS/nix/9fab14adbc3810d5cc1f88672fde1eee4358405c' (2023-06-28)
  • Added input 'nixpkgs':
      'github:NixOS/nixpkgs/3d2d8f281a27d466fa54b469b5993f7dde198375' (2023-06-30)
  ```

# Description

This command updates the lock file of a flake (`flake.lock`) so that
it contains a lock for every flake input specified in
`flake.nix`. Existing lock file entries are not updated unless
required by a flag such as `--update-input`.

Note that every command that operates on a flake will also update the
lock file if needed, and supports the same flags. Therefore,

```console
# nix flake lock --update-input nixpkgs
# nix build
```

is equivalent to:

```console
# nix build --update-input nixpkgs
```

Thus, this command is only useful if you want to update the lock file
separately from any other action such as building.

)""
