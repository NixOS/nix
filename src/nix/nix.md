R""(

# Examples

* Create a new flake:

  ```console
  # nix flake new hello
  # cd hello
  ```

* Build the flake in the current directory:

  ```console
  # nix build
  # ./result/bin/hello
  Hello, world!
  ```

* Run the flake in the current directory:

  ```console
  # nix run
  Hello, world!
  ```

* Start a development shell for hacking on this flake:

  ```console
  # nix develop
  # unpackPhase
  # cd hello-*
  # configurePhase
  # buildPhase
  # ./hello
  Hello, world!
  # installPhase
  # ../outputs/out/bin/hello
  Hello, world!
  ```

# Description

Nix is a tool for building software, configurations and other
artifacts in a reproducible and declarative way. For more information,
see the [Nix homepage](https://nixos.org/) or the [Nix
manual](https://nixos.org/manual/nix/stable/).

# Installables

Many `nix` subcommands operate on one or more *installables*. These are
command line arguments that represent something that can be built in
the Nix store. Here are the recognised types of installables:

* **Flake output attributes**: `nixpkgs#hello`

  These have the form *flakeref*[`#`*attrpath*], where *flakeref* is a
  flake reference and *attrpath* is an optional attribute path. For
  more information on flakes, see [the `nix flake` manual
  page](./nix3-flake.md). Flake references are most commonly a flake
  identifier in the flake registry (e.g. `nixpkgs`) or a path
  (e.g. `/path/to/my-flake` or `.`).

  If *attrpath* is omitted, Nix tries some default values; for most
  subcommands, the default is `defaultPackage.`*system*
  (e.g. `defaultPackage.x86_64-linux`), but some subcommands have
  other defaults. If *attrpath* *is* specified, *attrpath* is
  interpreted as relative to one or more prefixes; for most
  subcommands, these are `packages.`*system*,
  `legacyPackages.*system*` and the empty prefix. Thus, on
  `x86_64-linux` `nix build nixpkgs#hello` will try to build the
  attributes `packages.x86_64-linux.hello`,
  `legacyPackages.x86_64-linux.hello` and `hello`.

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

* **Indexed store derivations**: `/nix/store/p7gp6lxdg32h4ka1q398wd9r2zkbbz2v-hello-2.10.drv!out`

  Store derivations can be indexed with a specific output name. This
  allows finer control versus just specifying a derivation (without
  `--derivation`) and getting all the outputs.

  This is especially useful for (currently unstable) floating content
  addressed derivations, which do not have precomputed output paths that
  can be used instead.

* **Nix attributes**: `--file /path/to/nixpkgs hello`

  When the `-f` / `--file` *path* option is given, installables are
  interpreted as attribute paths referencing a value returned by
  evaluating the Nix file *path*.

* **Nix expressions**: `--expr '(import <nixpkgs> {}).hello.overrideDerivation (prev: { name = "my-hello"; })'`.

  When the `--expr` option is given, all installables are interpreted
  as Nix expressions. You may need to specify `--impure` if the
  expression references impure inputs (such as `<nixpkgs>`).

For most commands, if no installable is specified, the default is `.`,
i.e. Nix will operate on the default flake output attribute of the
flake in the current directory.

# Nix stores

Most `nix` subcommands operate on a *Nix store*.

TODO: list store types, options

)""
