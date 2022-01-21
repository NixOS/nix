R""(

# Description

`nix flake` provides subcommands for creating, modifying and querying
*Nix flakes*. Flakes are the unit for packaging Nix code in a
reproducible and discoverable way. They can have dependencies on other
flakes, making it possible to have multi-repository Nix projects.

A flake is a filesystem tree (typically fetched from a Git repository
or a tarball) that contains a file named `flake.nix` in the root
directory. `flake.nix` specifies some metadata about the flake such as
dependencies (called *inputs*), as well as its *outputs* (the Nix
values such as packages or NixOS modules provided by the flake).

# Flake references

Flake references (*flakerefs*) are a way to specify the location of a
flake. These have two different forms:

* An attribute set representation, e.g.

  ```nix
  {
    type = "github";
    owner = "NixOS";
    repo = "nixpkgs";
  }
  ```

  The only required attribute is `type`. The supported types are
  listed below.

* A URL-like syntax, e.g.

  ```
  github:NixOS/nixpkgs
  ```

  These are used on the command line as a more convenient alternative
  to the attribute set representation. For instance, in the command

  ```console
  # nix build github:NixOS/nixpkgs#hello
  ```

  `github:NixOS/nixpkgs` is a flake reference (while `hello` is an
  output attribute). They are also allowed in the `inputs` attribute
  of a flake, e.g.

  ```nix
  inputs.nixpkgs.url = github:NixOS/nixpkgs;
  ```

  is equivalent to

  ```nix
  inputs.nixpkgs = {
    type = "github";
    owner = "NixOS";
    repo = "nixpkgs";
  };
  ```

## Examples

Here are some examples of flake references in their URL-like representation:

* `.`: The flake in the current directory.
* `/home/alice/src/patchelf`: A flake in some other directory.
* `nixpkgs`: The `nixpkgs` entry in the flake registry.
* `nixpkgs/a3a3dda3bacf61e8a39258a0ed9c924eeca8e293`: The `nixpkgs`
  entry in the flake registry, with its Git revision overridden to a
  specific value.
* `github:NixOS/nixpkgs`: The `master` branch of the `NixOS/nixpkgs`
  repository on GitHub.
* `github:NixOS/nixpkgs/nixos-20.09`: The `nixos-20.09` branch of the
  `nixpkgs` repository.
* `github:NixOS/nixpkgs/a3a3dda3bacf61e8a39258a0ed9c924eeca8e293`: A
  specific revision of the `nixpkgs` repository.
* `github:edolstra/nix-warez?dir=blender`: A flake in a subdirectory
  of a GitHub repository.
* `git+https://github.com/NixOS/patchelf`: A Git repository.
* `git+https://github.com/NixOS/patchelf?ref=master`: A specific
  branch of a Git repository.
* `git+https://github.com/NixOS/patchelf?ref=master&rev=f34751b88bd07d7f44f5cd3200fb4122bf916c7e`:
  A specific branch *and* revision of a Git repository.
* `https://github.com/NixOS/patchelf/archive/master.tar.gz`: A tarball
  flake.

## Flake reference attributes

The following generic flake reference attributes are supported:

* `dir`: The subdirectory of the flake in which `flake.nix` is
  located. This parameter enables having multiple flakes in a
  repository or tarball. The default is the root directory of the
  flake.

* `narHash`: The hash of the NAR serialisation (in SRI format) of the
  contents of the flake. This is useful for flake types such as
  tarballs that lack a unique content identifier such as a Git commit
  hash.

In addition, the following attributes are common to several flake
reference types:

* `rev`: A Git or Mercurial commit hash.

* `ref`: A Git or Mercurial branch or tag name.

Finally, some attribute are typically not specified by the user, but
can occur in *locked* flake references and are available to Nix code:

* `revCount`: The number of ancestors of the commit `rev`.

* `lastModified`: The timestamp (in seconds since the Unix epoch) of
  the last modification of this version of the flake. For
  Git/Mercurial flakes, this is the commit time of commit *rev*, while
  for tarball flakes, it's the most recent timestamp of any file
  inside the tarball.

## Types

Currently the `type` attribute can be one of the following:

* `path`: arbitrary local directories, or local Git trees. The
  required attribute `path` specifies the path of the flake. The URL
  form is

  ```
  [path:]<path>(\?<params)?
  ```

  where *path* is an absolute path.

  *path* must be a directory in the file system containing a file
  named `flake.nix`.

  If the directory or any of its parents is a Git repository, then
  this is essentially equivalent to `git+file://<path>` (see below),
  except that the `dir` parameter is derived automatically. For
  example, if `/foo/bar` is a Git repository, then the flake reference
  `/foo/bar/flake` is equivalent to `/foo/bar?dir=flake`.

  If the directory is not inside a Git repository, then the flake
  contents is the entire contents of *path*.

  *path* generally must be an absolute path. However, on the command
  line, it can be a relative path (e.g. `.` or `./foo`) which is
  interpreted as relative to the current directory. In this case, it
  must start with `.` to avoid ambiguity with registry lookups
  (e.g. `nixpkgs` is a registry lookup; `./nixpkgs` is a relative
  path).

* `git`: Git repositories. The location of the repository is specified
  by the attribute `url`.

  They have the URL form

  ```
  git(+http|+https|+ssh|+git|+file|):(//<server>)?<path>(\?<params>)?
  ```

  The `ref` attribute defaults to `master`.

  The `rev` attribute must denote a commit that exists in the branch
  or tag specified by the `ref` attribute, since Nix doesn't do a full
  clone of the remote repository by default (and the Git protocol
  doesn't allow fetching a `rev` without a known `ref`). The default
  is the commit currently pointed to by `ref`.

  For example, the following are valid Git flake references:

  * `git+https://example.org/my/repo`
  * `git+https://example.org/my/repo?dir=flake1`
  * `git+ssh://git@github.com/NixOS/nix?ref=v1.2.3`
  * `git://github.com/edolstra/dwarffs?ref=unstable&rev=e486d8d40e626a20e06d792db8cc5ac5aba9a5b4`
  * `git+file:///home/my-user/some-repo/some-repo`

* `mercurial`: Mercurial repositories. The URL form is similar to the
  `git` type, except that the URL schema must be one of `hg+http`,
  `hg+https`, `hg+ssh` or `hg+file`.

* `tarball`: Tarballs. The location of the tarball is specified by the
  attribute `url`.

  In URL form, the schema must be `http://`, `https://` or `file://`
  URLs and the extension must be `.zip`, `.tar`, `.tar.gz`, `.tar.xz`,
  `.tar.bz2` or `.tar.zst`.

* `github`: A more efficient way to fetch repositories from
  GitHub. The following attributes are required:

  * `owner`: The owner of the repository.

  * `repo`: The name of the repository.

  These are downloaded as tarball archives, rather than
  through Git. This is often much faster and uses less disk space
  since it doesn't require fetching the entire history of the
  repository. On the other hand, it doesn't allow incremental fetching
  (but full downloads are often faster than incremental fetches!).

  The URL syntax for `github` flakes is:

  ```
  github:<owner>/<repo>(/<rev-or-ref>)?(\?<params>)?
  ```

  `<rev-or-ref>` specifies the name of a branch or tag (`ref`), or a
  commit hash (`rev`). Note that unlike Git, GitHub allows fetching by
  commit hash without specifying a branch or tag.

  Some examples:

  * `github:edolstra/dwarffs`
  * `github:edolstra/dwarffs/unstable`
  * `github:edolstra/dwarffs/d3f2baba8f425779026c6ec04021b2e927f61e31`

* `indirect`: Indirections through the flake registry. These have the
  form

  ```
  [flake:]<flake-id>(/<rev-or-ref>(/rev)?)?
  ```

  These perform a lookup of `<flake-id>` in the flake registry. For
  example, `nixpkgs` and `nixpkgs/release-20.09` are indirect flake
  references. The specified `rev` and/or `ref` are merged with the
  entry in the registry; see [nix registry](./nix3-registry.md) for
  details.

# Flake format

As an example, here is a simple `flake.nix` that depends on the
Nixpkgs flake and provides a single package (i.e. an installable
derivation):

```nix
{
  description = "A flake for building Hello World";

  inputs.nixpkgs.url = github:NixOS/nixpkgs/nixos-20.03;

  outputs = { self, nixpkgs }: {

    defaultPackage.x86_64-linux =
      # Notice the reference to nixpkgs here.
      with import nixpkgs { system = "x86_64-linux"; };
      stdenv.mkDerivation {
        name = "hello";
        src = self;
        buildPhase = "gcc -o hello ./hello.c";
        installPhase = "mkdir -p $out/bin; install -t $out/bin hello";
      };

  };
}
```

The following attributes are supported in `flake.nix`:

* `description`: A short, one-line description of the flake.

* `inputs`: An attrset specifying the dependencies of the flake
  (described below).

* `outputs`: A function that, given an attribute set containing the
  outputs of each of the input flakes keyed by their identifier,
  yields the Nix values provided by this flake. Thus, in the example
  above, `inputs.nixpkgs` contains the result of the call to the
  `outputs` function of the `nixpkgs` flake.

  In addition to the outputs of each input, each input in `inputs`
  also contains some metadata about the inputs. These are:

  * `outPath`: The path in the Nix store of the flake's source tree.

  * `rev`: The commit hash of the flake's repository, if applicable.

  * `revCount`: The number of ancestors of the revision `rev`. This is
    not available for `github` repositories, since they're fetched as
    tarballs rather than as Git repositories.

  * `lastModifiedDate`: The commit time of the revision `rev`, in the
    format `%Y%m%d%H%M%S` (e.g. `20181231100934`). Unlike `revCount`,
    this is available for both Git and GitHub repositories, so it's
    useful for generating (hopefully) monotonically increasing version
    strings.

  * `lastModified`: The commit time of the revision `rev` as an integer
    denoting the number of seconds since 1970.

  * `narHash`: The SHA-256 (in SRI format) of the NAR serialization of
    the flake's source tree.

  The value returned by the `outputs` function must be an attribute
  set. The attributes can have arbitrary values; however, various
  `nix` subcommands require specific attributes to have a specific
  value (e.g. `packages.x86_64-linux` must be an attribute set of
  derivations built for the `x86_64-linux` platform).

* `nixConfig`: An attribute set specifying configuration options for nix the
  same as you would set in [`nix.conf`](./how-do-i-reference/conf-file.md).
  These options will be enabled when building outputs from this flake. Options
  that may compromise security must be approved by the user.

## Flake inputs

The attribute `inputs` specifies the dependencies of a flake, as an
attrset mapping input names to flake references. For example, the
following specifies a dependency on the `nixpkgs` and `import-cargo`
repositories:

```nix
# A GitHub repository.
inputs.import-cargo = {
  type = "github";
  owner = "edolstra";
  repo = "import-cargo";
};

# An indirection through the flake registry.
inputs.nixpkgs = {
  type = "indirect";
  id = "nixpkgs";
};
```

Alternatively, you can use the URL-like syntax:

```nix
inputs.import-cargo.url = github:edolstra/import-cargo;
inputs.nixpkgs.url = "nixpkgs";
```

Each input is fetched, evaluated and passed to the `outputs` function
as a set of attributes with the same name as the corresponding
input. The special input named `self` refers to the outputs and source
tree of *this* flake. Thus, a typical `outputs` function looks like
this:

```nix
outputs = { self, nixpkgs, import-cargo }: {
  ... outputs ...
};
```

It is also possible to omit an input entirely and *only* list it as
expected function argument to `outputs`. Thus,

```nix
outputs = { self, nixpkgs }: ...;
```

without an `inputs.nixpkgs` attribute is equivalent to

```nix
inputs.nixpkgs = {
  type = "indirect";
  id = "nixpkgs";
};
```

Repositories that don't contain a `flake.nix` can also be used as
inputs, by setting the input's `flake` attribute to `false`:

```nix
inputs.grcov = {
  type = "github";
  owner = "mozilla";
  repo = "grcov";
  flake = false;
};

outputs = { self, nixpkgs, grcov }: {
  packages.x86_64-linux.grcov = stdenv.mkDerivation {
    src = grcov;
    ...
  };
};
```

Transitive inputs can be overridden from a `flake.nix` file. For
example, the following overrides the `nixpkgs` input of the `nixops`
input:

```nix
inputs.nixops.inputs.nixpkgs = {
  type = "github";
  owner = "my-org";
  repo = "nixpkgs";
};
```

It is also possible to "inherit" an input from another input. This is
useful to minimize flake dependencies. For example, the following sets
the `nixpkgs` input of the top-level flake to be equal to the
`nixpkgs` input of the `dwarffs` input of the top-level flake:

```nix
inputs.nixpkgs.follows = "dwarffs/nixpkgs";
```

The value of the `follows` attribute is a `/`-separated sequence of
input names denoting the path of inputs to be followed from the root
flake.

Overrides and `follows` can be combined, e.g.

```nix
inputs.nixops.inputs.nixpkgs.follows = "dwarffs/nixpkgs";
```

sets the `nixpkgs` input of `nixops` to be the same as the `nixpkgs`
input of `dwarffs`. It is worth noting, however, that it is generally
not useful to eliminate transitive `nixpkgs` flake inputs in this
way. Most flakes provide their functionality through Nixpkgs overlays
or NixOS modules, which are composed into the top-level flake's
`nixpkgs` input; so their own `nixpkgs` input is usually irrelevant.

## Flake outputs

While the technical specification of a flake does not include any details about
the outputs, there are some common conventions, and tools that expect certain
outputs. These are documented here, including how they are typically used,
whether they are recognized by [`nix flake check`](./flake-check.md), and what
type they should be.

### checks
Used-by: `nix flake check`
Checked: Yes
Type: systemAttrs (attrsOf (derivation))

Additional checks that get run by `nix flake check`. These can include lints,
NixOS tests, and anything else that makes sure the flake is correct. The
`checks` attribute is an attrset which maps system architectures to the package
derivations for the tests to run on that system.

### packages
Used-by: `nix build`, `nix run`
Checked: Yes
Type: systemAttrs (attrsOf derivation)

Packages allows you to specify multiple named packages. They can be built with
`nix build <flake>#<package-name>`. The `packages` attribute is an attrset which
maps system architecture to the package derivations defined for that system.

See [`defaultPackage`](#defaultPackage) for marking one package as default.

### defaultPackage
Used-by `nix build`, `nix run`
Checked: Yes
Type: systemAttrs derivation

The package for `nix build` to build when no specific package is specified at
the command line. It takes the form of a set mapping system architecture to the
default package derivation for that system. (e.g.  `defaultPackage.x86_64 =
pkgs.callPackage ./package.nix;`)

The [`flake-utils`](https://github.com/numtide/flake-utils) function
`eachDefaultSystem` is often used to generate an output for a set of common
systems.

See [`packages`](#packages) if you would like to expose multiple packages.

### apps
Used-by: `nix run`
Checked: Yes
Type: systemAttrs (attrsOf {type; program;})

Easy access to certain apps or commands. Takes the form of a set mapping system
architecture to the apps available on that system. For now `type` must be
`"app"`, and `program` is the path to an executable in a nix package.

See [`defaultApp`](#defaultApp) if you would like to mark one app as defaault.

### defaultApp
Used-by: `nix run`
Checked: Yes
Type: attrsof {type; program;}

The app for `nix run` to run when no specific app is specified at the command
line. It takes the form of a set mapping system architecture to the
default app for that system. For now `type` must be `"app"`, and `program` is
the path to an executable in a nix package.

See [`apps`](#apps) if you would like to expose multiple apps.

### legacyPackages
Used-by: `nix build`
Checked: Yes
type: systemAttrs (attrsOf^N derivation)

`legacyPackages` is just like [`packages`](#packages), except that packages do
not need to be at the top level. There can be nested attrsets that have packages
at the leaves. Where possible, `packages` is preferred, as it's more efficient
for some purposes.

### overlay
Used-by: Downstream flakes
Checked: Yes
Type: Overlay (final: prev: package set)

Overlay specifies the default overlay for this Flake. It can be used to add
packages to a nixpkgs set with `import <nixpkgs> { overlays = [ self.overlay ];}`.

If it is specified in the flake file itself, `nix flake check` requires the
function arguments to be named `final` and `prev` (rather than, for example, the
previous convention of `self` and `super` ).

See [`overlays`](#overlays) for when you need multiple overlays.

### overlays
Used-by: Downstream flakes
Checked: Yes
Type: attrsOf Overlay

Overlays allows you to specify multiple named overlays. It can be used to add
packages to a nixpkgs set in two ways.  First, a single overlay can be selected:
`import <nixpkgs> { overlays = [ self.overlay.foo ]; }` Second, all overlays can
be applied: `import <nixpkgs> { overlays = builtins.attrValues self.overlays }`

If the overlays should only ever be applied together, and/or if the order
they're applied in is important, one can use the [`overlay`](#overlay) output
and join the overlays with `lib.composeExtensions` or
`lib.composeManyExtensions`.

See [`overlay`](#overlay) for when you only need one overlay, or want to
designate a default.

### nixosModule
Used-by: Downstream flakes
Checked: Yes
Type: NixOS Module

A module for use in NixOS configurations by adding it to the
`imports` attribute.

See [`nixosModules`](#nixosModules) for when you want to provide multiple
modules.

### nixosModules
Used-by: Downstream flakes
Checked: Yes
Type: attrsOf (NixOS Module)

An attrset of NixOS modules, for use in NixOS configurations.

See [`nixosModule`](#nixosModule) for when you have only one, or a default
module to provide.

### nixosConfigurations
Used-by: `nixos-rebuild`
Checked: Yes
Type: NixOS Module

An attrset mapping hostnames to nixos configurations. Call `nixos-rebuild` with
the `--flake <URI>` argument to rebuild with the
`nixosConfigurations.<current hostname>` configuration. A specific hostname can
also be specified by `--flake <URI>.<hostname>`.

### hmModule
Used-by: home-manager
Checked: No
Type: Home-manager Module

A module for use in
[home-manager](https://github.com/nix-community/home-manager) configurations by
adding it to the `imports` attribute.

See [`hmModules`](#hmModules) for when you want to provide multiple modules.

### hmModules
Used-by: home-manager
Checked: No
Type: attrsOf (Home-manager Module)

An attrset of home-manager modules, for use in home-manager configurations.

See [`hmModule`](#hmModule) for when you have only one, or a default module to
provide.

### defaultBundler
Used-by: `nix bundle`
Checked: Yes
Type: Bundler

left blank until #5456 is resolved

### bundlers
Used-by: `nix bundle`
Checked: Yes
Type: attrsOf Bundler

left blank until #5456 is resolved

### devShell
Used-by: `nix develop`
Checked: Yes
Type: systemAttrs derivation

A shell derivation (created with functions like `nixpkgs#mkShell`) to set up the
environment for hacking on the flake.

See [`devShells`](#devShells) for when you want to provide multiple development
environments.

### devShells
Used-by: `nix develop`
Checked: Yes
Type: systemAttrs (attrsOf derivation)

Shell derivations (created with functions like `nixpkgs#mkShell`) to set up the
environments for hacking on the flake.

See [`devShell`](#devShell) for when you only need to provide one, or want to
provide a default.

### hydraJobs
Used-by: Hydra
Checked: Yes
Type: systemAttrs (attrsOf (???))

???

### defaultTemplate
Used-by: `nix flake init`
Checked: Yes
Type: {path; description;}

A default starting point for developing another flake. This is meant to
communicate the intended way to depend on a flake, and/or provide a quick
starting point for developers. Running `nix-flake -t <flake>` will copy the
contents specified by the `path` attribute into the current directory.

To specify multiple templates, use [`templates`](#templates)

### templates
Used-by: `nix flake init`
Checked: Yes
Type: attrsOf {path; description;}

Starting points for developing another flake. This is meant to communicate the
intended ways to depend on a flake, and/or provide a quick starting point for
developers. Running `nix-flake -t <flake>#<template>` will copy the contents
specified by the `path` attribute into the current directory.

If there is one primary template, use [`defaultTemplate`](#defaulttemplate).

### lib
Used-by: Downstream flakes
Checked: No
Type: attrset

The `lib` output is typically the right location for functions designed to be
used in downstream flakes, except where they have a specific use that fits more
in another category (for instance a [bundler](#bundler))

# Lock files

Inputs specified in `flake.nix` are typically "unlocked" in the sense
that they don't specify an exact revision. To ensure reproducibility,
Nix will automatically generate and use a *lock file* called
`flake.lock` in the flake's directory. The lock file contains a graph
structure isomorphic to the graph of dependencies of the root
flake. Each node in the graph (except the root node) maps the
(usually) unlocked input specifications in `flake.nix` to locked input
specifications. Each node also contains some metadata, such as the
dependencies (outgoing edges) of the node.

For example, if `flake.nix` has the inputs in the example above, then
the resulting lock file might be:

```json
{
  "version": 7,
  "root": "n1",
  "nodes": {
    "n1": {
      "inputs": {
        "nixpkgs": "n2",
        "import-cargo": "n3",
        "grcov": "n4"
      }
    },
    "n2": {
      "inputs": {},
      "locked": {
        "owner": "edolstra",
        "repo": "nixpkgs",
        "rev": "7f8d4b088e2df7fdb6b513bc2d6941f1d422a013",
        "type": "github",
        "lastModified": 1580555482,
        "narHash": "sha256-OnpEWzNxF/AU4KlqBXM2s5PWvfI5/BS6xQrPvkF5tO8="
      },
      "original": {
        "id": "nixpkgs",
        "type": "indirect"
      }
    },
    "n3": {
      "inputs": {},
      "locked": {
        "owner": "edolstra",
        "repo": "import-cargo",
        "rev": "8abf7b3a8cbe1c8a885391f826357a74d382a422",
        "type": "github",
        "lastModified": 1567183309,
        "narHash": "sha256-wIXWOpX9rRjK5NDsL6WzuuBJl2R0kUCnlpZUrASykSc="
      },
      "original": {
        "owner": "edolstra",
        "repo": "import-cargo",
        "type": "github"
      }
    },
    "n4": {
      "inputs": {},
      "locked": {
        "owner": "mozilla",
        "repo": "grcov",
        "rev": "989a84bb29e95e392589c4e73c29189fd69a1d4e",
        "type": "github",
        "lastModified": 1580729070,
        "narHash": "sha256-235uMxYlHxJ5y92EXZWAYEsEb6mm+b069GAd+BOIOxI="
      },
      "original": {
        "owner": "mozilla",
        "repo": "grcov",
        "type": "github"
      },
      "flake": false
    }
  }
}
```

This graph has 4 nodes: the root flake, and its 3 dependencies. The
nodes have arbitrary labels (e.g. `n1`). The label of the root node of
the graph is specified by the `root` attribute. Nodes contain the
following fields:

* `inputs`: The dependencies of this node, as a mapping from input
  names (e.g. `nixpkgs`) to node labels (e.g. `n2`).

* `original`: The original input specification from `flake.lock`, as a
  set of `builtins.fetchTree` arguments.

* `locked`: The locked input specification, as a set of
  `builtins.fetchTree` arguments. Thus, in the example above, when we
  build this flake, the input `nixpkgs` is mapped to revision
  `7f8d4b088e2df7fdb6b513bc2d6941f1d422a013` of the `edolstra/nixpkgs`
  repository on GitHub.

  It also includes the attribute `narHash`, specifying the expected
  contents of the tree in the Nix store (as computed by `nix
  hash-path`), and may include input-type-specific attributes such as
  the `lastModified` or `revCount`. The main reason for these
  attributes is to allow flake inputs to be substituted from a binary
  cache: `narHash` allows the store path to be computed, while the
  other attributes are necessary because they provide information not
  stored in the store path.

* `flake`: A Boolean denoting whether this is a flake or non-flake
  dependency. Corresponds to the `flake` attribute in the `inputs`
  attribute in `flake.nix`.

The `original` and `locked` attributes are omitted for the root
node. This is because we cannot record the commit hash or content hash
of the root flake, since modifying `flake.lock` will invalidate these.

The graph representation of lock files allows circular dependencies
between flakes. For example, here are two flakes that reference each
other:

```nix
{
  inputs.b = ... location of flake B ...;
  # Tell the 'b' flake not to fetch 'a' again, to ensure its 'a' is
  # *this* 'a'.
  inputs.b.inputs.a.follows = "";
  outputs = { self, b }: {
    foo = 123 + b.bar;
    xyzzy = 1000;
  };
}
```

and

```nix
{
  inputs.a = ... location of flake A ...;
  inputs.a.inputs.b.follows = "";
  outputs = { self, a }: {
    bar = 456 + a.xyzzy;
  };
}
```

Lock files transitively lock direct as well as indirect
dependencies. That is, if a lock file exists and is up to date, Nix
will not look at the lock files of dependencies. However, lock file
generation itself *does* use the lock files of dependencies by
default.

)""
