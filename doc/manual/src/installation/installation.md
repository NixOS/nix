# Installation

This section describes how to install and configure Nix for first-time use.

The current recommended option on Linux and MacOS is [multi-user](#multi-user).

## Multi-user

This installation offers better sharing, improved isolation, and more security
over a single user installation.

This option requires either:

* Linux running systemd, with SELinux disabled
* MacOS

```console
$ bash <(curl -L https://nixos.org/nix/install) --daemon
```

## Single-user

> Single-user is not supported on Mac.

This installation has less requirements than the multi-user install, however it
cannot offer equivalent sharing, isolation, or security.

This option is suitable for systems without systemd.

```console
$ bash <(curl -L https://nixos.org/nix/install) --no-daemon
```

## Distributions

The Nix community maintains installers for several distributions.

They can be found in the [`nix-community/nix-installers`](https://github.com/nix-community/nix-installers) repository.
