# Name

`nix-copy-closure` - copy store objects to or from a remote machine via SSH

# Synopsis

`nix-copy-closure`
  [`--to` _machine_ | `--from` _machine_]
  [`--gzip`]
  [`--include-outputs`]
  [`--use-substitutes` | `-s`]
  [`-v`]
  _user@machine_ _paths_

# Description

`nix-copy-closure` copies [store objects](@docroot@/glossary.md#gloss-store-object) along with all their dependencies to or from another machine via the SSH protocol.
It doesn’t copy store objects that are already present on the target machine.

Given one or more _paths_ accesible from the client, `nix-copy-closure` computes the [closure](@docroot@/glossary.md#gloss-closure) of those paths (i.e. all their dependencies in the Nix store), and copies all store objects in the closure to the remote machine via SSH.
With the `--from` option, the direction is reversed:
The closure of _paths_ on a remote machine is copied to the specified Nix store accessible from the client.

This command is efficient because it only sends the store paths that are missing on the target machine.

> **Note**
>
> While the Nix store to use on the client can be specified on the command line with the [`--store`](@docroot@/command-ref/conf-file.md#conf-store) option, the Nix store to be accessed on the remote machine can only be [configured statically](@docroot@/command-ref/conf-file.md#configuration-file) on that remote machine.

Since `nix-copy-closure` calls `ssh`, you may need to authenticate with the remote machine.
In fact, you may be asked for authentication _twice_ because `nix-copy-closure` currently connects twice to the remote machine: first to get the set of paths missing on the target machine, and second to send the dump of those paths.
When using public key authentication, you can avoid typing the passphrase with `ssh-agent`.

# Options

  - `--to` _machine_

    Copy the closure of _paths_ from the local Nix store to the Nix store on _machine_.
    This is the default.

  - `--from` _machine_

    Copy the closure of _paths_ from the Nix store on _machine_ to the local Nix store.

  - `--gzip`

    Enable compression of the SSH connection.

  - `--include-outputs`

    Also copy the outputs of [store derivation]s included in the closure.

    [store derivation]: @docroot@/glossary.md#gloss-store-derivation

  - `--use-substitutes` / `-s`

    Attempt to download missing store objects on the target from [substituters](@docroot@/command-ref/conf-file.md#conf-substituters).
    Any store objects that cannot be substituted on the target are still copied normally from the source.
    This is useful, for instance, if the connection between the source and target machine is slow, but the connection between the target machine and `cache.nixos.org` (the default binary cache server) is fast.

{{#include ./opt-common.md}}

# Environment variables

  - `NIX_SSHOPTS`

    Additional options to be passed to `ssh` on the command line.

{{#include ./env-common.md}}

# Examples

Copy Firefox with all its dependencies to a remote machine:

```console
$ nix-copy-closure --to alice@itchy.example.org $(type -P firefox)
```

Copy Subversion from a remote machine and then install it into a user
environment:

```console
$ nix-copy-closure --from alice@itchy.example.org \
    /nix/store/0dj0503hjxy5mbwlafv1rsbdiyx1gkdy-subversion-1.4.4
$ nix-env --install /nix/store/0dj0503hjxy5mbwlafv1rsbdiyx1gkdy-subversion-1.4.4
```
