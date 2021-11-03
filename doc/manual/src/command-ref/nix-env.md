# Name

`nix-env` - manipulate or query Nix user environments

# Synopsis

`nix-env`
  [`--option` *name* *value*]
  [`--arg` *name* *value*]
  [`--argstr` *name* *value*]
  [{`--file` | `-f`} *path*]
  [{`--profile` | `-p`} *path(]
  [`--system-filter` *system*]
  [`--dry-run`]
  *operation* [*options…*] [*arguments…*]

# Description

The command `nix-env` is used to manipulate Nix user environments. User
environments are sets of software packages available to a user at some
point in time. In other words, they are a synthesised view of the
programs available in the Nix store. There may be many user
environments: different users can have different environments, and
individual users can switch between different environments.

`nix-env` takes exactly one *operation* flag which indicates the
subcommand to be performed. These are documented below.

# Selectors

Several commands, such as `nix-env -q` and `nix-env -i`, take a list of
arguments that specify the packages on which to operate. These are
extended regular expressions that must match the entire name of the
package. (For details on regular expressions, see regex7.) The match is
case-sensitive. The regular expression can optionally be followed by a
dash and a version number; if omitted, any version of the package will
match. Here are some examples:

  - `firefox`\
    Matches the package name `firefox` and any version.

  - `firefox-32.0`\
    Matches the package name `firefox` and version `32.0`.

  - `gtk\\+`\
    Matches the package name `gtk+`. The `+` character must be escaped
    using a backslash to prevent it from being interpreted as a
    quantifier, and the backslash must be escaped in turn with another
    backslash to ensure that the shell passes it on.

  - `.\*`\
    Matches any package name. This is the default for most commands.

  - `'.*zip.*'`\
    Matches any package name containing the string `zip`. Note the dots:
    `'*zip*'` does not work, because in a regular expression, the
    character `*` is interpreted as a quantifier.

  - `'.*(firefox|chromium).*'`\
    Matches any package name containing the strings `firefox` or
    `chromium`.

# Common options

This section lists the options that are common to all operations. These
options are allowed for every subcommand, though they may not always
have an effect.

  - `--file` / `-f` *path*\
    Specifies the Nix expression (designated below as the *active Nix
    expression*) used by the `--install`, `--upgrade`, and `--query
    --available` operations to obtain derivations. The default is
    `~/.nix-defexpr`.

    If the argument starts with `http://` or `https://`, it is
    interpreted as the URL of a tarball that will be downloaded and
    unpacked to a temporary location. The tarball must include a single
    top-level directory containing at least a file named `default.nix`.

  - `--profile` / `-p` *path*\
    Specifies the profile to be used by those operations that operate on
    a profile (designated below as the *active profile*). A profile is a
    sequence of user environments called *generations*, one of which is
    the *current generation*.

  - `--dry-run`\
    For the `--install`, `--upgrade`, `--uninstall`,
    `--switch-generation`, `--delete-generations` and `--rollback`
    operations, this flag will cause `nix-env` to print what *would* be
    done if this flag had not been specified, without actually doing it.

    `--dry-run` also prints out which paths will be
    [substituted](../glossary.md) (i.e., downloaded) and which paths
    will be built from source (because no substitute is available).

  - `--system-filter` *system*\
    By default, operations such as `--query
                    --available` show derivations matching any platform. This option
    allows you to use derivations for the specified platform *system*.

<!-- end list -->

# Files

  - `~/.nix-defexpr`\
    The source for the default Nix expressions used by the
    `--install`, `--upgrade`, and `--query --available` operations to
    obtain derivations. The `--file` option may be used to override
    this default.

    If `~/.nix-defexpr` is a file, it is loaded as a Nix expression. If
    the expression is a set, it is used as the default Nix expression.
    If the expression is a function, an empty set is passed as argument
    and the return value is used as the default Nix expression.

    If `~/.nix-defexpr` is a directory containing a `default.nix` file,
    that file is loaded as in the above paragraph.

    If `~/.nix-defexpr` is a directory without a `default.nix` file,
    then its contents (both files and subdirectories) are loaded as Nix
    expressions. The expressions are combined into a single set, each
    expression under an attribute with the same name as the original
    file or subdirectory.

    For example, if `~/.nix-defexpr` contains two files, `foo.nix` and
    `bar.nix`, then the default Nix expression will essentially be

    ```nix
    {
      foo = import ~/.nix-defexpr/foo.nix;
      bar = import ~/.nix-defexpr/bar.nix;
    }
    ```

    The file `manifest.nix` is always ignored. Subdirectories without a
    `default.nix` file are traversed recursively in search of more Nix
    expressions, but the names of these intermediate directories are not
    added to the attribute paths of the default Nix expression.

    The command `nix-channel` places symlinks to the downloaded Nix
    expressions from each subscribed channel in this directory.

  - `~/.nix-profile`\
    A symbolic link to the user's current profile. By default, this
    symlink points to `prefix/var/nix/profiles/default`. The `PATH`
    environment variable should include `~/.nix-profile/bin` for the
    user environment to be visible to the user.

# Operation `--install`

## Synopsis

`nix-env` {`--install` | `-i`} *args…*
  [{`--prebuilt-only` | `-b`}]
  [{`--attr` | `-A`}]
  [`--from-expression`] [`-E`]
  [`--from-profile` *path*]
  [`--preserve-installed` | `-P`]
  [`--remove-all` | `-r`]

## Description

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
    [functions](../expressions/language-constructs.md#functions)
    that are called with the active Nix expression as their single
    argument. The derivations returned by those function calls are
    installed. This allows derivations to be specified in an
    unambiguous way, which is necessary if there are multiple
    derivations with the same name.

  - If *args* are store derivations, then these are
    [realised](nix-store.md#operation---realise), and the resulting output paths
    are installed.

  - If *args* are store paths that are not store derivations, then these
    are [realised](nix-store.md#operation---realise) and installed.

  - By default all outputs are installed for each derivation. That can
    be reduced by setting `meta.outputsToInstall`.

## Flags

  - `--prebuilt-only` / `-b`\
    Use only derivations for which a substitute is registered, i.e.,
    there is a pre-built binary available that can be downloaded in lieu
    of building the derivation. Thus, no packages will be built from
    source.

  - `--preserve-installed`; `-P`\
    Do not remove derivations with a name matching one of the
    derivations being installed. Usually, trying to have two versions of
    the same package installed in the same generation of a profile will
    lead to an error in building the generation, due to file name
    clashes between the two versions. However, this is not the case for
    all packages.

  - `--remove-all`; `-r`\
    Remove all previously installed packages first. This is equivalent
    to running `nix-env -e '.*'` first, except that everything happens
    in a single transaction.

## Examples

To install a specific version of `gcc` from the active Nix expression:

```console
$ nix-env --install gcc-3.3.2
installing `gcc-3.3.2'
uninstalling `gcc-3.1'
```

Note the previously installed version is removed, since
`--preserve-installed` was not specified.

To install an arbitrary version:

```console
$ nix-env --install gcc
installing `gcc-3.3.2'
```

To install using a specific attribute:

```console
$ nix-env -i -A gcc40mips
$ nix-env -i -A xorg.xorgserver
```

To install all derivations in the Nix expression `foo.nix`:

```console
$ nix-env -f ~/foo.nix -i '.*'
```

To copy the store path with symbolic name `gcc` from another profile:

```console
$ nix-env -i --from-profile /nix/var/nix/profiles/foo gcc
```

To install a specific store derivation (typically created by
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

# Operation `--upgrade`

## Synopsis

`nix-env` {`--upgrade` | `-u`} *args*
  [`--lt` | `--leq` | `--eq` | `--always`]
  [{`--prebuilt-only` | `-b`}]
  [{`--attr` | `-A`}]
  [`--from-expression`] [`-E`]
  [`--from-profile` *path*]
  [`--preserve-installed` | `-P`]

## Description

The upgrade operation creates a new user environment, based on the
current generation of the active profile, in which all store paths are
replaced for which there are newer versions in the set of paths
described by *args*. Paths for which there are no newer versions are
left untouched; this is not an error. It is also not an error if an
element of *args* matches no installed derivations.

For a description of how *args* is mapped to a set of store paths, see
[`--install`](#operation---install). If *args* describes multiple
store paths with the same symbolic name, only the one with the highest
version is installed.

## Flags

  - `--lt`\
    Only upgrade a derivation to newer versions. This is the default.

  - `--leq`\
    In addition to upgrading to newer versions, also “upgrade” to
    derivations that have the same version. Version are not a unique
    identification of a derivation, so there may be many derivations
    that have the same version. This flag may be useful to force
    “synchronisation” between the installed and available derivations.

  - `--eq`\
    *Only* “upgrade” to derivations that have the same version. This may
    not seem very useful, but it actually is, e.g., when there is a new
    release of Nixpkgs and you want to replace installed applications
    with the same versions built against newer dependencies (to reduce
    the number of dependencies floating around on your system).

  - `--always`\
    In addition to upgrading to newer versions, also “upgrade” to
    derivations that have the same or a lower version. I.e., derivations
    may actually be downgraded depending on what is available in the
    active Nix expression.

For the other flags, see `--install`.

## Examples

```console
$ nix-env --upgrade gcc
upgrading `gcc-3.3.1' to `gcc-3.4'
```

```console
$ nix-env -u gcc-3.3.2 --always (switch to a specific version)
upgrading `gcc-3.4' to `gcc-3.3.2'
```

```console
$ nix-env --upgrade pan
(no upgrades available, so nothing happens)
```

```console
$ nix-env -u (try to upgrade everything)
upgrading `hello-2.1.2' to `hello-2.1.3'
upgrading `mozilla-1.2' to `mozilla-1.4'
```

## Versions

The upgrade operation determines whether a derivation `y` is an upgrade
of a derivation `x` by looking at their respective `name` attributes.
The names (e.g., `gcc-3.3.1` are split into two parts: the package name
(`gcc`), and the version (`3.3.1`). The version part starts after the
first dash not followed by a letter. `x` is considered an upgrade of `y`
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

# Operation `--uninstall`

## Synopsis

`nix-env` {`--uninstall` | `-e`} *drvnames…*

## Description

The uninstall operation creates a new user environment, based on the
current generation of the active profile, from which the store paths
designated by the symbolic names *drvnames* are removed.

## Examples

```console
$ nix-env --uninstall gcc
$ nix-env -e '.*' (remove everything)
```

# Operation `--set`

## Synopsis

`nix-env` `--set` *drvname*

## Description

The `--set` operation modifies the current generation of a profile so
that it contains exactly the specified derivation, and nothing else.

## Examples

The following updates a profile such that its current generation will
contain just Firefox:

```console
$ nix-env -p /nix/var/nix/profiles/browser --set firefox
```

# Operation `--set-flag`

## Synopsis

`nix-env` `--set-flag` *name* *value* *drvnames*

## Description

The `--set-flag` operation allows meta attributes of installed packages
to be modified. There are several attributes that can be usefully
modified, because they affect the behaviour of `nix-env` or the user
environment build script:

  - `priority` can be changed to resolve filename clashes. The user
    environment build script uses the `meta.priority` attribute of
    derivations to resolve filename collisions between packages. Lower
    priority values denote a higher priority. For instance, the GCC
    wrapper package and the Binutils package in Nixpkgs both have a file
    `bin/ld`, so previously if you tried to install both you would get a
    collision. Now, on the other hand, the GCC wrapper declares a higher
    priority than Binutils, so the former’s `bin/ld` is symlinked in the
    user environment.

  - `keep` can be set to `true` to prevent the package from being
    upgraded or replaced. This is useful if you want to hang on to an
    older version of a package.

  - `active` can be set to `false` to “disable” the package. That is, no
    symlinks will be generated to the files of the package, but it
    remains part of the profile (so it won’t be garbage-collected). It
    can be set back to `true` to re-enable the package.

## Examples

To prevent the currently installed Firefox from being upgraded:

```console
$ nix-env --set-flag keep true firefox
```

After this, `nix-env -u` will ignore Firefox.

To disable the currently installed Firefox, then install a new Firefox
while the old remains part of the profile:

```console
$ nix-env -q
firefox-2.0.0.9 (the current one)

$ nix-env --preserve-installed -i firefox-2.0.0.11
installing `firefox-2.0.0.11'
building path(s) `/nix/store/myy0y59q3ig70dgq37jqwg1j0rsapzsl-user-environment'
collision between `/nix/store/...-firefox-2.0.0.11/bin/firefox'
  and `/nix/store/...-firefox-2.0.0.9/bin/firefox'.
(i.e., can’t have two active at the same time)

$ nix-env --set-flag active false firefox
setting flag on `firefox-2.0.0.9'

$ nix-env --preserve-installed -i firefox-2.0.0.11
installing `firefox-2.0.0.11'

$ nix-env -q
firefox-2.0.0.11 (the enabled one)
firefox-2.0.0.9 (the disabled one)
```

To make files from `binutils` take precedence over files from `gcc`:

```console
$ nix-env --set-flag priority 5 binutils
$ nix-env --set-flag priority 10 gcc
```

# Operation `--query`

## Synopsis

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

## Description

The query operation displays information about either the store paths
that are installed in the current generation of the active profile
(`--installed`), or the derivations that are available for installation
in the active Nix expression (`--available`). It only prints information
about derivations whose symbolic name matches one of *names*.

The derivations are sorted by their `name` attributes.

## Source selection

The following flags specify the set of things on which the query
operates.

  - `--installed`\
    The query operates on the store paths that are installed in the
    current generation of the active profile. This is the default.

  - `--available`; `-a`\
    The query operates on the derivations that are available in the
    active Nix expression.

## Queries

The following flags specify what information to display about the
selected derivations. Multiple flags may be specified, in which case the
information is shown in the order given here. Note that the name of the
derivation is shown unless `--no-name` is specified.

  - `--xml`\
    Print the result in an XML representation suitable for automatic
    processing by other tools. The root element is called `items`, which
    contains a `item` element for each available or installed
    derivation. The fields discussed below are all stored in attributes
    of the `item` elements.

  - `--json`\
    Print the result in a JSON representation suitable for automatic
    processing by other tools.

  - `--prebuilt-only` / `-b`\
    Show only derivations for which a substitute is registered, i.e.,
    there is a pre-built binary available that can be downloaded in lieu
    of building the derivation. Thus, this shows all packages that
    probably can be installed quickly.

  - `--status`; `-s`\
    Print the *status* of the derivation. The status consists of three
    characters. The first is `I` or `-`, indicating whether the
    derivation is currently installed in the current generation of the
    active profile. This is by definition the case for `--installed`,
    but not for `--available`. The second is `P` or `-`, indicating
    whether the derivation is present on the system. This indicates
    whether installation of an available derivation will require the
    derivation to be built. The third is `S` or `-`, indicating whether
    a substitute is available for the derivation.

  - `--attr-path`; `-P`\
    Print the *attribute path* of the derivation, which can be used to
    unambiguously select it using the `--attr` option available in
    commands that install derivations like `nix-env --install`. This
    option only works together with `--available`

  - `--no-name`\
    Suppress printing of the `name` attribute of each derivation.

  - `--compare-versions` / `-c`\
    Compare installed versions to available versions, or vice versa (if
    `--available` is given). This is useful for quickly seeing whether
    upgrades for installed packages are available in a Nix expression. A
    column is added with the following meaning:

      - `<` *version*\
        A newer version of the package is available or installed.

      - `=` *version*\
        At most the same version of the package is available or
        installed.

      - `>` *version*\
        Only older versions of the package are available or installed.

      - `- ?`\
        No version of the package is available or installed.

  - `--system`\
    Print the `system` attribute of the derivation.

  - `--drv-path`\
    Print the path of the store derivation.

  - `--out-path`\
    Print the output path of the derivation.

  - `--description`\
    Print a short (one-line) description of the derivation, if
    available. The description is taken from the `meta.description`
    attribute of the derivation.

  - `--meta`\
    Print all of the meta-attributes of the derivation. This option is
    only available with `--xml` or `--json`.

## Examples

To show installed packages:

```console
$ nix-env -q
bison-1.875c
docbook-xml-4.2
firefox-1.0.4
MPlayer-1.0pre7
ORBit2-2.8.3
…
```

To show available packages:

```console
$ nix-env -qa
firefox-1.0.7
GConf-2.4.0.1
MPlayer-1.0pre7
ORBit2-2.8.3
…
```

To show the status of available packages:

```console
$ nix-env -qas
-P- firefox-1.0.7   (not installed but present)
--S GConf-2.4.0.1   (not present, but there is a substitute for fast installation)
--S MPlayer-1.0pre3 (i.e., this is not the installed MPlayer, even though the version is the same!)
IP- ORBit2-2.8.3    (installed and by definition present)
…
```

To show available packages in the Nix expression `foo.nix`:

```console
$ nix-env -f ./foo.nix -qa
foo-1.2.3
```

To compare installed versions to what’s available:

```console
$ nix-env -qc
...
acrobat-reader-7.0 - ?      (package is not available at all)
autoconf-2.59      = 2.59   (same version)
firefox-1.0.4      < 1.0.7  (a more recent version is available)
...
```

To show all packages with “`zip`” in the name:

```console
$ nix-env -qa '.*zip.*'
bzip2-1.0.6
gzip-1.6
zip-3.0
…
```

To show all packages with “`firefox`” or “`chromium`” in the name:

```console
$ nix-env -qa '.*(firefox|chromium).*'
chromium-37.0.2062.94
chromium-beta-38.0.2125.24
firefox-32.0.3
firefox-with-plugins-13.0.1
…
```

To show all packages in the latest revision of the Nixpkgs repository:

```console
$ nix-env -f https://github.com/NixOS/nixpkgs/archive/master.tar.gz -qa
```

# Operation `--switch-profile`

## Synopsis

`nix-env` {`--switch-profile` | `-S`} *path*

## Description

This operation makes *path* the current profile for the user. That is,
the symlink `~/.nix-profile` is made to point to *path*.

## Examples

```console
$ nix-env -S ~/my-profile
```

# Operation `--list-generations`

## Synopsis

`nix-env` `--list-generations`

## Description

This operation print a list of all the currently existing generations
for the active profile. These may be switched to using the
`--switch-generation` operation. It also prints the creation date of the
generation, and indicates the current generation.

## Examples

```console
$ nix-env --list-generations
  95   2004-02-06 11:48:24
  96   2004-02-06 11:49:01
  97   2004-02-06 16:22:45
  98   2004-02-06 16:24:33   (current)
```

# Operation `--delete-generations`

## Synopsis

`nix-env` `--delete-generations` *generations*

## Description

This operation deletes the specified generations of the current profile.
The generations can be a list of generation numbers, the special value
`old` to delete all non-current generations, a value such as `30d` to
delete all generations older than the specified number of days (except
for the generation that was active at that point in time), or a value
such as `+5` to keep the last `5` generations ignoring any newer than
current, e.g., if `30` is the current generation `+5` will delete
generation `25` and all older generations. Periodically deleting old
generations is important to make garbage collection effective.

## Examples

```console
$ nix-env --delete-generations 3 4 8
```

```console
$ nix-env --delete-generations +5
```

```console
$ nix-env --delete-generations 30d
```

```console
$ nix-env -p other_profile --delete-generations old
```

# Operation `--switch-generation`

## Synopsis

`nix-env` {`--switch-generation` | `-G`} *generation*

## Description

This operation makes generation number *generation* the current
generation of the active profile. That is, if the `profile` is the path
to the active profile, then the symlink `profile` is made to point to
`profile-generation-link`, which is in turn a symlink to the actual user
environment in the Nix store.

Switching will fail if the specified generation does not exist.

## Examples

```console
$ nix-env -G 42
switching from generation 50 to 42
```

# Operation `--rollback`

## Synopsis

`nix-env` `--rollback`

## Description

This operation switches to the “previous” generation of the active
profile, that is, the highest numbered generation lower than the current
generation, if it exists. It is just a convenience wrapper around
`--list-generations` and `--switch-generation`.

## Examples

```console
$ nix-env --rollback
switching from generation 92 to 91
```

```console
$ nix-env --rollback
error: no generation older than the current (91) exists
```

# Environment variables

  - `NIX_PROFILE`\
    Location of the Nix profile. Defaults to the target of the symlink
    `~/.nix-profile`, if it exists, or `/nix/var/nix/profiles/default`
    otherwise.
