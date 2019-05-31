# Nix Flake MVP

## Goals

* To provide Nix repositories with an easy and standard way to
  reference other Nix repositories.

* To allow such references to be queried and updated automatically.

* To provide a replacement for `nix-channel`, `NIX_PATH` and Hydra
  jobset definitions.

* To enable reproducible, hermetic evaluation of packages and NixOS
  configurations.

Things that we probably won't do in the initial iteration:

* Sophisticated flake versioning, such as the ability to specify
  version ranges on dependencies.

* A way to specify the types of values provided by a flake. For the
  most part, flakes can provide arbitrary Nix values, but there will
  be some standard attribute names (e.g. `packages` must be a set of
  installable derivations).


## Overview

* A flake is (usually) a Git repository that contains a file named
  `flake.nix` at top-level.

* Flakes *provide* an attribute set of values, such as packages,
  Nixpkgs overlays, NixOS modules, library functions, Hydra jobs,
  `nix-shell` definitions, etc.

* Flakes can *depend* on other flakes.

* Flakes are referred to using a *flake reference*, which is either a
  URL specifying its repository's location
  (e.g. `github:NixOS/nixpkgs/release-18.09`) or an identifier
  (e.g. `nixpkgs`) looked up in a *lock file* or *flake
  registry*. They can also specify revisions,
  e.g. `github:NixOS/nixpkgs/98a2a5b5370c1e2092d09cb38b9dcff6d98a109f`.

* The *flake registry* is a centrally maintained mapping (on
  `nixos.org`) from flake identifiers to flake locations
  (e.g. `nixpkgs -> github:NixOS/nixpkgs/release-18.09`).

* A flake can contain a *lock file* (`flake.lock`) used when resolving
  the dependencies in `flake.nix`. It maps flake references to
  references containing revisions (e.g. `nixpkgs ->
  github:NixOS/nixpkgs/98a2a5b5370c1e2092d09cb38b9dcff6d98a109f`).

* The `nix` command uses the flake registry as its default
  installation source. For example, `nix build nixpkgs.hello` builds the
  `hello` package provided by the `nixpkgs` flake listed in the
  registry. `nix` will automatically download/upload the registry and
  flakes as needed.

* `nix build` without arguments will build the flake in the current
  directory (or some parent).

* The command `nix flake update` generates/updates `flake.lock` from
  `flake.nix`. This should probably also be done automatically when
  building from a local flake.

* `nixos-rebuild` will build a configuration from a (locked)
  flake. Evaluation will be done in pure mode to ensure there are no
  unaccounted inputs. Thus the NixOS configuration can be reproduced
  unambiguously from the top-level flake.

* Nix code can query flake metadata such as `commitHash` (the Git
  revision) or `date` (the date of the last commit). This is useful
  for NixOS to compute the NixOS version string (which will be the
  revision of the top-level configuration flake, uniquely identifying
  the configuration).

* Hydra jobset configurations will consist of a single flake
  reference. Thus we can get rid of jobset inputs; any other needed
  repositories can be fetched by the top-level flake. The top-level
  flake can be locked or unlocked; if some dependencies are unlocked,
  then Nix will fetch the latest revision for each.


## Example flake

A flake is a Git repository that contains a file named
`flake.nix`. For example, here is the `flake.nix` for `dwarffs`, a
small repository that provides a single package and a single NixOS
module.

```nix
{
  # The flake identifier.
  name = "dwarffs";

  # The epoch may be used in the future to determine how Nix
  # expressions inside this flake are to be parsed.
  epoch = 2018;

  # Some other metadata.
  description = "A filesystem that fetches DWARF debug info from the Internet on demand";

  # A list of flake references denoting the flakes that this flake
  # depends on. Nix will resolve and fetch these flakes and pass them
  # as a function argument to `outputs` below.
  #
  # `flake:nixpkgs` denotes a flake named `nixpkgs` which is looked up
  # in the flake registry, or in `flake.lock` inside this flake, if it
  # exists.
  inputs = [ flake:nixpkgs ];

  # The stuff provided by this flake. Flakes can provide whatever they
  # want (convention over configuration), but some attributes have
  # special meaning to tools / other flakes: for example, `packages`
  # is used by the `nix` CLI to search for packages, and
  # `nixosModules` is used by NixOS to automatically pull in the
  # modules provided by a flake.
  #
  # `outputs` takes a single argument named `deps` that contains
  # the resolved set of flakes. (See below.)
  outputs = deps: {

    # This is searched by `nix`, so something like `nix install
    # dwarffs.dwarffs` resolves to this `packages.dwarffs`.
    packages.dwarffs =
      with deps.nixpkgs.packages;
      with deps.nixpkgs.builders;
      with deps.nixpkgs.lib;

      stdenv.mkDerivation {
        name = "dwarffs-0.1";

        buildInputs = [ fuse nix nlohmann_json boost ];

        NIX_CFLAGS_COMPILE = "-I ${nix.dev}/include/nix -include ${nix.dev}/include/nix/config.h -D_FILE_OFFSET_BITS=64";

        src = cleanSource ./.;

        installPhase =
          ''
            mkdir -p $out/bin $out/lib/systemd/system

            cp dwarffs $out/bin/
            ln -s dwarffs $out/bin/mount.fuse.dwarffs

            cp ${./run-dwarffs.mount} $out/lib/systemd/system/run-dwarffs.mount
            cp ${./run-dwarffs.automount} $out/lib/systemd/system/run-dwarffs.automount
          '';
      };

    # NixOS modules.
    nixosModules.dwarffs = import ./module.nix deps;

    # Provide a single Hydra job (`hydraJobs.dwarffs`).
    hydraJobs = deps.this.packages;
  };
}
```

Similarly, a minimal `flake.nix` for Nixpkgs:

```nix
{
  name = "nixpkgs";

  epoch = 2018;

  description = "A collection of packages for the Nix package manager";

  outputs = deps:
    let pkgs = import ./. {}; in
    {
      lib = import ./lib;

      builders = {
        inherit (pkgs) stdenv fetchurl;
      };

      packages = {
        inherit (pkgs) hello nix fuse nlohmann_json boost;
      };
    };
}
```
Note that `packages` is an unpolluted set of packages: non-package
values like `lib` or `fetchurl` are not part of it.


## Flake identifiers

A flake has an identifier (e.g. `nixpkgs` or `dwarffs`).


## Flake references

Flake references are a URI-like syntax to specify the physical
location of a flake (e.g. a Git repository) or to denote a lookup in
the flake registry or lock file.

* `(flake:)?<flake-id>(/rev-or-ref(/rev)?)?`

  Look up a flake by ID in the flake lock file or in the flake
  registry. These must specify an actual location for the flake using
  the formats listed below. Note that in pure evaluation mode, the
  flake registry is empty.

  Optionally, the `rev` or `ref` from the dereferenced flake can be
  overriden. For example,

  > nixpkgs/19.09

  uses the `19.09` branch of the `nixpkgs` flake's GitHub repository,
  while

  > nixpkgs/98a2a5b5370c1e2092d09cb38b9dcff6d98a109f

  uses the specified revision. For Git (rather than GitHub)
  repositories, both the rev and ref must be given, e.g.

  > nixpkgs/19.09/98a2a5b5370c1e2092d09cb38b9dcff6d98a109f

* `github:<owner>/<repo>(/<rev-or-ref>)?`

   A repository on GitHub. These differ from Git references in that
   they're downloaded in a efficient way (via the tarball mechanism)
   and that they support downloading a specific revision without
   specifying a branch. `rev-or-ref` is either a commit hash (`rev`)
   or a branch or tag name (`ref`). The default is `master` if none is
   specified. Note that in pure evaluation mode, a commit hash must be
   used.

   Flakes fetched in this manner expose `rev` and `date` attributes,
   but not `revCount`.

   Examples:

   > github:edolstra/dwarffs

   > github:edolstra/dwarffs/unstable

   > github:edolstra/dwarffs/41c0c1bf292ea3ac3858ff393b49ca1123dbd553

* > https://<server>/<path>.git(\?attr(&attr)*)?

  > ssh://<server>/<path>.git(\?attr(&attr)*)?

  > git://<server>/<path>.git(\?attr(&attr)*)?

  > file:///<path>(\?attr(&attr)*)?

   where `attr` is one of `rev=<rev>` or `ref=<ref>`.

   A Git repository fetched through https. Note that the path must end
   in `.git`. The default for `ref` is `master`.

   Examples:

   > https://example.org/my/repo.git
   > https://example.org/my/repo.git?ref=release-1.2.3
   > https://example.org/my/repo.git?rev=e72daba8250068216d79d2aeef40d4d95aff6666

* > /path.git(\?attr(&attr)*)?

  Like `file://path.git`, but if no `ref` or `rev` is specified, the
  (possibly dirty) working tree will be used. Using a working tree is
  not allowed in pure evaluation mode.

  Examples:

  > /path/to/my/repo

  > /path/to/my/repo?ref=develop

  > /path/to/my/repo?rev=e72daba8250068216d79d2aeef40d4d95aff6666

* > https://<server>/<path>.tar.xz(?hash=<sri-hash>)

  > file:///<path>.tar.xz(?hash=<sri-hash>)

   A flake distributed as a tarball. In pure evaluation mode, an SRI
   hash is mandatory. It exposes a `date` attribute, being the newest
   file inside the tarball.

   Example:

   > https://releases.nixos.org/nixos/unstable/nixos-19.03pre167858.f2a1a4e93be/nixexprs.tar.xz

   > https://releases.nixos.org/nixos/unstable/nixos-19.03pre167858.f2a1a4e93be/nixexprs.tar.xz?hash=sha256-56bbc099995ea8581ead78f22832fee7dbcb0a0b6319293d8c2d0aef5379397c

Note: currently, there can be only one flake per Git repository, and
it must be at top-level. In the future, we may want to add a field
(e.g. `dir=<dir>`) to specify a subdirectory inside the repository.


## Flake lock files

This is a JSON file named `flake.lock` that maps flake identifiers
used in the corresponding `flake.nix` to "immutable" flake references;
that is, flake references that contain a revision (for Git
repositories) or a content hash (for tarballs).

Example:

```json
{
  "nixpkgs": "github:NixOS/nixpkgs/41c0c1bf292ea3ac3858ff393b49ca1123dbd553",
  "foo": "https://example.org/foo.tar.xz?hash=sha256-56bbc099995ea8581ead78f22832fee7dbcb0a0b6319293d8c2d0aef5379397c"
}
```


## `outputs`

The flake attribute `outputs` is a function that takes an argument
named `deps` and returns a (mostly) arbitrary attrset of values. Some
of the standard result attributes:

* `packages`: A set of installable derivations used by the `nix`
  command. That is, commands such as `nix install` ignore all other
  flake attributes.

* `hydraJobs`: Used by Hydra.

* `nixosModules`: An attrset of NixOS modules.

* `nixosSystems`: An attrset of calls to `evalModules`, i.e. things
  that `nixos-rebuild` can switch to. (Maybe this is superfluous, but
  we need to avoid a situation where `nixos-rebuild` needs to fetch
  its own `nixpkgs` just to do `evalModules`.)

* `devShell`: A specification of a development environment in some TBD
  format.

The function argument `flakes` is an attrset that contains an
attribute for each dependency specified in `inputs`. (Should it
contain transitive dependencies? Probably not.) Each attribute is an
attrset containing the `outputs` of the dependency, in addition to
the following attributes:

* `path`: The path to the flake's source code. Useful when you want to
  use non-Nix artifacts from the flake, or if you want to *store* the
  source code of the dependency in a derivation. (For example, we
  could store the sources of all flake dependencies in a NixOS system
  configuration, as a generalization of
  `system.copySystemConfiguration`.)

* `meta`: An attrset containing the following:

  * `description`

  * `commitHash` (or `rev`?) (not for tarball flakes): The Git commit
    hash.

  * `date`: The timestamp of the most recent commit (for Git
    repositories), or the timestamp of the most recently modified file
    (for tarballs).

  * `revCount` (for Git flakes, but not GitHub flakes): The number of
    ancestors of the revision. Useful for generating version strings.


## Non-flake dependencies

It may be useful to pull in repositories that are not flakes
(i.e. don't contain a `flake.nix`). This could be done in two ways:

* Allow flakes not to have a `flake.nix` file, in which case it's a
  flake with no inputs and no outputs. The downside of this
  approach is that we can't detect accidental use of a non-flake
  repository. (Also, we need to conjure up an identifier somehow.)

* Add a flake attribute to specifiy non-flake dependencies, e.g.

  > nonFlakeInputs.foobar = github:foo/bar;


## Flake registry

The flake registry maps flake IDs to flake references (where the
latter cannot be another indirection, i.e. it must not be a
`flake:<flake-id>` reference).

The default registry is kept at
`https://nixos.org/flake-registry.json`. It looks like this:

```json
{
    "version": 1,
    "flakes": {
        "dwarffs": {
            "uri": "github:edolstra/dwarffs/flake"
        },
        "nixpkgs": {
            "uri": "github:NixOS/nixpkgs/release-18.09"
        }
    }
}
```

Nix automatically (re)downloads the registry. The downloaded file is a
GC root so the registry remains available if nixos.org is unreachable.
TBD: when to redownload?


## Nix UI

Commands for registry / user flake configuration:

* `nix flake list`: Show all flakes in the registry.

* `nix flake add <flake-ref>`: Add or override a flake to/in the
  user's flake configuration (`~/.config/nix/flakes.nix`). For
  example, `nix flake add nixpkgs/nixos-18.03` overrides the `nixpkgs`
  flake to use the `nixos-18.03` branch. There should also be a way to
  add multiple branches/revisions of the same flake by giving them a
  different ID, e.g. `nix flake add --id nixpkgs-ancient
  nixpkgs/nixos-16.03`).

* `nix flake remove <flake-id>`: Remove a flake from the user's flake
  configuration. Any flake with the same ID in the registry remains
  available.

* `nix flake lock <flake-id>`: Lock a flake. For example, `nix flake
  lock nixpkgs` pins `nixpkgs` to the current revision.

Commands for creating/modifying a flake:

* `nix flake init`: Create a `flake.nix` in the current directory.

* `nix flake update`: Update the lock file for the `flake.nix` in the
  current directory. In most cases, this should be done
  automatically. (E.g. `nix build` should automatically update the
  lock file is a new dependency is added to `flake.nix`.)

* `nix flake check`: Do some checks on the flake, e.g. check that all
  `packages` are really packages.

* `nix flake clone`: Do a Git clone of the flake repository. This is a
  convenience to easily start hacking on a flake. E.g. `nix flake
  clone dwarffs` clones the `dwarffs` GitHub repository to `./dwarffs`.

TODO: maybe the first set of commands should have a different name
from the second set.

Flags / configuration options:

* `--flakes (<flake-id>=<flake-ref>)*`: add/override some flakes.

* (In `nix`) `--flake <flake-ref>`: set the specified flake as the
  installation source. E.g. `nix build --flake ./my-nixpkgs hello`.

The default installation source in `nix` is the `packages` from all
flakes in the registry, that is:
```
builtins.mapAttrs (flakeName: flakeInfo:
  (getFlake flakeInfo.uri).${flakeName}.outputs.packages or {})
  builtins.flakeRegistry
```
(where `builtins.flakeRegistry` is the global registry with user
overrides applied, and `builtins.getFlake` downloads a flake and
resolves its dependencies.)

It may be nice to extend the default installation source with the
`packages` from the flake in the current directory, so that

> nix build hello

does something similar to the old

> nix-build -A hello

Specifically, it builds `packages.hello` from the flake in the current
directory. Of course, this creates some ambiguity if there is a flake
in the registry named `hello`.

Maybe the command

> nix dev-shell

should do something like use `outputs.devShell` to initialize the
shell, but probably we should ditch `nix shell` / `nix-shell` for
direnv.


## Pure evaluation and caching

Flake evaluation should be done in pure mode. Thus:

* Flakes cannot do `NIX_PATH` lookups via the `<...>` syntax.

* They can't read random stuff from non-flake directories, such as
  `~/.nix/config.nix` or overlays.

This enables aggressive caching or precomputation of Nixpkgs package
sets. For example, for a particular Nixpkgs flake closure (as
identified by, say, a hash of the fully-qualified flake references
after dependency resolution) and system type, an attribute like
`packages.hello` should always evaluate to the same derivation. So we
can:

* Keep a local evaluation cache (say `~/.cache/nix/eval.sqlite`)
  mapping `(<flake-closure-hash, <attribute>) -> (<drv-name>,
  <drv-output-paths>, <whatever other info we want to cache>)`.

* Download a precomputed cache
  (e.g. `https://releases.nixos.org/eval/<flake-closure-hash>.sqlite`). So
  a command like `nix search` could avoid evaluating Nixpkgs entirely.

Of course, this doesn't allow overlays. With pure evaluation, the only
way to have these is to define a top-level flake that depends on the
Nixpkgs flake and somehow passes in a set of overlays.

TODO: in pure mode we have to pass the system type explicitly!


## Hydra jobset dependencies

Hydra can use the flake dependency resolution mechanism to fetch
dependencies. This allows us to get rid of jobset configuration in the
web interface: a jobset only requires a flake reference. That is, *a
jobset is a flake*. Hydra then just builds the `hydraJobs` attrset
`provide`d by the flake. (It omitted, maybe it can build `packages`.)


## NixOS system configuration

NixOS currently contains a lot of modules that really should be moved
into their own repositories. For example, it contains a Hydra module
that duplicates the one in the Hydra repository. Also, we want
reproducible evaluation for NixOS system configurations. So NixOS
system configurations should be stored as flakes in (local) Git
repositories.

`my-system/flake.nix`:

```nix
{
  outputs = flakes: {
    nixosSystems.default =
      flakes.nixpkgs.lib.evalModules {
        modules =
          [ { networking.firewall.enable = true;
              hydra.useSubstitutes = true;
            }
            # The latter could be extracted automatically from `flakes`.
            flakes.dwarffs.nixosModules.dwarffs
            flakes.hydra.nixosModules.hydra
          ];
      };
  };

  inputs =
    [ "nixpkgs/nixos-18.09"
      "dwarffs"
      "hydra"
      ... lots of other module flakes ...
    ];
}
```

We can then build the system:
```
nixos-rebuild switch --flake ~/my-system
```
This performs dependency resolution starting at `~/my-system/flake.nix`
and builds the `system` attribute in `nixosSystems.default`.
