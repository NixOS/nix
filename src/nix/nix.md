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
  page](./nix3-flake.md).  Flake references are most commonly a flake
  identifier in the flake registry (e.g. `nixpkgs`), or a raw path
  (e.g. `/path/to/my-flake` or `.` or `../foo`), or a full URL
  (e.g. `github:nixos/nixpkgs` or `path:.`)

  When the flake reference is a raw path (a path without any URL
  scheme), it is interpreted as a `path:` or `git+file:` url in the following
  way:
  
  - If the path is within a Git repository, then the url will be of the form
    `git+file://[GIT_REPO_ROOT]?dir=[RELATIVE_FLAKE_DIR_PATH]`
    where `GIT_REPO_ROOT` is the path to the root of the git repository,
    and `RELATIVE_FLAKE_DIR_PATH` is the path (relative to the directory
    root) of the closest parent of the given path that contains a `flake.nix` within
    the git repository.
    If no such directory exists, then Nix will error-out.
    
    Note that the search will only include files indexed by git. In particular, files
    which are matched by `.gitignore` or have never been `git add`-ed will not be
    available in the flake. If this is undesirable, specify `path:<directory>` explicitly;
    
    For example, if `/foo/bar` is a git repository with the following structure:
    ```
    .
    └── baz
        ├── blah
        │   └── file.txt
        └── flake.nix
    ```

  Then `/foo/bar/baz/blah` will resolve to `git+file:///foo/bar?dir=baz`

  - If the supplied path is not a git repository, then the url will have the form
    `path:FLAKE_DIR_PATH` where `FLAKE_DIR_PATH` is the closest parent
    of the supplied path that contains a `flake.nix` file (within the same file-system).
    If no such directory exists, then Nix will error-out.
    
    For example, if `/foo/bar/flake.nix` exists, then `/foo/bar/baz/` will resolve to
   `path:/foo/bar`

  If *attrpath* is omitted, Nix tries some default values; for most
  subcommands, the default is `packages.`*system*`.default`
  (e.g. `packages.x86_64-linux.default`), but some subcommands have
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
