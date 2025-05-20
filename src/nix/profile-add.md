R""(

# Examples

- Add a package from Nixpkgs:

  ```console
  # nix profile add nixpkgs#hello
  ```

- Add a package from a specific branch of Nixpkgs:

  ```console
  # nix profile add nixpkgs/release-20.09#hello
  ```

- Add a package from a specific revision of Nixpkgs:

  ```console
  # nix profile add nixpkgs/d73407e8e6002646acfdef0e39ace088bacc83da#hello
  ```

- Add a specific output of a package:

  ```console
  # nix profile add nixpkgs#bash^man
  ```

# Description

This command adds [_installables_](./nix.md#installables) to a Nix profile.

> **Note**
>
> `nix profile install` is an alias for `nix profile add`.

)""
