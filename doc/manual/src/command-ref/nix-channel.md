# Name

`nix-channel` - manage Nix channels

# Synopsis

`nix-channel` {`--add` url [*name*] | `--remove` *name* | `--list` | `--update` [*names…*] | `--rollback` [*generation*] }

# Description

A Nix channel is a mechanism that allows you to automatically stay
up-to-date with a set of pre-built Nix expressions. A Nix channel is
just a URL that points to a place containing a set of Nix expressions.

To see the list of official NixOS channels, visit
<https://nixos.org/channels>.

This command has the following operations:

  - `--add` *url* \[*name*\]\
    Adds a channel named *name* with URL *url* to the list of subscribed
    channels. If *name* is omitted, it defaults to the last component of
    *url*, with the suffixes `-stable` or `-unstable` removed.

    A channel URL must point to a directory containing a file `nixexprs.tar.gz`.
    At the top level, that tarball must contain a single directory with a `default.nix` file that serves as the channel’s entry point.

  - `--remove` *name*\
    Removes the channel named *name* from the list of subscribed
    channels.

  - `--list`\
    Prints the names and URLs of all subscribed channels on standard
    output.

  - `--update` \[*names*…\]\
    Downloads the Nix expressions of all subscribed channels (or only
    those included in *names* if specified) and makes them the default
    for `nix-env` operations (by symlinking them from the directory
    `~/.nix-defexpr`).

  - `--rollback` \[*generation*\]\
    Reverts the previous call to `nix-channel
                    --update`. Optionally, you can specify a specific channel generation
    number to restore.

Note that `--add` does not automatically perform an update.

The list of subscribed channels is stored in `~/.nix-channels`.

{{#include ./opt-common.md}}

{{#include ./env-common.md}}

# Files

`nix-channel` operates on the following files.

{{#include ./files/channels.md}}

# Examples

To subscribe to the Nixpkgs channel and install the GNU Hello package:

```console
$ nix-channel --add https://nixos.org/channels/nixpkgs-unstable
$ nix-channel --update
$ nix-env --install --attr nixpkgs.hello
```

You can revert channel updates using `--rollback`:

```console
$ nix-instantiate --eval --expr '(import <nixpkgs> {}).lib.version'
"14.04.527.0e935f1"

$ nix-channel --rollback
switching from generation 483 to 482

$ nix-instantiate --eval --expr '(import <nixpkgs> {}).lib.version'
"14.04.526.dbadfad"
```
