# Nix Flake MVP

## Goals

* Standard and easy way for Nix repos to reference other Nix repos as
  dependencies

* Discoverability: Be able to query and update these references to Nix repos
  automatically

* To provide a replacement for `nix-channel`, `NIX_PATH` and Hydra jobset
  definitions

* Reproducibility: Evaluate packages and NixOS configurations hermetic by
  default

Upcoming but not yet implemented:

* Sophisticated flake versioning, such as the ability to specify version ranges
  on dependencies.

* A way to specify the types of values provided by a flake. For the most part,
  flakes can provide arbitrary Nix values, but there will be some standard
  attribute names (e.g. `packages` must be a set of installable derivations).


## Overview

* A flake is (usually) a Git repository that contains a file named `flake.nix`
  at top-level

* A flake *provides* an attribute set of values, such as packages, Nixpkgs
  overlays, NixOS modules, library functions, Hydra jobs, `nix-shell`
  definitions, etc.

* Flakes can *depend* on other flakes or other repositories which aren't flakes

* Flakes are referred to using a *flake reference*, which is either a URL
  specifying its repository's location or an identifier looked up in a *lock
  file* or *flake registry*.

* A *flake registry* is a mapping from flake identifiers to flake locations
  (e.g. `nixpkgs -> github:NixOS/nixpkgs/release-18.09`). There is a centrally
  maintained flake registry on `nixos.org`.

* A flake can contain a *lock file* (`flake.lock`) used when resolving the
  dependencies in `flake.nix`. It maps mutable flake references
  (e.g. `github:NixOS/nixpkgs/release-18.09`) to references containing revisions
  (e.g. `nixpkgs ->
  github:NixOS/nixpkgs/98a2a5b5370c1e2092d09cb38b9dcff6d98a109f`).

* The `nix` command uses the flake registry as its default installation source.
  For example, `nix build nixpkgs.hello` builds the `hello` package provided by
  the `nixpkgs` flake listed in the registry. `nix` will automatically
  download/upload the registry and flakes as needed.

* `nix build` without arguments will build the flake in the current
  directory (or some parent).

* `nix flake update` generates `flake.lock` from `flake.nix`, ignoring the old
  lockfile.

* `nixos-rebuild` will build a configuration from a (locked) flake. Evaluation
  is done in pure mode to ensure there are no unaccounted inputs. Thus the
  NixOS configuration can be reproduced unambiguously from the top-level flake.

* Nix code can query flake metadata such as `commitHash` (the Git revision) or
  `epoch` (the date of the last commit). This is useful for NixOS to compute
  the NixOS version string (which will be the revision of the top-level
  configuration flake, uniquely identifying the configuration).

* Hydra jobset configurations will consist of a single flake reference. Thus we
  can get rid of jobset inputs; any other needed repositories can be fetched by
  the top-level flake. The top-level flake can be locked or unlocked; if some
  dependencies are unlocked, then Nix will fetch the latest revision for each.


## Example flake

Let us look at an example of a `flake.nix` file, here for `dwarffs`, a small
repository that provides a single package and a single NixOS module.

```nix
{
  # The flake identifier.
  name = "dwarffs";

  # The epoch may be used in the future to determine how Nix
  # expressions inside this flake are to be parsed.
  epoch = 201906;

  # Some other metadata.
  description = "A filesystem that fetches DWARF debug info from the Internet on demand";

  # The flake dependencies. Nix will resolve and fetch these flakes and pass
  # them as a function argument to `outputs` below.
  #
  # "nixpkgs" denotes a flake named `nixpkgs` which is looked up
  # in the flake registry, or in `flake.lock` inside this flake, if it
  # exists.
  inputs = [ flake:nixpkgs ];

  # An attribute set listing dependencies which aren't flakes, also to be passed as
  # a function argument to `provides`.
  nonFlakeRequires = {};

  # The stuff provided by this flake. Flakes can provide whatever they
  # want (convention over configuration), but some attributes have
  # special meaning to tools / other flakes. For example, `packages`
  # is used by the `nix` CLI to search for packages, and
  # `nixosModules` is used by NixOS to automatically pull in the
  # modules provided by a flake.
  #
  # `outputs` takes a single argument (`deps`) that contains
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
    hydraJobs.build.x86_64-linux = packages.dwarffs;

    # A bunch of things which can be checked (through `nix flake check`) to
    # make sure the flake is well-defined.
    checks.build = packages.dwarffs;
  };
}
```

Similarly, a minimal `flake.nix` for Nixpkgs:

```nix
{
  name = "nixpkgs";

  epoch = 201906;

  description = "A collection of packages for the Nix package manager";

  outputs = deps:
    let pkgs = import ./. {}; in
    let pkgs = import ./. { system = "x86_64-linux"; }; in
    {
      lib = (import ./lib) // {
        nixosSystem = import ./nixos/lib/eval-config.nix;
      };

      builders = {
        inherit (pkgs) stdenv fetchurl;
      };

      packages = {
        inherit (pkgs) hello nix fuse nlohmann_json boost;
      };

      legacyPkgs = pkgs;
    };
}
```
Note that `packages` is an unpolluted set of packages: non-package values like
`lib` or `fetchurl` are not part of it.

## Flake registries

Note: If a flake registry contains an entry `nixpkgs -> github:NixOS/nixpkgs`,
then `nixpkgs/release-18.09` will match to become
`github:NixOS/nixpkgs/release-18.09`. This is referred to as "fuzzymatching".


## Flake references

Flake references are a URI-like syntax to specify the physical location of a
flake (e.g. a Git repository) or to denote a lookup in the flake registry or
lock file. There are four options for the syntax:

* Flake aliases
  A flake alias is a name which requires a lookup in a flake
  registry or lock file.

  Example: "nixpkgs"

* GitHub repositories
  A repository which is stored on GitHub can easily be fetched using this type.
  Note:
  * Only the code in this particular commit is downloaded, not the entire repo
  * By default, the commit to download is the last commit on the `master` branch.
    See later for how to change this.

  Example: `github:NixOS/nixpkgs`

* `ssh/https/git/file`
  These are generic `FlakeRef`s for downloadding git repositories or tarballs.

  Examples:
  - https://example.org/my/repo.git
  - ssh://git@github.com:NixOS/nix.git
  - git://github.com/edolstra/dwarffs.git
  - file:///home/my-user/some-repo/some-repo.git
  - https://releases.nixos.org/nixos/unstable/nixos-19.03pre167858.f2a1a4e93be/nixexprs.tar.xz
  - file:///<path>.tar.xz

* Local, dirty paths
  This `FlakeRef` is the equivalent of `file://<path>` used for dirty paths.

  Example: /path/to/my/repo

Notes:
- Each FlakeRef (except for the Path option) allows for a Git revision (i.e.
  commit hash) and/or referenceo(i.e. git branch name) to be added. For
  tarbals, an SRI hash needs to be added.
  Examples:
  * `"nixpkgs/release-18.09"`
  * `github:NixOS/nixpkgs/1e9e709953e315ab004951248b186ac8e2306451`
  * `git://github.com/edolstra/dwarffs.git?ref=flake&rev=2efca4bc9da70fb001b26c3dc858c6397d3c4817`
  * file:///<path>.tar.xz(?hash=<sri-hash>)
- In full pure mode, no mutable `FlakeRef`s can be used
  * No aliases, because they need to be looked up
  * `github` requires a specified `rev`
  * `ssh/https/git/file` require a specified `ref` _and_ `rev`
  * `path` is always mutable
- Flakes don't need to be top-level, but can also reside in a subdirectory. This is shown by adding `dir=<subdir>` to the `FlakeRef`.
  Example: `./foo?dir=bar`


## Flake lock files

A lockfile is a JSON file named `flake.lock` which contains a forrest of
entries mapping `FlakeRef`s to the immutable `FlakeRef` they were resolved to.

Example:

```json
{
  "nixpkgs": {
    "uri": "github:NixOS/nixpkgs/41c0c1bf292ea3ac3858ff393b49ca1123dbd553",
    "content-hash": "sha256-vy2UmXQM66aS/Kn2tCtjt9RwxfBvV+nQVb5tJQFwi8E="
  },
  "foo": {
    "uri": "https://example.org/foo.tar.xz?hash=sha256-56bbc099995ea8581ead78f22832fee7dbcb0a0b6319293d8c2d0aef5379397c",
    "content-hash": "sha256-vy2UmXQM66aS/Kn2tCtjt9RwxfBvV+nQVb5tJQFwi8E="
  }
}
```

Lockfiles are used to help resolve the dependencies of a flake.
- `nix build github:<..>` uses the remote lockfile and update it
- `nix build /home/user/dwarffs` uses the local lockfile, updates it and writes the result to file
- `nix flake update <flakeref>` recreates the lockfile from scratch and writes it to file
- `--no-registries` makes the command pure, also when fetching dependencies
- `--no-save-lock-file`: Several commands will update the lockfile (e.g. `nix
  build`). This flag prevents the updated lockfile to be written to file.
- `--recreate-lock-file` makes prevents the current lockfile from being used

## `outputs`

The function argument `deps` is an attrset containing all dependencies listed
in `requires` and `nonFlakeRequires` as well as `path` (for the flake's source
code) and an attribute `meta` with:
- `description`
- `commitHash` (not for tarball flakes): The Git commit hash.
- `date`: The timestamp of the most recent commit (for Git repos), or of the
  most recently modified file (for tarballs)
- `revCount` (for Git flakes, but not GitHub flakes): The number of ancestors
  of the revision. Useful for generating version strings.

The flake attribute `outputs` is a function that takes an argument named `deps`
and returns an attribute set. Some of the members of this set have protected
names:

* `packages`: A set of installable derivations used by the `nix` command. That
  is, commands such as `nix install` ignore all other flake attributes. It
  cannot be a nested set.

* `hydraJobs`: Used by Hydra.

* `nixosModules`: An attrset of NixOS modules.

* `nixosSystems`: An attrset of calls to `evalModules`, i.e. things
  that `nixos-rebuild` can switch to. (Maybe this is superfluous, but
  we need to avoid a situation where `nixos-rebuild` needs to fetch
  its own `nixpkgs` just to do `evalModules`.)

* `devShell`: A derivation to create a development environment

* `self`: The result of the flake's output which is passed to itself
  Example: `self.outputs.foo` works.


## Flake registry

A flake registry is a JSON file mapping flake references to flake references.
The default/global registry is kept at
`https://github.com/NixOS/flake-registry/blob/master/flake-registry.json` and
looks like this:

```json
{
    "flakes": {
        "dwarffs": {
            "uri": "github:edolstra/dwarffs/flake"
        },
        "nix": {
            "uri": "github:NixOS/nix/flakes"
        },
        "nixpkgs": {
            "uri": "github:edolstra/nixpkgs/release-19.03"
        },
        "hydra": {
            "uri": "github:NixOS/hydra/flake"
        },
        "patchelf": {
            "uri": "github:NixOS/patchelf"
        }
    },
    "version": 1
}
```

Nix automatically (re)downloads this file whenever you have network access. The
downloaded file is a GC root so the registry remains available if nixos.org is
unreachable.

In addition to a global registry, there is also a user registry stored in
`~/.config/nix/registry.json`.


## Nix UI

There is a list of new commands added to the `nix` CLI:

* `nix flake list`: Show all flakes in the registry

* `nix flake add <alias FlakeRef> <resolved FlakeRef>`: Add or override a flake
  to/in the user flake registry.

* `nix flake remove <alias FlakeRef>`: Remove a FlakeRef from the user flake
  registry.

* `nix flake pin <alias FlakeRef>`: Look up to which immutable FlakeRef the
  alias FlakeRef maps to currently, and store that map in the user registry.
  Example: `nix flake pin github:NixOS/nixpkgs` will create an entry
  `github:NixOS/nixpkgs ->
  github:NixOS/nixpkgs/444f22ca892a873f76acd88d5d55bdc24ed08757`.

* `nix flake init`: Create a `flake.nix` in the current directory

* `nix flake update`: Recreate the lock file from scratch, from the `flake.nix`.

* `nix flake check`: Do some checks on the flake, e.g. check that all
  `packages` are really packages.

* `nix flake clone`: `git clone` the flake repo

Flags / configuration options:

* `--flakes (<alias FlakeRef>=<resolved FlakeRef>)*`: add/override some
  FlakeRef

* `--flake <flake-ref>`: set the specified flake as the installation source
  E.g. `nix build --flake ./my-nixpkgs hello`.

The default installation source in `nix` is the `packages` from all flakes in
the registry, that is:
```
builtins.mapAttrs (flakeName: flakeInfo:
  (getFlake flakeInfo.uri).${flakeName}.outputs.packages or {})
  builtins.flakeRegistry
```
where `builtins.flakeRegistry` is the global registry with user overrides
applied, and `builtins.getFlake` downloads a flake and resolves its
dependencies.


## Pure evaluation and caching

Flake evaluation is done in pure mode. Thus:

* Flakes cannot use `NIX_PATH` via the `<...>` syntax.

* Flakes cannot read random stuff from non-flake directories, such as
  `~/.nix/config.nix` or overlays.

This enables aggressive caching or precomputation of Nixpkgs package sets. For
example, for a particular Nixpkgs flake closure (as identified by, say, a hash
of the fully-qualified flake references after dependency resolution) and system
type, an attribute like `packages.hello` should always evaluate to the same
derivation. So we can:

* Keep a local evaluation cache (say `~/.cache/nix/eval-cache-v1.sqlite`)
  mapping `(<flake-closure-hash, <attribute>) -> (<drv-name>,
  <drv-output-paths>, <whatever other info we want to cache>)`.

* Download a precomputed cache, e.g.
  `https://releases.nixos.org/eval/<flake-closure-hash>.sqlite`. So a command
  like `nix search` could avoid evaluating Nixpkgs entirely.

Of course, this doesn't allow overlays. With pure evaluation, the only way to
have these is to define a top-level flake that depends on the Nixpkgs flake and
somehow passes in a set of overlays.


## Hydra jobset dependencies

Hydra can use the flake dependency resolution mechanism to fetch dependencies.
This allows us to get rid of jobset configuration in the web interface: a
jobset only requires a flake reference. That is, a jobset *is* a flake. Hydra
then just builds the `hydraJobs` attrset


## NixOS system configuration

NixOS currently contains a lot of modules that really should be moved into
their own repositories. For example, it contains a Hydra module that duplicates
the one in the Hydra repository. Also, we want reproducible evaluation for
NixOS system configurations. So NixOS system configurations should be stored as
flakes in (local) Git repositories.

`my-system/flake.nix`:
```nix
{
  name = "my-system";

  epoch = 201906;

  inputs =
    [ "nixpkgs/nixos-18.09"
      "dwarffs"
      "hydra"
      ... lots of other module flakes ...
    ];

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
}
```

We can then build the system:
```
nixos-rebuild switch --flake ~/my-system
```
This performs dependency resolution starting at `~/my-system/flake.nix` and
builds the `system` attribute in `nixosSystems.default`.
