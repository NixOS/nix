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

# Examples

To subscribe to the Nixpkgs channel and install the GNU Hello package:

```console
$ nix-channel --add https://nixos.org/channels/nixpkgs-unstable
$ nix-channel --update
$ nix-env -iA nixpkgs.hello
```

You can revert channel updates using `--rollback`:

```console
$ nix-instantiate --eval -E '(import <nixpkgs> {}).lib.version'
"14.04.527.0e935f1"

$ nix-channel --rollback
switching from generation 483 to 482

$ nix-instantiate --eval -E '(import <nixpkgs> {}).lib.version'
"14.04.526.dbadfad"
```

# Files

  - `/nix/var/nix/profiles/per-user/username/channels`\
    `nix-channel` uses a `nix-env` profile to keep track of previous
    versions of the subscribed channels. Every time you run `nix-channel
    --update`, a new channel generation (that is, a symlink to the
    channel Nix expressions in the Nix store) is created. This enables
    `nix-channel --rollback` to revert to previous versions.

  - `~/.nix-defexpr/channels`\
    This is a symlink to
    `/nix/var/nix/profiles/per-user/username/channels`. It ensures that
    `nix-env` can find your channels. In a multi-user installation, you
    may also have `~/.nix-defexpr/channels_root`, which links to the
    channels of the root user.

# Channel format

A channel URL should point to a directory containing the following
files:

  - `nixexprs.tar.xz`\
    A tarball containing Nix expressions and files referenced by them
    (such as build scripts and patches). At the top level, the tarball
    should contain a single directory. That directory must contain a
    file `default.nix` that serves as the channel’s “entry point”.
