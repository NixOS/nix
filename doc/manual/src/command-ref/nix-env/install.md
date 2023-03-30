# Name

`nix-env --install` - add packages to user environment

# Synopsis

`nix-env` {`--install` | `-i`} *args…*
  [{`--prebuilt-only` | `-b`}]
  [{`--attr` | `-A`}]
  [`--from-expression`] [`-E`]
  [`--from-profile` *path*]
  [`--preserve-installed` | `-P`]
  [`--remove-all` | `-r`]

# Description

The install operation creates a new user environment, based on the
current generation of the active profile, to which a set of store paths
described by *args* is added. The arguments *args* map to store paths in
a number of possible ways:

  - By default, *args* is a set of derivation names denoting derivations
    in the active Nix expression. These are realised, and the resulting
    output paths are installed. Currently installed derivations with a
    name equal to the name of a derivation being added are removed
    unless the option `--preserve-installed` is specified.

    If there are multiple derivations matching a name in *args* that
    have the same name (e.g., `gcc-3.3.6` and `gcc-4.1.1`), then the
    derivation with the highest *priority* is used. A derivation can
    define a priority by declaring the `meta.priority` attribute. This
    attribute should be a number, with a higher value denoting a lower
    priority. The default priority is `0`.

    If there are multiple matching derivations with the same priority,
    then the derivation with the highest version will be installed.

    You can force the installation of multiple derivations with the same
    name by being specific about the versions. For instance, `nix-env -i
    gcc-3.3.6 gcc-4.1.1` will install both version of GCC (and will
    probably cause a user environment conflict\!).

  - If `--attr` (`-A`) is specified, the arguments are *attribute
    paths* that select attributes from the top-level Nix
    expression. This is faster than using derivation names and
    unambiguous. To find out the attribute paths of available
    packages, use `nix-env -qaP`.

  - If `--from-profile` *path* is given, *args* is a set of names
    denoting installed store paths in the profile *path*. This is an
    easy way to copy user environment elements from one profile to
    another.

  - If `--from-expression` is given, *args* are Nix
    [functions](@docroot@/language/constructs.md#functions)
    that are called with the active Nix expression as their single
    argument. The derivations returned by those function calls are
    installed. This allows derivations to be specified in an
    unambiguous way, which is necessary if there are multiple
    derivations with the same name.

  - If *args* are [store derivations](@docroot@/glossary.md#gloss-store-derivation), then these are
    [realised](@docroot@/command-ref/nix-store/realise.md), and the resulting output paths
    are installed.

  - If *args* are store paths that are not store derivations, then these
    are [realised](@docroot@/command-ref/nix-store/realise.md) and installed.

  - By default all outputs are installed for each derivation. That can
    be reduced by setting `meta.outputsToInstall`.

# Flags

  - `--prebuilt-only` / `-b`\
    Use only derivations for which a substitute is registered, i.e.,
    there is a pre-built binary available that can be downloaded in lieu
    of building the derivation. Thus, no packages will be built from
    source.

  - `--preserve-installed` / `-P`\
    Do not remove derivations with a name matching one of the
    derivations being installed. Usually, trying to have two versions of
    the same package installed in the same generation of a profile will
    lead to an error in building the generation, due to file name
    clashes between the two versions. However, this is not the case for
    all packages.

  - `--remove-all` / `-r`\
    Remove all previously installed packages first. This is equivalent
    to running `nix-env -e '.*'` first, except that everything happens
    in a single transaction.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ./env-common.md}}

{{#include ../env-common.md}}

# Examples

To install a package using a specific attribute path from the active Nix expression:

```console
$ nix-env -iA gcc40mips
installing `gcc-4.0.2'
$ nix-env -iA xorg.xorgserver
installing `xorg-server-1.2.0'
```

To install a specific version of `gcc` using the derivation name:

```console
$ nix-env --install gcc-3.3.2
installing `gcc-3.3.2'
uninstalling `gcc-3.1'
```

Using attribute path for selecting a package is preferred,
as it is much faster and there will not be multiple matches.

Note the previously installed version is removed, since
`--preserve-installed` was not specified.

To install an arbitrary version:

```console
$ nix-env --install gcc
installing `gcc-3.3.2'
```

To install all derivations in the Nix expression `foo.nix`:

```console
$ nix-env -f ~/foo.nix -i '.*'
```

To copy the store path with symbolic name `gcc` from another profile:

```console
$ nix-env -i --from-profile /nix/var/nix/profiles/foo gcc
```

To install a specific [store derivation] (typically created by
`nix-instantiate`):

```console
$ nix-env -i /nix/store/fibjb1bfbpm5mrsxc4mh2d8n37sxh91i-gcc-3.4.3.drv
```

To install a specific output path:

```console
$ nix-env -i /nix/store/y3cgx0xj1p4iv9x0pnnmdhr8iyg741vk-gcc-3.4.3
```

To install from a Nix expression specified on the command-line:

```console
$ nix-env -f ./foo.nix -i -E \
    'f: (f {system = "i686-linux";}).subversionWithJava'
```

I.e., this evaluates to `(f: (f {system =
"i686-linux";}).subversionWithJava) (import ./foo.nix)`, thus selecting
the `subversionWithJava` attribute from the set returned by calling the
function defined in `./foo.nix`.

A dry-run tells you which paths will be downloaded or built from source:

```console
$ nix-env -f '<nixpkgs>' -iA hello --dry-run
(dry run; not doing anything)
installing ‘hello-2.10’
this path will be fetched (0.04 MiB download, 0.19 MiB unpacked):
  /nix/store/wkhdf9jinag5750mqlax6z2zbwhqb76n-hello-2.10
  ...
```

To install Firefox from the latest revision in the Nixpkgs/NixOS 14.12
channel:

```console
$ nix-env -f https://github.com/NixOS/nixpkgs/archive/nixos-14.12.tar.gz -iA firefox
```

