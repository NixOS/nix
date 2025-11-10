# Name

`nix-env --upgrade` - upgrade packages in user environment

# Synopsis

`nix-env` {`--upgrade` | `-u`} *args*
  [`--lt` | `--leq` | `--eq` | `--always`]
  [{`--prebuilt-only` | `-b`}]
  [{`--attr` | `-A`}]
  [`--from-expression`] [`-E`]
  [`--from-profile` *path*]
  [`--preserve-installed` | `-P`]

# Description

The upgrade operation creates a new user environment, based on the
current generation of the active profile, in which all store paths are
replaced for which there are newer versions in the set of paths
described by *args*. Paths for which there are no newer versions are
left untouched; this is not an error. It is also not an error if an
element of *args* matches no installed derivations.

For a description of how *args* is mapped to a set of store paths, see
[`--install`](./install.md). If *args* describes multiple
store paths with the same symbolic name, only the one with the highest
version is installed.

# Flags

- `--lt`

  Only upgrade a derivation to newer versions. This is the default.

- `--leq`

  In addition to upgrading to newer versions, also “upgrade” to
  derivations that have the same version. Version are not a unique
  identification of a derivation, so there may be many derivations
  that have the same version. This flag may be useful to force
  “synchronisation” between the installed and available derivations.

- `--eq`

  *Only* “upgrade” to derivations that have the same version. This may
  not seem very useful, but it actually is, e.g., when there is a new
  release of Nixpkgs and you want to replace installed applications
  with the same versions built against newer dependencies (to reduce
  the number of dependencies floating around on your system).

- `--always`

  In addition to upgrading to newer versions, also “upgrade” to
  derivations that have the same or a lower version. I.e., derivations
  may actually be downgraded depending on what is available in the
  active Nix expression.

- `--prebuilt-only` / `-b`

  Use only derivations for which a substitute is registered, i.e.,
  there is a pre-built binary available that can be downloaded in lieu
  of building the derivation. Thus, no packages will be built from
  source.

- `--preserve-installed` / `-P`

  Do not remove derivations with a name matching one of the
  derivations being installed. Usually, trying to have two versions of
  the same package installed in the same generation of a profile will
  lead to an error in building the generation, due to file name
  clashes between the two versions. However, this is not the case for
  all packages.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ./env-common.md}}

{{#include ../env-common.md}}

# Examples

```console
$ nix-env --upgrade --attr nixpkgs.gcc
upgrading `gcc-3.3.1' to `gcc-3.4'
```

When there are no updates available, nothing will happen:

```console
$ nix-env --upgrade --attr nixpkgs.pan
```

Using `-A` is preferred when possible, as it is faster and unambiguous but
it is also possible to upgrade to a specific version by matching the derivation name:

```console
$ nix-env --upgrade gcc-3.3.2 --always
upgrading `gcc-3.4' to `gcc-3.3.2'
```

To try to upgrade everything
(matching packages based on the part of the derivation name without version):

```console
$ nix-env --upgrade
upgrading `hello-2.1.2' to `hello-2.1.3'
upgrading `mozilla-1.2' to `mozilla-1.4'
```

# Versions

The upgrade operation determines whether a derivation `y` is an upgrade
of a derivation `x` by looking at their respective `name` attributes.
The names (e.g., `gcc-3.3.1` are split into two parts: the package name
(`gcc`), and the version (`3.3.1`). The version part starts after the
first dash not followed by a letter. `y` is considered an upgrade of `x`
if their package names match, and the version of `y` is higher than that
of `x`.

The versions are compared by splitting them into contiguous components
of numbers and letters. E.g., `3.3.1pre5` is split into `[3, 3, 1,
"pre", 5]`. These lists are then compared lexicographically (from left
to right). Corresponding components `a` and `b` are compared as follows.
If they are both numbers, integer comparison is used. If `a` is an empty
string and `b` is a number, `a` is considered less than `b`. The special
string component `pre` (for *pre-release*) is considered to be less than
other components. String components are considered less than number
components. Otherwise, they are compared lexicographically (i.e., using
case-sensitive string comparison).

This is illustrated by the following examples:

    1.0 < 2.3
    2.1 < 2.3
    2.3 = 2.3
    2.5 > 2.3
    3.1 > 2.3
    2.3.1 > 2.3
    2.3.1 > 2.3a
    2.3pre1 < 2.3
    2.3pre3 < 2.3pre12
    2.3a < 2.3c
    2.3pre1 < 2.3c
    2.3pre1 < 2.3q

