# Upgrading Nix

> **Note**
>
> These upgrade instructions apply for regular Linux distributions where Nix was installed following the [installation instructions in this manual](./index.md).

First, find the name of the current [channel](@docroot@/command-ref/nix-channel) through which Nix is distributed:

```console
$ nix-channel --list
```

By default this should return an entry for Nixpkgs:

```console
nixpkgs https://nixos.org/channels/nixpkgs-23.05
```

Check which Nix version will be installed:

```console
$ nix-shell -p nix -I nixpkgs=channel:nixpkgs-23.11 --run "nix --version"
nix (Nix) 2.18.1
```

> **Warning**
>
> Writing to the [local store](@docroot@/store/types/local-store.md) with a newer version of Nix, for example by building derivations with `nix-build` or `nix-store --realise`, may change the database schema!
> Reverting to an older version of Nix may therefore require purging the store database before it can be used.

Update the channel entry:

```console
$ nix-channel --remove nixpkgs
$ nix-channel --add https://nixos.org/channels/nixpkgs-23.11 nixpkgs
```

Multi-user Nix users on macOS can upgrade Nix by running: `sudo -i sh -c
'nix-channel --update &&
nix-env --install --attr nixpkgs.nix &&
launchctl remove org.nixos.nix-daemon &&
launchctl load /Library/LaunchDaemons/org.nixos.nix-daemon.plist'`

Single-user installations of Nix should run this: `nix-channel --update;
nix-env --install --attr nixpkgs.nix nixpkgs.cacert`

Multi-user Nix users on Linux should run this with sudo: `nix-channel
--update; nix-env --install --attr nixpkgs.nix nixpkgs.cacert; systemctl
daemon-reload; systemctl restart nix-daemon`
