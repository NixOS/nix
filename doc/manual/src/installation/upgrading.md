# Upgrading Nix

> **Note**
>
> These upgrade instructions apply where Nix was installed following the [installation instructions in this manual](./index.md).

Check which Nix version will be installed, for example from one of the [release channels](http://channels.nixos.org/) such as `nixpkgs-unstable`:

```console
$ nix-shell -p nix -I nixpkgs=channel:nixpkgs-unstable --run "nix --version"
nix (Nix) 2.18.1
```

> **Warning**
>
> Writing to the [local store](@docroot@/store/types/local-store.md) with a newer version of Nix, for example by building derivations with [`nix-build`](@docroot@/command-ref/nix-build.md) or [`nix-store --realise`](@docroot@/command-ref/nix-store/realise.md), may change the database schema!
> Reverting to an older version of Nix may therefore require purging the store database before it can be used.

## Linux multi-user

```console
$ sudo su
# nix-env --install --file '<nixpkgs>' --attr nix cacert -I nixpkgs=channel:nixpkgs-unstable
# systemctl daemon-reload
# systemctl restart nix-daemon
```

## macOS multi-user

```console
$ sudo nix-env --install --file '<nixpkgs>' --attr nix cacert -I nixpkgs=channel:nixpkgs-unstable
$ sudo launchctl remove org.nixos.nix-daemon
$ sudo launchctl load /Library/LaunchDaemons/org.nixos.nix-daemon.plist
```

## Single-user all platforms

```console
$ nix-env --install --file '<nixpkgs>' --attr nix cacert -I nixpkgs=channel:nixpkgs-unstable
```
