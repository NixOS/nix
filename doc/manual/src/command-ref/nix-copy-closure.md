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
and second to send the dump of those paths.  When using public key
authentication, you can avoid typing the passphrase with `ssh-agent`.

# Options

  - `--to`\
    Copy the closure of _paths_ from the local Nix store to the Nix
    store on _machine_. This is the default.

  - `--from`\
    Copy the closure of _paths_ from the Nix store on _machine_ to the
    local Nix store.

  - `--gzip`\
    Enable compression of the SSH connection.

  - `--include-outputs`\
    Also copy the outputs of [store derivation]s included in the closure.

    [store derivation]: @docroot@/glossary.md#gloss-store-derivation

  - `--use-substitutes` / `-s`\
    Attempt to download missing paths on the target machine using Nix’s
    substitute mechanism.  Any paths that cannot be substituted on the
    target are still copied normally from the source.  This is useful,
    for instance, if the connection between the source and target
    machine is slow, but the connection between the target machine and
    `nixos.org` (the default binary cache server) is
    fast.

  - `-v`\
    Show verbose output.

{{#include ./opt-common.md}}

# Environment variables

  - `NIX_SSHOPTS`\
    Additional options to be passed to `ssh` on the command
    line.

{{#include ./env-common.md}}

# Examples

> **Example**
>
> Copy GNU Hello with all its dependencies to a remote machine:
>
> ```shell-session
> $ storePath="$(nix-build '<nixpkgs>' -I nixpkgs=channel:nixpkgs-unstable -A hello --no-out-link)"
> $ nix-copy-closure --to alice@itchy.example.org "$storePath"
> copying 5 paths...
> copying path '/nix/store/nrwkk6ak3rgkrxbqhsscb01jpzmslf2r-xgcc-13.2.0-libgcc' to 'ssh://alice@itchy.example.org'...
> copying path '/nix/store/gm61h1y42pqyl6178g90x8zm22n6pyy5-libunistring-1.1' to 'ssh://alice@itchy.example.org'...
> copying path '/nix/store/ddfzjdykw67s20c35i7a6624by3iz5jv-libidn2-2.3.7' to 'ssh://alice@itchy.example.org'...
> copying path '/nix/store/apab5i73dqa09wx0q27b6fbhd1r18ihl-glibc-2.39-31' to 'ssh://alice@itchy.example.org'...
> copying path '/nix/store/g1n2vryg06amvcc1avb2mcq36faly0mh-hello-2.12.1' to 'ssh://alice@itchy.example.org'...
> ```

> **Example**
>
> Copy GNU Hello from a remote machine using a known store path, and run it:
>
> ```shell-session
> $ storePath=/nix/store/g1n2vryg06amvcc1avb2mcq36faly0mh-hello-2.12.1
> $ nix-copy-closure --from alice@itchy.example.org "$storePath"
> $ "$storePath"/bin/hello
> ```
