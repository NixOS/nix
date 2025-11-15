# Name

`nix-channel` - manage Nix channels

# Synopsis

`nix-channel` {`--add` url [*name*] | `--remove` *name* | `--list` | `--update` [*names…*] | `--list-generations` | `--rollback` [*generation*] }

# Description

Channels are a mechanism for referencing remote Nix expressions and conveniently retrieving their latest version.

The moving parts of channels are:
- The official channels listed at <https://channels.nixos.org>
- The user-specific list of [subscribed channels](#subscribed-channels)
- The [downloaded channel contents](#channels)
- The [Nix expression search path](@docroot@/command-ref/conf-file.md#conf-nix-path), set with the [`-I` option](#opt-I) or the [`NIX_PATH` environment variable](#env-NIX_PATH)

> **Note**
>
> The state of a subscribed channel is external to the Nix expressions relying on it.
> This may limit reproducibility.
>
> Dependencies on other Nix expressions can be declared explicitly with:
> - [`fetchurl`](@docroot@/language/builtins.md#builtins-fetchurl), [`fetchTarball`](@docroot@/language/builtins.md#builtins-fetchTarball), or [`fetchGit`](@docroot@/language/builtins.md#builtins-fetchGit) in Nix expressions
> - the [`-I` option](@docroot@/command-ref/opt-common.md#opt-I) in command line invocations

This command has the following operations:

- `--add` *url* \[*name*\]

  Add a channel *name* located at *url* to the list of subscribed channels.
  If *name* is omitted, default to the last component of *url*, with the suffixes `-stable` or `-unstable` removed.

  > **Note**
  >
  > `--add` does not automatically perform an update.
  > Use `--update` explicitly.

  A channel URL must point to a directory containing a file `nixexprs.tar.gz`.
  At the top level, that tarball must contain a single directory with a `default.nix` file that serves as the channel’s entry point.

- `--remove` *name*

  Remove the channel *name* from the list of subscribed channels.

- `--list`

  Print the names and URLs of all subscribed channels on standard output.

- `--update` \[*names*…\]

  Download the Nix expressions of subscribed channels and create a new generation.
  Update all channels if none is specified, and only those included in *names* otherwise.

  > **Note**
  >
  > Downloaded channel contents are cached.
  > Use `--tarball-ttl` or the [`tarball-ttl` configuration option](@docroot@/command-ref/conf-file.md#conf-tarball-ttl) to change the validity period of cached downloads.

- `--list-generations`

  Prints a list of all the current existing generations for the
  channel profile.

  Works the same way as
  ```
  nix-env --profile /nix/var/nix/profiles/per-user/$USER/channels --list-generations
  ```

- `--rollback` \[*generation*\]

  Revert channels to the state before the last call to `nix-channel --update`.
  Optionally, you can specify a specific channel *generation* number to restore.

{{#include ./opt-common.md}}

{{#include ./env-common.md}}

# Files

`nix-channel` operates on the following files.

{{#include ./files/channels.md}}

# Examples

Subscribe to the Nixpkgs channel and run `hello` from the GNU Hello package:

```console
$ nix-channel --add https://channels.nixos.org/nixpkgs-unstable
$ nix-channel --list
nixpkgs https://channels.nixos.org/nixpkgs
$ nix-channel --update
$ nix-shell -p hello --run hello
hello
```

Revert channel updates using `--rollback`:

```console
$ nix-instantiate --eval '<nixpkgs>' --attr lib.version
"22.11pre296212.530a53dcbc9"

$ nix-channel --rollback
switching from generation 483 to 482

$ nix-instantiate --eval '<nixpkgs>' --attr lib.version
"22.11pre281526.d0419badfad"
```

Remove a channel:

```console
$ nix-channel --remove nixpkgs
$ nix-channel --list
```
