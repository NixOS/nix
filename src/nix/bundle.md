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

`nix bundle`, by default, packs the closure of the [Nix app](./nix3-run.md)
*installable* into a single self-extracting executable. See the [`nix-bundle`
homepage](https://github.com/matthewbauer/nix-bundle) for more details.

> **Note**
>
> This command only works on Linux.

# Bundler definitions

If no flake output attribute is given, `nix bundle` tries the following
flake output attributes:

* `defaultBundler.<system>`

If an attribute *name* is given, `nix run` tries the following flake
output attributes:

* `bundler.<system>.<name>`

# Bundlers

An bundlers is specified by a flake output attribute named
`bundlers.<system>.<name>` or `defaultBundler.<system>`. It looks like this:

```nix
bundlers.x86_64-linux.identity = drv: drv;

bundlers.x86_64-linux.blender_2_79 = drv: self.packages.x86_64-linux.blender_2_79;

defaultBundler.x86_64-linux = drv: drv;
```

A bundler must be a function that accepts a derivation and returns a derivation.

)""
