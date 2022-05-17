# Remote Builds

Nix supports remote builds, where a local Nix installation can forward
Nix builds to other machines. This allows multiple builds to be
performed in parallel and allows Nix to perform multi-platform builds in
a semi-transparent way. For instance, if you perform a build for a
`x86_64-darwin` on an `i686-linux` machine, Nix can automatically
forward the build to a `x86_64-darwin` machine, if available.

To forward a build to a remote machine, it’s required that the remote
machine is accessible via SSH and that it has Nix installed. You can
test whether connecting to the remote Nix instance works, e.g.

```console
$ nix ping-store --store ssh://mac
```

will try to connect to the machine named `mac`. It is possible to
specify an SSH identity file as part of the remote store URI, e.g.

```console
$ nix ping-store --store ssh://mac?ssh-key=/home/alice/my-key
```

Since builds should be non-interactive, the key should not have a
passphrase. Alternatively, you can load identities ahead of time into
`ssh-agent` or `gpg-agent`.

If you get the error

```console
bash: nix-store: command not found
error: cannot connect to 'mac'
```

then you need to ensure that the `PATH` of non-interactive login shells
contains Nix.

> **Warning**
>
> If you are building via the Nix daemon, it is the Nix daemon user
> account (that is, `root`) that should have SSH access to the remote
> machine. If you can’t or don’t want to configure `root` to be able to
> access to remote machine, you can use a private Nix store instead by
> passing e.g. `--store ~/my-nix`.

The list of remote machines can be specified on the command line or in
the Nix configuration file. The former is convenient for testing. For
example, the following command allows you to build a derivation for
`x86_64-darwin` on a Linux machine:

```console
$ uname
Linux

$ nix build --impure \
  --expr '(with import <nixpkgs> { system = "x86_64-darwin"; }; runCommand "foo" {} "uname > $out")' \
  --builders 'ssh://mac x86_64-darwin'
[1/0/1 built, 0.0 MiB DL] building foo on ssh://mac

$ cat ./result
Darwin
```

It is possible to specify multiple builders separated by a semicolon or
a newline, e.g.

```console
  --builders 'ssh://mac x86_64-darwin ; ssh://beastie x86_64-freebsd'
```

Each machine specification consists of the following elements, separated
by spaces. Only the first element is required. To leave a field at its
default, set it to `-`.

1.  The URI of the remote store in the format
    `ssh://[username@]hostname`, e.g. `ssh://nix@mac` or `ssh://mac`.
    For backward compatibility, `ssh://` may be omitted. The hostname
    may be an alias defined in your `~/.ssh/config`.

2.  A comma-separated list of Nix platform type identifiers, such as
    `x86_64-darwin`. It is possible for a machine to support multiple
    platform types, e.g., `i686-linux,x86_64-linux`. If omitted, this
    defaults to the local platform type.

3.  The SSH identity file to be used to log in to the remote machine. If
    omitted, SSH will use its regular identities.

4.  The maximum number of builds that Nix will execute in parallel on
    the machine. Typically this should be equal to the number of CPU
    cores. For instance, the machine `itchy` in the example will execute
    up to 8 builds in parallel.

5.  The “speed factor”, indicating the relative speed of the machine. If
    there are multiple machines of the right type, Nix will prefer the
    fastest, taking load into account.

6.  A comma-separated list of *supported features*. If a derivation has
    the `requiredSystemFeatures` attribute, then Nix will only perform
    the derivation on a machine that has the specified features. For
    instance, the attribute

    ```nix
    requiredSystemFeatures = [ "kvm" ];
    ```

    will cause the build to be performed on a machine that has the `kvm`
    feature.

7.  A comma-separated list of *mandatory features*. A machine will only
    be used to build a derivation if all of the machine’s mandatory
    features appear in the derivation’s `requiredSystemFeatures`
    attribute.

8.  The (base64-encoded) public host key of the remote machine. If omitted, SSH
    will use its regular known-hosts file. Specifically, the field is calculated
    via `base64 -w0 /etc/ssh/ssh_host_ed25519_key.pub`.

For example, the machine specification

    nix@scratchy.labs.cs.uu.nl  i686-linux      /home/nix/.ssh/id_scratchy_auto        8 1 kvm
    nix@itchy.labs.cs.uu.nl     i686-linux      /home/nix/.ssh/id_scratchy_auto        8 2
    nix@poochie.labs.cs.uu.nl   i686-linux      /home/nix/.ssh/id_scratchy_auto        1 2 kvm benchmark

specifies several machines that can perform `i686-linux` builds.
However, `poochie` will only do builds that have the attribute

```nix
requiredSystemFeatures = [ "benchmark" ];
```

or

```nix
requiredSystemFeatures = [ "benchmark" "kvm" ];
```

`itchy` cannot do builds that require `kvm`, but `scratchy` does support
such builds. For regular builds, `itchy` will be preferred over
`scratchy` because it has a higher speed factor.

Remote builders can also be configured in `nix.conf`, e.g.

    builders = ssh://mac x86_64-darwin ; ssh://beastie x86_64-freebsd

Finally, remote builders can be configured in a separate configuration
file included in `builders` via the syntax `@file`. For example,

    builders = @/etc/nix/machines

causes the list of machines in `/etc/nix/machines` to be included. (This
is the default.)

If you want the builders to use caches, you likely want to set the
option `builders-use-substitutes` in your local `nix.conf`.

To build only on remote builders and disable building on the local
machine, you can use the option `--max-jobs 0`.
