# Name

`nix-env --query` - display information about packages

# Synopsis

`nix-env` {`--query` | `-q`} *names…*
  [`--installed` | `--available` | `-a`]
  [{`--status` | `-s`}]
  [{`--attr-path` | `-P`}]
  [`--no-name`]
  [{`--compare-versions` | `-c`}]
  [`--system`]
  [`--drv-path`]
  [`--out-path`]
  [`--description`]
  [`--meta`]
  [`--xml`]
  [`--json`]
  [{`--prebuilt-only` | `-b`}]
  [{`--attr` | `-A`} *attribute-path*]

# Description

The query operation displays information about either the store paths
that are installed in the current generation of the active profile
(`--installed`), or the derivations that are available for installation
in the active Nix expression (`--available`). It only prints information
about derivations whose symbolic name matches one of *names*.

The derivations are sorted by their `name` attributes.

# Source selection

The following flags specify the set of things on which the query
operates.

  - `--installed`

    The query operates on the store paths that are installed in the
    current generation of the active profile. This is the default.

  - `--available` / `-a`

    The query operates on the derivations that are available in the
    active Nix expression.

# Queries

The following flags specify what information to display about the
selected derivations. Multiple flags may be specified, in which case the
information is shown in the order given here. Note that the name of the
derivation is shown unless `--no-name` is specified.

  - `--xml`

    Print the result in an XML representation suitable for automatic
    processing by other tools. The root element is called `items`, which
    contains a `item` element for each available or installed
    derivation. The fields discussed below are all stored in attributes
    of the `item` elements.

  - `--json`

    Print the result in a JSON representation suitable for automatic
    processing by other tools.

  - `--prebuilt-only` / `-b`

    Show only derivations for which a substitute is registered, i.e.,
    there is a pre-built binary available that can be downloaded in lieu
    of building the derivation. Thus, this shows all packages that
    probably can be installed quickly.

  - `--status` / `-s`

    Print the *status* of the derivation. The status consists of three
    characters. The first is `I` or `-`, indicating whether the
    derivation is currently installed in the current generation of the
    active profile. This is by definition the case for `--installed`,
    but not for `--available`. The second is `P` or `-`, indicating
    whether the derivation is present on the system. This indicates
    whether installation of an available derivation will require the
    derivation to be built. The third is `S` or `-`, indicating whether
    a substitute is available for the derivation.

  - `--attr-path` / `-P`

    Print the *attribute path* of the derivation, which can be used to
    unambiguously select it using the `--attr` option available in
    commands that install derivations like `nix-env --install`. This
    option only works together with `--available`

  - `--no-name`

    Suppress printing of the `name` attribute of each derivation.

  - `--compare-versions` / `-c`

    Compare installed versions to available versions, or vice versa (if
    `--available` is given). This is useful for quickly seeing whether
    upgrades for installed packages are available in a Nix expression. A
    column is added with the following meaning:

      - `<` *version*

        A newer version of the package is available or installed.

      - `=` *version*

        At most the same version of the package is available or
        installed.

      - `>` *version*

        Only older versions of the package are available or installed.

      - `- ?`

        No version of the package is available or installed.

  - `--system`

    Print the `system` attribute of the derivation.

  - `--drv-path`

    Print the [store path] to the [store derivation].

    [store path]: @docroot@/glossary.md#gloss-store-path
    [store derivation]: @docroot@/glossary.md#gloss-derivation

  - `--out-path`

    Print the output path of the derivation.

  - `--description`

    Print a short (one-line) description of the derivation, if
    available. The description is taken from the `meta.description`
    attribute of the derivation.

  - `--meta`

    Print all of the meta-attributes of the derivation. This option is
    only available with `--xml` or `--json`.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ./env-common.md}}

{{#include ../env-common.md}}

# Examples

To show installed packages:

```console
$ nix-env --query
bison-1.875c
docbook-xml-4.2
firefox-1.0.4
MPlayer-1.0pre7
ORBit2-2.8.3
…
```

To show available packages:

```console
$ nix-env --query --available
firefox-1.0.7
GConf-2.4.0.1
MPlayer-1.0pre7
ORBit2-2.8.3
…
```

To show the status of available packages:

```console
$ nix-env --query --available --status
-P- firefox-1.0.7   (not installed but present)
--S GConf-2.4.0.1   (not present, but there is a substitute for fast installation)
--S MPlayer-1.0pre3 (i.e., this is not the installed MPlayer, even though the version is the same!)
IP- ORBit2-2.8.3    (installed and by definition present)
…
```

To show available packages in the Nix expression `foo.nix`:

```console
$ nix-env --file ./foo.nix --query --available
foo-1.2.3
```

To compare installed versions to what’s available:

```console
$ nix-env --query --compare-versions
...
acrobat-reader-7.0 - ?      (package is not available at all)
autoconf-2.59      = 2.59   (same version)
firefox-1.0.4      < 1.0.7  (a more recent version is available)
...
```

To show all packages with “`zip`” in the name:

```console
$ nix-env --query --available '.*zip.*'
bzip2-1.0.6
gzip-1.6
zip-3.0
…
```

To show all packages with “`firefox`” or “`chromium`” in the name:

```console
$ nix-env --query --available '.*(firefox|chromium).*'
chromium-37.0.2062.94
chromium-beta-38.0.2125.24
firefox-32.0.3
firefox-with-plugins-13.0.1
…
```

To show all packages in the latest revision of the Nixpkgs repository:

```console
$ nix-env --file https://github.com/NixOS/nixpkgs/archive/master.tar.gz --query --available
```

