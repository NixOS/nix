R""(

# Examples

* Install a package from Nixpkgs:

  ```console
  # nix profile install nixpkgs#hello
  ```

* Install a package from a specific branch of Nixpkgs:

  ```console
  # nix profile install nixpkgs/release-20.09#hello
  ```

* Install a package from a specific revision of Nixpkgs:

  ```console
  # nix profile install nixpkgs/d73407e8e6002646acfdef0e39ace088bacc83da#hello
  ```

* Install a specific output of a package:

  ```console
  # nix profile install nixpkgs#bash^man
  ```


# Description

This command adds [*installables*](./nix.md#installables) to a Nix profile.

)""
