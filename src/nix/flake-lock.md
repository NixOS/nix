R""(

# Examples

* Create the lock file for the flake in the current directory:

  ```console
  # nix flake lock
  warning: creating lock file '/home/myself/repos/testflake/flake.lock':
  • Added input 'nix':
      'github:NixOS/nix/9fab14adbc3810d5cc1f88672fde1eee4358405c' (2023-06-28)
  • Added input 'nixpkgs':
      'github:NixOS/nixpkgs/3d2d8f281a27d466fa54b469b5993f7dde198375' (2023-06-30)
  ```

* Add missing inputs to the lock file for a flake in a different directory:

  ```console
  # nix flake lock ~/repos/another
  warning: updating lock file '/home/myself/repos/another/flake.lock':
  • Added input 'nixpkgs':
      'github:NixOS/nixpkgs/3d2d8f281a27d466fa54b469b5993f7dde198375' (2023-06-30)
  ```

  > **Note**
  >
  > When trying to refer to a flake in a subdirectory, write `./another`
  > instead of `another`.
  > Otherwise Nix will try to look up the flake in the registry.

# Description

This command adds inputs to the lock file of a flake (`flake.lock`)
so that it contains a lock for every flake input specified in
`flake.nix`. Existing lock file entries are not updated.

If you want to update existing lock entries, use
[`nix flake update`](@docroot@/command-ref/new-cli/nix3-flake-update.md)

)""
