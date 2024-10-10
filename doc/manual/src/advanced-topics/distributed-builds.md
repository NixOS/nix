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
$ nix store info --store ssh://mac
```

will try to connect to the machine named `mac`. It is possible to
specify an SSH identity file as part of the remote store URI, e.g.

```console
$ nix store info --store ssh://mac?ssh-key=/home/alice/my-key
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

The [list of remote build machines](@docroot@/command-ref/conf-file.md#conf-builders) can be specified on the command line or in the Nix configuration file.
For example, the following command allows you to build a derivation for `x86_64-darwin` on a Linux machine:

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

It is possible to specify multiple build machines separated by a semicolon or a newline, e.g.

```console
  --builders 'ssh://mac x86_64-darwin ; ssh://beastie x86_64-freebsd'
```

Remote build machines can also be configured in [`nix.conf`](@docroot@/command-ref/conf-file.md), e.g.

    builders = ssh://mac x86_64-darwin ; ssh://beastie x86_64-freebsd

Finally, remote build machines can be configured in a separate configuration
file included in `builders` via the syntax `@/path/to/file`. For example,

    builders = @/etc/nix/machines

causes the list of machines in `/etc/nix/machines` to be included.
(This is the default.)
