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

* Bundle a Hello using a specific bundler:

  ```console
  # nix bundle --bundler github:NixOS/bundlers#toDockerImage nixpkgs#hello
  # docker load < hello-2.10.tar.gz
  # docker run hello-2.10:latest hello
  Hello, world!
  ```

# Description

`nix bundle`, by default, packs the closure of the *installable* into a single
self-extracting executable. See the [`bundlers`
homepage](https://github.com/NixOS/bundlers) for more details.

> **Note**
>
> This command only works on Linux.

# Flake output attributes

If no flake output attribute is given, `nix bundle` tries the following
flake output attributes:

* `bundlers.<system>.default`

If an attribute *name* is given, `nix run` tries the following flake
output attributes:

* `bundlers.<system>.<name>`

# Bundlers

A bundler is specified by a flake output attribute named
`bundlers.<system>.<name>`. It looks like this:

```nix
bundlers.x86_64-linux = rec {
  identity = drv: drv;

  blender_2_79 = drv: self.packages.x86_64-linux.blender_2_79;

  default = identity;
};
```

A bundler must be a function that accepts an arbitrary value (typically a
derivation or app definition) and returns a derivation.

)""
