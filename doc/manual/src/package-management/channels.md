# Channels

If you want to stay up to date with a set of packages, it’s not very
convenient to manually download the latest set of Nix expressions for
those packages and upgrade using `nix-env`. Fortunately, there’s a
better way: *Nix channels*.

A Nix channel is just a URL that points to a place that contains a set
of Nix expressions and a manifest. Using the command
[`nix-channel`](../command-ref/nix-channel.md) you can automatically
stay up to date with whatever is available at that URL.

To see the list of official NixOS channels, visit
<https://nixos.org/channels>.

You can “subscribe” to a channel using `nix-channel --add`, e.g.,

```console
$ nix-channel --add https://nixos.org/channels/nixpkgs-unstable
```

subscribes you to a channel that always contains that latest version of
the Nix Packages collection. (Subscribing really just means that the URL
is added to the file `~/.nix-channels`, where it is read by subsequent
calls to `nix-channel
--update`.) You can “unsubscribe” using `nix-channel
--remove`:

```console
$ nix-channel --remove nixpkgs
```

To obtain the latest Nix expressions available in a channel, do

```console
$ nix-channel --update
```

This downloads and unpacks the Nix expressions in every channel
(downloaded from `url/nixexprs.tar.bz2`). It also makes the union of
each channel’s Nix expressions available by default to `nix-env`
operations (via the symlink `~/.nix-defexpr/channels`). Consequently,
you can then say

```console
$ nix-env -u
```

to upgrade all packages in your profile to the latest versions available
in the subscribed channels.
