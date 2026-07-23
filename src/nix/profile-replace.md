R""(

# Examples

* Replace a package with a different one:

  ```console
  # nix profile replace hello nixpkgs#hello-unfree
  ```

* Replace a package with a specific version:

  ```console
  # nix profile replace nodejs nixpkgs#nodejs-18_x
  ```

# Description

This command replaces an existing package in a profile with a new one.
The first argument identifies the package to replace, and the second argument
is the [_installable_](./nix.md#installables) to install in its place.

The new package takes the place of the old one, preserving the element
name unless the replacement has a different name.

)""
