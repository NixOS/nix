# Name

`nix` - a tool for reproducible and declarative configuration management

# Synopsis

`nix` [*flags*...] *subcommand*

# Flags

  - `--debug`  
    enable debug output

  - `--help`  
    show usage information

  - `--help-config`  
    show configuration options

  - `--log-format` *format*  
    format of log output; `raw`, `internal-json`, `bar` or `bar-with-logs`

  - `--no-net`  
    disable substituters and consider all previously downloaded files up-to-date

  - `--option` *name* *value*  
    set a Nix configuration option (overriding `nix.conf`)

  - `--print-build-logs` / `L`  
    print full build logs on stderr

  - `--quiet`  
    decrease verbosity level

  - `--refresh`  
    consider all previously downloaded files out-of-date

  - `--verbose` / `v`  
    increase verbosity level

  - `--version`  
    show version information

# Subcommand `nix add-to-store`

## Name

`nix add-to-store` - add a path to the Nix store

## Synopsis

`nix add-to-store` [*flags*...] *path*

## Description


Copy the file or directory *path* to the Nix store, and
print the resulting store path on standard output.



## Flags

  - `--dry-run`  
    show what this command would do without doing it

  - `--flat`  
    add flat file to the Nix store

  - `--name` / `n` *name*  
    name component of the store path

# Subcommand `nix build`

## Name

`nix build` - build a derivation or fetch a store path

## Synopsis

`nix build` [*flags*...] *installables*...

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--dry-run`  
    show what this command would do without doing it

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-link`  
    do not create a symlink to the build result

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--out-link` / `o` *path*  
    path of the symlink to the build result

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--profile` *path*  
    profile to update

  - `--rebuild`  
    rebuild an already built package and compare the result to the existing store paths

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To build and run GNU Hello from NixOS 17.03:

```console
nix build -f channel:nixos-17.03 hello; ./result/bin/hello
```

To build the build.x86_64-linux attribute from release.nix:

```console
nix build -f release.nix build.x86_64-linux
```

To make a profile point at GNU Hello:

```console
nix build --profile /tmp/profile nixpkgs#hello
```

# Subcommand `nix bundle`

## Name

`nix bundle` - bundle an application so that it works outside of the Nix store

## Synopsis

`nix bundle` [*flags*...] *installable*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--bundler` *flake-url*  
    use custom bundler

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--out-link` / `o` *path*  
    path of the symlink to the build result

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To bundle Hello:

```console
nix bundle hello
```

# Subcommand `nix cat-nar`

## Name

`nix cat-nar` - print the contents of a file inside a NAR file on stdout

## Synopsis

`nix cat-nar` [*flags*...] *nar* *path*

# Subcommand `nix cat-store`

## Name

`nix cat-store` - print the contents of a file in the Nix store on stdout

## Synopsis

`nix cat-store` [*flags*...] *path*

# Subcommand `nix copy`

## Name

`nix copy` - copy paths between Nix stores

## Synopsis

`nix copy` [*flags*...] *installables*...

## Flags

  - `--all`  
    apply operation to the entire store

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--from` *store-uri*  
    URI of the source Nix store

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-check-sigs`  
    do not require that paths are signed by trusted keys

  - `--no-recursive`  
    apply operation to specified paths only

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--substitute-on-destination` / `s`  
    whether to try substitutes on the destination store (only supported by SSH)

  - `--to` *store-uri*  
    URI of the destination Nix store

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To copy Firefox from the local store to a binary cache in file:///tmp/cache:

```console
nix copy --to file:///tmp/cache $(type -p firefox)
```

To copy the entire current NixOS system closure to another machine via SSH:

```console
nix copy --to ssh://server /run/current-system
```

To copy a closure from another machine via SSH:

```console
nix copy --from ssh://server /nix/store/a6cnl93nk1wxnq84brbbwr6hxw9gp2w9-blender-2.79-rc2
```

To copy Hello to an S3 binary cache:

```console
nix copy --to s3://my-bucket?region=eu-west-1 nixpkgs#hello
```

To copy Hello to an S3-compatible binary cache:

```console
nix copy --to s3://my-bucket?region=eu-west-1&endpoint=example.com nixpkgs#hello
```

# Subcommand `nix copy-sigs`

## Name

`nix copy-sigs` - copy path signatures from substituters (like binary caches)

## Synopsis

`nix copy-sigs` [*flags*...] *installables*...

## Flags

  - `--all`  
    apply operation to the entire store

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--recursive` / `r`  
    apply operation to closure of the specified paths

  - `--substituter` / `s` *store-uri*  
    use signatures from specified store

  - `--update-input` *input-path*  
    update a specific flake input

# Subcommand `nix describe-stores`

## Name

`nix describe-stores` - show registered store types and their available options

## Synopsis

`nix describe-stores` [*flags*...] 

## Flags

  - `--json`  
    produce JSON output

# Subcommand `nix develop`

## Name

`nix develop` - run a bash shell that provides the build environment of a derivation

## Synopsis

`nix develop` [*flags*...] *installable*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--build`  
    run the build phase

  - `--check`  
    run the check phase

  - `--command` / `c` *command* *args*  
    command and arguments to be executed instead of an interactive shell

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--configure`  
    run the configure phase

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--ignore-environment` / `i`  
    clear the entire environment (except those specified with --keep)

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--install`  
    run the install phase

  - `--installcheck`  
    run the installcheck phase

  - `--keep` / `k` *name*  
    keep specified environment variable

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--phase` *phase-name*  
    phase to run (e.g. `build` or `configure`)

  - `--profile` *path*  
    profile to update

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--redirect` *installable* *outputs-dir*  
    redirect a store path to a mutable location

  - `--unset` / `u` *name*  
    unset specified environment variable

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To get the build environment of GNU hello:

```console
nix develop nixpkgs#hello
```

To get the build environment of the default package of flake in the current directory:

```console
nix develop
```

To store the build environment in a profile:

```console
nix develop --profile /tmp/my-shell nixpkgs#hello
```

To use a build environment previously recorded in a profile:

```console
nix develop /tmp/my-shell
```

To replace all occurences of a store path with a writable directory:

```console
nix develop --redirect nixpkgs#glibc.dev ~/my-glibc/outputs/dev
```

# Subcommand `nix diff-closures`

## Name

`nix diff-closures` - show what packages and versions were added and removed between two closures

## Synopsis

`nix diff-closures` [*flags*...] *before* *after*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To show what got added and removed between two versions of the NixOS system profile:

```console
nix diff-closures /nix/var/nix/profiles/system-655-link /nix/var/nix/profiles/system-658-link
```

# Subcommand `nix doctor`

## Name

`nix doctor` - check your system for potential problems and print a PASS or FAIL for each check

## Synopsis

`nix doctor` [*flags*...] 

# Subcommand `nix dump-path`

## Name

`nix dump-path` - dump a store path to stdout (in NAR format)

## Synopsis

`nix dump-path` [*flags*...] *installables*...

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To get a NAR from the binary cache https://cache.nixos.org/:

```console
nix dump-path --store https://cache.nixos.org/ /nix/store/7crrmih8c52r8fbnqb933dxrsp44md93-glibc-2.25
```

# Subcommand `nix edit`

## Name

`nix edit` - open the Nix expression of a Nix package in $EDITOR

## Synopsis

`nix edit` [*flags*...] *installable*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To open the Nix expression of the GNU Hello package:

```console
nix edit nixpkgs#hello
```

# Subcommand `nix eval`

## Name

`nix eval` - evaluate a Nix expression

## Synopsis

`nix eval` [*flags*...] *installable*

## Flags

  - `--apply` *expr*  
    apply a function to each argument

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--json`  
    produce JSON output

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--raw`  
    print strings unquoted

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To evaluate a Nix expression given on the command line:

```console
nix eval --expr '1 + 2'
```

To evaluate a Nix expression from a file or URI:

```console
nix eval -f ./my-nixpkgs hello.name
```

To get the current version of Nixpkgs:

```console
nix eval --raw nixpkgs#lib.version
```

To print the store path of the Hello package:

```console
nix eval --raw nixpkgs#hello
```

To get a list of checks in the 'nix' flake:

```console
nix eval nix#checks.x86_64-linux --apply builtins.attrNames
```

# Subcommand `nix flake`

## Name

`nix flake` - manage Nix flakes

## Synopsis

`nix flake` [*flags*...] *subcommand*

# Subcommand `nix flake archive`

## Name

`nix flake archive` - copy a flake and all its inputs to a store

## Synopsis

`nix flake archive` [*flags*...] *flake-url*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--dry-run`  
    show what this command would do without doing it

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--json`  
    produce JSON output

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--to` *store-uri*  
    URI of the destination Nix store

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To copy the dwarffs flake and its dependencies to a binary cache:

```console
nix flake archive --to file:///tmp/my-cache dwarffs
```

To fetch the dwarffs flake and its dependencies to the local Nix store:

```console
nix flake archive dwarffs
```

To print the store paths of the flake sources of NixOps without fetching them:

```console
nix flake archive --json --dry-run nixops
```

# Subcommand `nix flake check`

## Name

`nix flake check` - check whether the flake evaluates and run its tests

## Synopsis

`nix flake check` [*flags*...] *flake-url*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-build`  
    do not build checks

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

# Subcommand `nix flake clone`

## Name

`nix flake clone` - clone flake repository

## Synopsis

`nix flake clone` [*flags*...] *flake-url*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--dest` / `f` *path*  
    destination path

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

# Subcommand `nix flake info`

## Name

`nix flake info` - list info about a given flake

## Synopsis

`nix flake info` [*flags*...] *flake-url*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--json`  
    produce JSON output

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

# Subcommand `nix flake init`

## Name

`nix flake init` - create a flake in the current directory from a template

## Synopsis

`nix flake init` [*flags*...] 

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--template` / `t` *template*  
    the template to use

## Examples

To create a flake using the default template:

```console
nix flake init
```

To see available templates:

```console
nix flake show templates
```

To create a flake from a specific template:

```console
nix flake init -t templates#nixos-container
```

# Subcommand `nix flake list-inputs`

## Name

`nix flake list-inputs` - list flake inputs

## Synopsis

`nix flake list-inputs` [*flags*...] *flake-url*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--json`  
    produce JSON output

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

# Subcommand `nix flake new`

## Name

`nix flake new` - create a flake in the specified directory from a template

## Synopsis

`nix flake new` [*flags*...] *dest-dir*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--template` / `t` *template*  
    the template to use

# Subcommand `nix flake show`

## Name

`nix flake show` - show the outputs provided by a flake

## Synopsis

`nix flake show` [*flags*...] *flake-url*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--legacy`  
    show the contents of the 'legacyPackages' output

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

# Subcommand `nix flake update`

## Name

`nix flake update` - update flake lock file

## Synopsis

`nix flake update` [*flags*...] *flake-url*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

# Subcommand `nix hash-file`

## Name

`nix hash-file` - print cryptographic hash of a regular file

## Synopsis

`nix hash-file` [*flags*...] *paths*...

## Flags

  - `--base16`  
    print hash in base-16

  - `--base32`  
    print hash in base-32 (Nix-specific)

  - `--base64`  
    print hash in base-64

  - `--sri`  
    print hash in SRI format

  - `--type` *hash-algo*  
    hash algorithm ('md5', 'sha1', 'sha256', or 'sha512')

# Subcommand `nix hash-path`

## Name

`nix hash-path` - print cryptographic hash of the NAR serialisation of a path

## Synopsis

`nix hash-path` [*flags*...] *paths*...

## Flags

  - `--base16`  
    print hash in base-16

  - `--base32`  
    print hash in base-32 (Nix-specific)

  - `--base64`  
    print hash in base-64

  - `--sri`  
    print hash in SRI format

  - `--type` *hash-algo*  
    hash algorithm ('md5', 'sha1', 'sha256', or 'sha512')

# Subcommand `nix log`

## Name

`nix log` - show the build log of the specified packages or paths, if available

## Synopsis

`nix log` [*flags*...] *installable*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To get the build log of GNU Hello:

```console
nix log nixpkgs#hello
```

To get the build log of a specific path:

```console
nix log /nix/store/lmngj4wcm9rkv3w4dfhzhcyij3195hiq-thunderbird-52.2.1
```

To get a build log from a specific binary cache:

```console
nix log --store https://cache.nixos.org nixpkgs#hello
```

# Subcommand `nix ls-nar`

## Name

`nix ls-nar` - show information about a path inside a NAR file

## Synopsis

`nix ls-nar` [*flags*...] *nar* *path*

## Flags

  - `--directory` / `d`  
    show directories rather than their contents

  - `--json`  
    produce JSON output

  - `--long` / `l`  
    show more file information

  - `--recursive` / `R`  
    list subdirectories recursively

## Examples

To list a specific file in a NAR:

```console
nix ls-nar -l hello.nar /bin/hello
```

# Subcommand `nix ls-store`

## Name

`nix ls-store` - show information about a path in the Nix store

## Synopsis

`nix ls-store` [*flags*...] *path*

## Flags

  - `--directory` / `d`  
    show directories rather than their contents

  - `--json`  
    produce JSON output

  - `--long` / `l`  
    show more file information

  - `--recursive` / `R`  
    list subdirectories recursively

## Examples

To list the contents of a store path in a binary cache:

```console
nix ls-store --store https://cache.nixos.org/ -lR /nix/store/0i2jd68mp5g6h2sa5k9c85rb80sn8hi9-hello-2.10
```

# Subcommand `nix make-content-addressable`

## Name

`nix make-content-addressable` - rewrite a path or closure to content-addressable form

## Synopsis

`nix make-content-addressable` [*flags*...] *installables*...

## Flags

  - `--all`  
    apply operation to the entire store

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--json`  
    produce JSON output

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--recursive` / `r`  
    apply operation to closure of the specified paths

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To create a content-addressable representation of GNU Hello (but not its dependencies):

```console
nix make-content-addressable nixpkgs#hello
```

To compute a content-addressable representation of the current NixOS system closure:

```console
nix make-content-addressable -r /run/current-system
```

# Subcommand `nix optimise-store`

## Name

`nix optimise-store` - replace identical files in the store by hard links

## Synopsis

`nix optimise-store` [*flags*...] 

## Examples

To optimise the Nix store:

```console
nix optimise-store
```

# Subcommand `nix path-info`

## Name

`nix path-info` - query information about store paths

## Synopsis

`nix path-info` [*flags*...] *installables*...

## Flags

  - `--all`  
    apply operation to the entire store

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--closure-size` / `S`  
    print sum size of the NAR dumps of the closure of each path

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--human-readable` / `h`  
    with -s and -S, print sizes like 1K 234M 5.67G etc.

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--json`  
    produce JSON output

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--recursive` / `r`  
    apply operation to closure of the specified paths

  - `--sigs`  
    show signatures

  - `--size` / `s`  
    print size of the NAR dump of each path

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To show the closure sizes of every path in the current NixOS system closure, sorted by size:

```console
nix path-info -rS /run/current-system | sort -nk2
```

To show a package's closure size and all its dependencies with human readable sizes:

```console
nix path-info -rsSh nixpkgs#rust
```

To check the existence of a path in a binary cache:

```console
nix path-info -r /nix/store/7qvk5c91...-geeqie-1.1 --store https://cache.nixos.org/
```

To print the 10 most recently added paths (using --json and the jq(1) command):

```console
nix path-info --json --all | jq -r 'sort_by(.registrationTime)[-11:-1][].path'
```

To show the size of the entire Nix store:

```console
nix path-info --json --all | jq 'map(.narSize) | add'
```

To show every path whose closure is bigger than 1 GB, sorted by closure size:

```console
nix path-info --json --all -S | jq 'map(select(.closureSize > 1e9)) | sort_by(.closureSize) | map([.path, .closureSize])'
```

# Subcommand `nix ping-store`

## Name

`nix ping-store` - test whether a store can be opened

## Synopsis

`nix ping-store` [*flags*...] 

## Examples

To test whether connecting to a remote Nix store via SSH works:

```console
nix ping-store --store ssh://mac1
```

# Subcommand `nix print-dev-env`

## Name

`nix print-dev-env` - print shell code that can be sourced by bash to reproduce the build environment of a derivation

## Synopsis

`nix print-dev-env` [*flags*...] *installable*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--profile` *path*  
    profile to update

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--redirect` *installable* *outputs-dir*  
    redirect a store path to a mutable location

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To apply the build environment of GNU hello to the current shell:

```console
. <(nix print-dev-env nixpkgs#hello)
```

# Subcommand `nix profile`

## Name

`nix profile` - manage Nix profiles

## Synopsis

`nix profile` [*flags*...] *subcommand*

# Subcommand `nix profile diff-closures`

## Name

`nix profile diff-closures` - show the closure difference between each generation of a profile

## Synopsis

`nix profile diff-closures` [*flags*...] 

## Flags

  - `--profile` *path*  
    profile to update

## Examples

To show what changed between each generation of the NixOS system profile:

```console
nix profile diff-closure --profile /nix/var/nix/profiles/system
```

# Subcommand `nix profile info`

## Name

`nix profile info` - list installed packages

## Synopsis

`nix profile info` [*flags*...] 

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--profile` *path*  
    profile to update

## Examples

To show what packages are installed in the default profile:

```console
nix profile info
```

# Subcommand `nix profile install`

## Name

`nix profile install` - install a package into a profile

## Synopsis

`nix profile install` [*flags*...] *installables*...

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--profile` *path*  
    profile to update

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To install a package from Nixpkgs:

```console
nix profile install nixpkgs#hello
```

To install a package from a specific branch of Nixpkgs:

```console
nix profile install nixpkgs/release-19.09#hello
```

To install a package from a specific revision of Nixpkgs:

```console
nix profile install nixpkgs/1028bb33859f8dfad7f98e1c8d185f3d1aaa7340#hello
```

# Subcommand `nix profile remove`

## Name

`nix profile remove` - remove packages from a profile

## Synopsis

`nix profile remove` [*flags*...] *elements*...

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--profile` *path*  
    profile to update

## Examples

To remove a package by attribute path:

```console
nix profile remove packages.x86_64-linux.hello
```

To remove all packages:

```console
nix profile remove '.*'
```

To remove a package by store path:

```console
nix profile remove /nix/store/rr3y0c6zyk7kjjl8y19s4lsrhn4aiq1z-hello-2.10
```

To remove a package by position:

```console
nix profile remove 3
```

# Subcommand `nix profile upgrade`

## Name

`nix profile upgrade` - upgrade packages using their most recent flake

## Synopsis

`nix profile upgrade` [*flags*...] *elements*...

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--profile` *path*  
    profile to update

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To upgrade all packages that were installed using a mutable flake reference:

```console
nix profile upgrade '.*'
```

To upgrade a specific package:

```console
nix profile upgrade packages.x86_64-linux.hello
```

# Subcommand `nix registry`

## Name

`nix registry` - manage the flake registry

## Synopsis

`nix registry` [*flags*...] *subcommand*

# Subcommand `nix registry add`

## Name

`nix registry add` - add/replace flake in user flake registry

## Synopsis

`nix registry add` [*flags*...] *from-url* *to-url*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

# Subcommand `nix registry list`

## Name

`nix registry list` - list available Nix flakes

## Synopsis

`nix registry list` [*flags*...] 

# Subcommand `nix registry pin`

## Name

`nix registry pin` - pin a flake to its current version in user flake registry

## Synopsis

`nix registry pin` [*flags*...] *url*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

# Subcommand `nix registry remove`

## Name

`nix registry remove` - remove flake from user flake registry

## Synopsis

`nix registry remove` [*flags*...] *url*

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

# Subcommand `nix repl`

## Name

`nix repl` - start an interactive environment for evaluating Nix expressions

## Synopsis

`nix repl` [*flags*...] *files*...

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

## Examples

Display all special commands within the REPL:

```console
nix repl
nix-repl> :?
```

# Subcommand `nix run`

## Name

`nix run` - run a Nix application

## Synopsis

`nix run` [*flags*...] *installable* *args*...

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To run Blender:

```console
nix run blender-bin
```

To run vim from nixpkgs:

```console
nix run nixpkgs#vim
```

To run vim from nixpkgs with arguments:

```console
nix run nixpkgs#vim -- --help
```

# Subcommand `nix search`

## Name

`nix search` - query available packages

## Synopsis

`nix search` [*flags*...] *installable* *regex*...

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--json`  
    produce JSON output

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To show all packages in the flake in the current directory:

```console
nix search
```

To show packages in the 'nixpkgs' flake containing 'blender' in its name or description:

```console
nix search nixpkgs blender
```

To search for Firefox or Chromium:

```console
nix search nixpkgs 'firefox|chromium'
```

To search for packages containing 'git' and either 'frontend' or 'gui':

```console
nix search nixpkgs git 'frontend|gui'
```

# Subcommand `nix shell`

## Name

`nix shell` - run a shell in which the specified packages are available

## Synopsis

`nix shell` [*flags*...] *installables*...

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--command` / `c` *command* *args*  
    command and arguments to be executed; defaults to '$SHELL'

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--ignore-environment` / `i`  
    clear the entire environment (except those specified with --keep)

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--keep` / `k` *name*  
    keep specified environment variable

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--unset` / `u` *name*  
    unset specified environment variable

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To start a shell providing GNU Hello from NixOS 20.03:

```console
nix shell nixpkgs/nixos-20.03#hello
```

To start a shell providing youtube-dl from your 'nixpkgs' channel:

```console
nix shell nixpkgs#youtube-dl
```

To run GNU Hello:

```console
nix shell nixpkgs#hello -c hello --greeting 'Hi everybody!'
```

To run GNU Hello in a chroot store:

```console
nix shell --store ~/my-nix nixpkgs#hello -c hello
```

# Subcommand `nix show-config`

## Name

`nix show-config` - show the Nix configuration

## Synopsis

`nix show-config` [*flags*...] 

## Flags

  - `--json`  
    produce JSON output

# Subcommand `nix show-derivation`

## Name

`nix show-derivation` - show the contents of a store derivation

## Synopsis

`nix show-derivation` [*flags*...] *installables*...

## Flags

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--recursive` / `r`  
    include the dependencies of the specified derivations

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To show the store derivation that results from evaluating the Hello package:

```console
nix show-derivation nixpkgs#hello
```

To show the full derivation graph (if available) that produced your NixOS system:

```console
nix show-derivation -r /run/current-system
```

# Subcommand `nix sign-paths`

## Name

`nix sign-paths` - sign the specified paths

## Synopsis

`nix sign-paths` [*flags*...] *installables*...

## Flags

  - `--all`  
    apply operation to the entire store

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--key-file` / `k` *file*  
    file containing the secret signing key

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--recursive` / `r`  
    apply operation to closure of the specified paths

  - `--update-input` *input-path*  
    update a specific flake input

# Subcommand `nix to-base16`

## Name

`nix to-base16` - convert a hash to base-16 representation

## Synopsis

`nix to-base16` [*flags*...] *strings*...

## Flags

  - `--type` *hash-algo*  
    hash algorithm ('md5', 'sha1', 'sha256', or 'sha512'). Optional as can also be gotten from SRI hash itself.

# Subcommand `nix to-base32`

## Name

`nix to-base32` - convert a hash to base-32 representation

## Synopsis

`nix to-base32` [*flags*...] *strings*...

## Flags

  - `--type` *hash-algo*  
    hash algorithm ('md5', 'sha1', 'sha256', or 'sha512'). Optional as can also be gotten from SRI hash itself.

# Subcommand `nix to-base64`

## Name

`nix to-base64` - convert a hash to base-64 representation

## Synopsis

`nix to-base64` [*flags*...] *strings*...

## Flags

  - `--type` *hash-algo*  
    hash algorithm ('md5', 'sha1', 'sha256', or 'sha512'). Optional as can also be gotten from SRI hash itself.

# Subcommand `nix to-sri`

## Name

`nix to-sri` - convert a hash to SRI representation

## Synopsis

`nix to-sri` [*flags*...] *strings*...

## Flags

  - `--type` *hash-algo*  
    hash algorithm ('md5', 'sha1', 'sha256', or 'sha512'). Optional as can also be gotten from SRI hash itself.

# Subcommand `nix upgrade-nix`

## Name

`nix upgrade-nix` - upgrade Nix to the latest stable version

## Synopsis

`nix upgrade-nix` [*flags*...] 

## Flags

  - `--dry-run`  
    show what this command would do without doing it

  - `--nix-store-paths-url` *url*  
    URL of the file that contains the store paths of the latest Nix release

  - `--profile` / `p` *profile-dir*  
    the Nix profile to upgrade

## Examples

To upgrade Nix to the latest stable version:

```console
nix upgrade-nix
```

To upgrade Nix in a specific profile:

```console
nix upgrade-nix -p /nix/var/nix/profiles/per-user/alice/profile
```

# Subcommand `nix verify`

## Name

`nix verify` - verify the integrity of store paths

## Synopsis

`nix verify` [*flags*...] *installables*...

## Flags

  - `--all`  
    apply operation to the entire store

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-contents`  
    do not verify the contents of each store path

  - `--no-registries`  
    don't use flake registries

  - `--no-trust`  
    do not verify whether each store path is trusted

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--recursive` / `r`  
    apply operation to closure of the specified paths

  - `--sigs-needed` / `n` *N*  
    require that each path has at least N valid signatures

  - `--substituter` / `s` *store-uri*  
    use signatures from specified store

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To verify the entire Nix store:

```console
nix verify --all
```

To check whether each path in the closure of Firefox has at least 2 signatures:

```console
nix verify -r -n2 --no-contents $(type -p firefox)
```

# Subcommand `nix why-depends`

## Name

`nix why-depends` - show why a package has another package in its closure

## Synopsis

`nix why-depends` [*flags*...] *package* *dependency*

## Flags

  - `--all` / `a`  
    show all edges in the dependency graph leading from 'package' to 'dependency', rather than just a shortest path

  - `--arg` *name* *expr*  
    argument to be passed to Nix functions

  - `--argstr` *name* *string*  
    string-valued argument to be passed to Nix functions

  - `--commit-lock-file`  
    commit changes to the lock file

  - `--derivation`  
    operate on the store derivation rather than its outputs

  - `--expr` *expr*  
    evaluate attributes from *expr*

  - `--file` / `f` *file*  
    evaluate *file* rather than the default

  - `--impure`  
    allow access to mutable paths and repositories

  - `--include` / `I` *path*  
    add a path to the list of locations used to look up `<...>` file names

  - `--inputs-from` *flake-url*  
    use the inputs of the specified flake as registry entries

  - `--no-registries`  
    don't use flake registries

  - `--no-update-lock-file`  
    do not allow any updates to the lock file

  - `--no-write-lock-file`  
    do not write the newly generated lock file

  - `--override-flake` *original-ref* *resolved-ref*  
    override a flake registry value

  - `--override-input` *input-path* *flake-url*  
    override a specific flake input (e.g. `dwarffs/nixpkgs`)

  - `--recreate-lock-file`  
    recreate lock file from scratch

  - `--update-input` *input-path*  
    update a specific flake input

## Examples

To show one path through the dependency graph leading from Hello to Glibc:

```console
nix why-depends nixpkgs#hello nixpkgs#glibc
```

To show all files and paths in the dependency graph leading from Thunderbird to libX11:

```console
nix why-depends --all nixpkgs#thunderbird nixpkgs#xorg.libX11
```

To show why Glibc depends on itself:

```console
nix why-depends nixpkgs#glibc nixpkgs#glibc
```

