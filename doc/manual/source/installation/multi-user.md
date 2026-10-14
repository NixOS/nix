# Multi-User Mode

When the _store_ is shared between multiple users, store paths serve as
unique identifiers of packages that reside in a shared namespace.
Thus, it is important that users are not able to interact with it in arbitrary ways,
lest the store objects referenced by another user (or a system service) could
be hijacked.

In multi-user configuration, the Nix _store_, database and other data (e.g.
build logs) are owned by some privileged user (usually `root`) and builders are
executed under special user accounts (usually named `nixbld1`, `nixbld2`, etc.) that
are part of the special group (usually `nixbld`, configurable via
[`build-users-group`](@docroot@/command-ref/conf-file.md#conf-build-users-group)).
When an unprivileged user runs a Nix command, actions that operate on the Nix _store_
(such as builds) are forwarded to a *Nix daemon* running under the owner
of the Nix store/database that performs the operation.

## Trusted and untrusted users

The *daemon* differentiates between different trust levels of the users that are allowed
to request that operartions are performed on their behalf.

Namely, the set of users that the daemon accepts connections from is configured by
the [`allowed-users`](@docroot@/command-ref/conf-file.md#conf-allowed-users) option. Unless
otherwise configured via [`trusted-users`](@docroot@/command-ref/conf-file.md#conf-trusted-users) such
users are *untrusted*. The daemon also rejects connections from the *build users group*.

Instead of relying on the daemon to reject connections, you can also use
filesystem permissions to restrict access to the socket
(located by default in the `/nix/var/nix/daemon-socket/` directory):

```console
$ chgrp nix-users /nix/var/nix/daemon-socket
$ chmod ug=rwx,o= /nix/var/nix/daemon-socket
```

Connections from those users and/or groups are privileged to perform
only a limited set of package management operations that is supposed to limit
their ability to interfere with or influence the _store_ contents in a number of ways:

- [Store objects](@docroot@/store/store-object.md) can only be added if:
  - They are [content-addressed](@docroot@/store/store-object/content-address.md),
    meaning that their store path is computed (and can be verified) based on their contents.
    This includes [derivations](@docroot@/glossary.md#gloss-store-derivation) that also need to uphold more invariants.
    <!-- Not sure it's worth going into the details of checkInvariants. -->
  - If they are [input-addressed](@docroot@/glossary.md#gloss-input-addressed-store-object)
    *and* its metadata is signed by [`trusted-public-keys`](@docroot@/command-ref/conf-file.md#conf-trusted-public-keys)
    that is configured for the *daemon* process.

- Only [store derivations](@docroot@/glossary.md#gloss-store-derivation) can be built.
  This also implies that the whole derivation [closure](@docroot@/glossary.md#gloss-closure) needs to be copied
  to the _store_. This is because the only way to verify that the input addressed output paths encoded in the
  derivation is legitimate is to re-examine the whole closure (though this invariant is maintained incrementally by
  enforcing that each derivation added to the store is valid).
  This requirement might be fine for using Nix locally, but is counterproductive for remote building, hence why
  in practice remote building currently requires that the remote builder trusts the user triggering the build.

- Trigger garbage collection and collect the set of live paths without disclosing why those store paths are live.

- Configure [`substituters`](@docroot@/command-ref/conf-file.md#conf-substituters) that have been explicitly white-listed by the daemon via
  [`trusted-substituters`](@docroot@/command-ref/conf-file.md#conf-trusted-substituters) daemon option.
  [`trusted-public-keys`](@docroot@/command-ref/conf-file.md#conf-trusted-public-keys) is what determines which mappings
  between input addressed store paths and their contents are trusted by the Nix daemon and considered for substitution.
  However, [`substituters`](@docroot@/command-ref/conf-file.md#conf-substituters) should be used with care, because
  the [NAR](@docroot@/glossary.md#gloss-nar) hash and size that gets signed can only be verified once the whole store object
  is downloaded and unpacked to a temporary location. Notably, the location from which NARs are downloaded is not part of the
  signature. This means that malicious substituters can "spoof" substitutable paths by exposing a `.narinfo` file that has
  a signature from a legitimate cache (such as [cache.nixos.org](https://cache.nixos.org)).
  Even though (to the best of our knowledge) Nix is hardened against malicious substituters, it is best to limit the set of
  `trusted-substituters` to reduce potential attack surface.

Nonetheless, even *untrusted* users can trigger arbitrary builds, downloads and insert any content into the _store_ --
just under unique addresses that are correctly computed. Hardening against DoS (Denial of Service) attacks on the _daemon_
is considered out of scope. You should limit the set of [`allowed-users`](@docroot@/command-ref/conf-file.md#conf-allowed-users)
to the smallest set of users that have a legitimate need to interact with it.

### Trusted users

The above limitations don't apply to [`trusted-users`](@docroot@/command-ref/conf-file.md#conf-trusted-users) that serves
to give the same privileges to the user it would have if it were to able to operate on the _store_ directly.

> [!NOTE]
>
> Due to aforementioned [deficiencies of the input addressing scheme](https://github.com/NixOS/nix/issues/9259), this
> feature is in widespread use for remote building.
>
> Prefer to set daemon options directly when possible.
> Only resort to enabling this feature if you deem the capabilities described
> below to be safe to grant for your use-case.

Such users are allowed to operate on the store almost as if they had direct write access to it.
They can:

- Insert content at arbitrary store paths or replace existing store objects.
- Delete arbitrary store objects regardless of whether they are GC roots or not.
- Configure hooks that are executed in the context of the daemon process without sandboxing
  as the user running the daemon.
- Apply arbitrary settings to the daemon, including `substituters` and `trusted-public-keys`.

This list is non-exhaustive. For all intents and purposes, this feature allows you to enable a user
to run arbitrary code as the daemon user (unless configured otherwise, `root`) and have full control
over the store.

If you think about enabling this feature for a default Nix installation, a good
rule of thumb is to ask whether you'd give that user passwordless `sudo`.

> [!CAUTION]
> When [combined](https://github.com/NixOS/nix/issues/9649) with [`accept-flake-config`](@docroot@/command-ref/conf-file.md#conf-accept-flake-config),
> it makes for an incredibly dangerous mechanism that allows flakes to execute code on your machine
> as the daemon user.
> Never combine these two features on machines that evaluate untrusted nix expressions.

## Build sandboxing

> [!NOTE]
>
> This section isn't specific to daemon installations.

In addition to being performed by special users, [derivation builders](@docroot@/language/derivations.md#attr-builder) are executed
in a sandbox on platforms that provide such facilities (e.g. Linux namespaces and FreeBSD jails). It can configured via [`sandbox`](@docroot@/command-ref/conf-file.md#conf-sandbox) setting.

<!-- TODO: Maybe describe the specifics of which derivations are sandboxed how
     and how that interacts with "relaxed" sandbox mode. -->

The primary purpose of this sandboxing is not to be resilient against malicious
derivations, but to aid in ensuring build purity and isolation. Additionally,
these sandboxes need to be permissive enough to build, run and test most
software.

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

The [Nix daemon](../command-ref/nix-daemon.md) should be started as follows:

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
