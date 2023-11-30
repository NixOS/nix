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

> **Warning** \
> Installables are part of the unstable
> [`nix-command` experimental feature](@docroot@/contributing/experimental-features.md#xp-feature-nix-command),
> and subject to change without notice.

Many `nix` subcommands operate on one or more *installables*.
These are command line arguments that represent something that can be realised in the Nix store.

The following types of installable are supported by most commands:

- [Flake output attribute](#flake-output-attribute) (experimental)
- [Store path](#store-path)
- [Nix file](#nix-file), optionally qualified by an attribute path
- [Nix expression](#nix-expression), optionally qualified by an attribute path

For most commands, if no installable is specified, `.` is assumed.
That is, Nix will operate on the default flake output attribute of the flake in the current directory.

### Flake output attribute

> **Warning** \
> Flake output attribute installables depend on both the
> [`flakes`](@docroot@/contributing/experimental-features.md#xp-feature-flakes)
> and
> [`nix-command`](@docroot@/contributing/experimental-features.md#xp-feature-nix-command)
> experimental features, and subject to change without notice.

Example: `nixpkgs#hello`

These have the form *flakeref*[`#`*attrpath*], where *flakeref* is a
[flake reference](./nix3-flake.md#flake-references) and *attrpath* is an optional attribute path. For
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

If *attrpath* begins with `.` then no prefixes or defaults are attempted. This allows the form *flakeref*[`#.`*attrpath*], such as `github:NixOS/nixpkgs#.lib.fakeSha256` to avoid a search of `packages.*system*.lib.fakeSha256`

### Store path

Example: `/nix/store/v5sv61sszx301i0x6xysaqzla09nksnd-hello-2.10`

These are paths inside the Nix store, or symlinks that resolve to a path in the Nix store.

A [store derivation] is also addressed by store path.

Example: `/nix/store/p7gp6lxdg32h4ka1q398wd9r2zkbbz2v-hello-2.10.drv`

If you want to refer to an output path of that store derivation, add the output name preceded by a caret (`^`).

Example: `/nix/store/p7gp6lxdg32h4ka1q398wd9r2zkbbz2v-hello-2.10.drv^out`

All outputs can be referred to at once with the special syntax `^*`.

Example: `/nix/store/p7gp6lxdg32h4ka1q398wd9r2zkbbz2v-hello-2.10.drv^*`

### Nix file

Example: `--file /path/to/nixpkgs hello`

When the option `-f` / `--file` *path* \[*attrpath*...\] is given, installables are interpreted as the value of the expression in the Nix file at *path*.
If attribute paths are provided, commands will operate on the corresponding values accessible at these paths.
The Nix expression in that file, or any selected attribute, must evaluate to a derivation.

### Nix expression

Example: `--expr 'import <nixpkgs> {}' hello`

When the option `--expr` *expression* \[*attrpath*...\] is given, installables are interpreted as the value of the of the Nix expression.
If attribute paths are provided, commands will operate on the corresponding values accessible at these paths.
The Nix expression, or any selected attribute, must evaluate to a derivation.

You may need to specify `--impure` if the expression references impure inputs (such as `<nixpkgs>`).

## Derivation output selection

Derivations can have multiple outputs, each corresponding to a
different store path. For instance, a package can have a `bin` output
that contains programs, and a `dev` output that provides development
artifacts like C/C++ header files. The outputs on which `nix` commands
operate are determined as follows:

* You can explicitly specify the desired outputs using the syntax
  *installable*`^`*output1*`,`*...*`,`*outputN*. For example, you can
  obtain the `dev` and `static` outputs of the `glibc` package:

  ```console
  # nix build 'nixpkgs#glibc^dev,static'
  # ls ./result-dev/include/ ./result-static/lib/
  …
  ```

  and likewise, using a store path to a "drv" file to specify the derivation:

  ```console
  # nix build '/nix/store/gzaflydcr6sb3567hap9q6srzx8ggdgg-glibc-2.33-78.drv^dev,static'
  …
  ```

* You can also specify that *all* outputs should be used using the
  syntax *installable*`^*`. For example, the following shows the size
  of all outputs of the `glibc` package in the binary cache:

  ```console
  # nix path-info --closure-size --eval-store auto --store https://cache.nixos.org 'nixpkgs#glibc^*'
  /nix/store/g02b1lpbddhymmcjb923kf0l7s9nww58-glibc-2.33-123                 33208200
  /nix/store/851dp95qqiisjifi639r0zzg5l465ny4-glibc-2.33-123-bin             36142896
  /nix/store/kdgs3q6r7xdff1p7a9hnjr43xw2404z7-glibc-2.33-123-debug          155787312
  /nix/store/n4xa8h6pbmqmwnq0mmsz08l38abb06zc-glibc-2.33-123-static          42488328
  /nix/store/q6580lr01jpcsqs4r5arlh4ki2c1m9rv-glibc-2.33-123-dev             44200560
  ```

  and likewise, using a store path to a "drv" file to specify the derivation:

  ```console
  # nix path-info --closure-size '/nix/store/gzaflydcr6sb3567hap9q6srzx8ggdgg-glibc-2.33-78.drv^*'
  …
  ```
* If you didn't specify the desired outputs, but the derivation has an
  attribute `meta.outputsToInstall`, Nix will use those outputs. For
  example, since the package `nixpkgs#libxml2` has this attribute:

  ```console
  # nix eval 'nixpkgs#libxml2.meta.outputsToInstall'
  [ "bin" "man" ]
  ```

  a command like `nix shell nixpkgs#libxml2` will provide only those
  two outputs by default.

  Note that a [store derivation] (given by its `.drv` file store path) doesn't have
  any attributes like `meta`, and thus this case doesn't apply to it.

  [store derivation]: ../../glossary.md#gloss-store-derivation

* Otherwise, Nix will use all outputs of the derivation.

# Nix stores

Most `nix` subcommands operate on a *Nix store*.
The various store types are documented in the
[Store Types](@docroot@/store/types/index.md)
section of the manual.

The same information is also available from the [`nix help-stores`](./nix3-help-stores.md) command.

# Shebang interpreter

The `nix` command can be used as a `#!` interpreter.
Arguments to Nix can be passed on subsequent lines in the script.

Verbatim strings may be passed in double backtick (```` `` ````) quotes. <!-- that's markdown for two backticks in inline code. -->
Sequences of _n_ backticks of 3 or longer are parsed as _n-1_ literal backticks.
A single space before the closing ```` `` ```` is ignored if present.

`--file` and `--expr` resolve relative paths based on the script location.

Examples:

```
#!/usr/bin/env nix
#! nix shell --file ``<nixpkgs>`` hello cowsay --command bash

hello | cowsay
```

or with **flakes**:

```
#!/usr/bin/env nix
#! nix shell nixpkgs#bash nixpkgs#hello nixpkgs#cowsay --command bash

hello | cowsay
```

or with an **expression**:

```bash
#! /usr/bin/env nix
#! nix shell --impure --expr ``
#! nix with (import (builtins.getFlake "nixpkgs") {});
#! nix terraform.withPlugins (plugins: [ plugins.openstack ])
#! nix ``
#! nix --command bash

terraform "$@"
```

or with cascading interpreters. Note that the `#! nix` lines don't need to follow after the first line, to accomodate other interpreters.

```
#!/usr/bin/env nix
//! ```cargo
//! [dependencies]
//! time = "0.1.25"
//! ```
/*
#!nix shell nixpkgs#rustc nixpkgs#rust-script nixpkgs#cargo --command rust-script
*/
fn main() {
    for argument in std::env::args().skip(1) {
        println!("{}", argument);
    };
    println!("{}", std::env::var("HOME").expect(""));
    println!("{}", time::now().rfc822z());
}
// vim: ft=rust
```

)""
