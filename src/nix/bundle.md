R""(

# Examples

* Bundle Hello:

  ```console
  # nix bundle nixpkgs#hello
  # ./hello
  Hello, world!
  ```

* Bundle a specific version of Nix:

  ```console
  # nix bundle github:NixOS/nix/e3ddffb27e5fc37a209cfd843c6f7f6a9460a8ec
  # ./nix --version
  nix (Nix) 2.4pre20201215_e3ddffb
  ```

# Description

`nix bundle` packs the closure of the [Nix app](./nix3-run.md)
*installable* into a single self-extracting executable. See the
[`nix-bundle` homepage](https://github.com/matthewbauer/nix-bundle)
for more details.

> **Note**
>
> This command only works on Linux.

# Bundler definitions

TODO

)""
