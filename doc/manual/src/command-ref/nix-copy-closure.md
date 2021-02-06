# Name

`nix-copy-closure` - copy a closure to or from a remote machine via SSH

# Synopsis

`nix-copy-closure`
  [`--to` | `--from`]
  [`--gzip`]
  [`--include-outputs`]
  [`--use-substitutes` | `-s`]
  [`-v`]
  _user@machine_ _paths_

# Description

`nix-copy-closure` gives you an easy and efficient way to exchange
software between machines.  Given one or more Nix store _paths_ on the
local machine, `nix-copy-closure` computes the closure of those paths
(i.e. all their dependencies in the Nix store), and copies all paths
in the closure to the remote machine via the `ssh` (Secure Shell)
command.  With the `--from` option, the direction is reversed: the
closure of _paths_ on a remote machine is copied to the Nix store on
the local machine.

This command is efficient because it only sends the store paths
that are missing on the target machine.

Since `nix-copy-closure` calls `ssh`, you may be asked to type in the
appropriate password or passphrase.  In fact, you may be asked _twice_
because `nix-copy-closure` currently connects twice to the remote
machine, first to get the set of paths missing on the target machine,
and second to send the dump of those paths.  If this bothers you, use
`ssh-agent`.

# Options

  - `--to`  
    Copy the closure of _paths_ from the local Nix store to the Nix
    store on _machine_. This is the default.

  - `--from`  
    Copy the closure of _paths_ from the Nix store on _machine_ to the
    local Nix store.

  - `--gzip`  
    Enable compression of the SSH connection.

  - `--include-outputs`  
    Also copy the outputs of store derivations included in the closure.

  - `--use-substitutes` / `-s`  
    Attempt to download missing paths on the target machine using Nix’s
    substitute mechanism.  Any paths that cannot be substituted on the
    target are still copied normally from the source.  This is useful,
    for instance, if the connection between the source and target
    machine is slow, but the connection between the target machine and
    `nixos.org` (the default binary cache server) is
    fast.

  - `-v`  
    Show verbose output.

# Environment variables

  - `NIX_SSHOPTS`  
    Additional options to be passed to `ssh` on the command
    line.

# Examples

Copy Firefox with all its dependencies to a remote machine:

```console
$ nix-copy-closure --to alice@itchy.labs $(type -tP firefox)
```

Copy Subversion from a remote machine and then install it into a user
environment:

```console
$ nix-copy-closure --from alice@itchy.labs \
    /nix/store/0dj0503hjxy5mbwlafv1rsbdiyx1gkdy-subversion-1.4.4
$ nix-env -i /nix/store/0dj0503hjxy5mbwlafv1rsbdiyx1gkdy-subversion-1.4.4
```
