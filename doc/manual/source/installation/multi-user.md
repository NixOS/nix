# Multi-User Mode

To allow a Nix store to be shared safely among multiple users, it is
important that users are not able to run builders that modify the Nix
store or database in arbitrary ways, or that interfere with builds
started by other users. If they could do so, they could install a Trojan
horse in some package and compromise the accounts of other users.

To prevent this, the Nix store and database are owned by some privileged
user (usually `root`) and builders are executed under special user
accounts (usually named `nixbld1`, `nixbld2`, etc.). When a unprivileged
user runs a Nix command, actions that operate on the Nix store (such as
builds) are forwarded to a *Nix daemon* running under the owner of the
Nix store/database that performs the operation.

> **Note**
> 
> Multi-user mode has one important limitation: only root and a set of
> trusted users specified in `nix.conf` can specify arbitrary binary
> caches. So while unprivileged users may install packages from
> arbitrary Nix expressions, they may not get pre-built binaries.

## Setting up the build users

The *build users* are the special UIDs under which builds are performed.
They should all be members of the *build users group* `nixbld`. This
group should have no other members. The build users should not be
members of any other group. On Linux, you can create the group and users
as follows:

```console
$ groupadd -r nixbld
$ for n in $(seq 1 10); do useradd -c "Nix build user $n" \
    -d /var/empty -g nixbld -G nixbld -M -N -r -s "$(which nologin)" \
    nixbld$n; done
```

This creates 10 build users. There can never be more concurrent builds
than the number of build users, so you may want to increase this if you
expect to do many builds at the same time.

## Running the daemon

The [Nix daemon](../command-ref/nix-daemon.md) should be started as
follows (as `root`):

```console
$ nix-daemon
```

You’ll want to put that line somewhere in your system’s boot scripts.

To let unprivileged users use the daemon, they should set the
[`NIX_REMOTE` environment variable](../command-ref/env-common.md) to
`daemon`. So you should put a line like

```console
export NIX_REMOTE=daemon
```

into the users’ login scripts.

## Restricting access

To limit which users can perform Nix operations, you can use the
permissions on the directory `/nix/var/nix/daemon-socket`. For instance,
if you want to restrict the use of Nix to the members of a group called
`nix-users`, do

```console
$ chgrp nix-users /nix/var/nix/daemon-socket
$ chmod ug=rwx,o= /nix/var/nix/daemon-socket
```

This way, users who are not in the `nix-users` group cannot connect to
the Unix domain socket `/nix/var/nix/daemon-socket/socket`, so they
cannot perform Nix operations.
