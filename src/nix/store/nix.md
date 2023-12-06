R""(

# Description

Nix is a tool for building software, configurations and other
artifacts in a reproducible and declarative way. For more information,
see the [Nix homepage](https://nixos.org/) or the [Nix
manual](https://nixos.org/manual/nix/stable/).

# Installables

Many `nix` subcommands operate on one or more *installables*. These are
command line arguments that represent something that can be built in
the Nix store. Here are the recognised types of installables:

* **Store paths**: `/nix/store/v5sv61sszx301i0x6xysaqzla09nksnd-hello-2.10`

  These are paths inside the Nix store, or symlinks that resolve to a
  path in the Nix store.

* **Store derivations**: `/nix/store/p7gp6lxdg32h4ka1q398wd9r2zkbbz2v-hello-2.10.drv`

  Store derivations are store paths with extension `.drv` and are a
  low-level representation of a build-time dependency graph used
  internally by Nix. By default, if you pass a store derivation to a
  `nix` subcommand, it will operate on the *output paths* of the
  derivation. For example, `nix path-info` prints information about
  the output paths:

  ```console
  # nix path-info --json /nix/store/p7gp6lxdg32h4ka1q398wd9r2zkbbz2v-hello-2.10.drv
  [{"path":"/nix/store/v5sv61sszx301i0x6xysaqzla09nksnd-hello-2.10",…}]
  ```

  If you want to operate on the store derivation itself, pass the
  `--derivation` flag.

# Nix stores

Most `nix` subcommands operate on a *Nix store*.

TODO: list store types, options

)""
