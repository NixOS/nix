# Installation

This section describes how to install and configure Nix for first-time use.

The current recommended option on Linux and MacOS is [multi-user](#multi-user).

## Multi-user

This installation offers better sharing, improved isolation, and more security
over a single user installation.

This option requires either:

* Linux running systemd, with SELinux disabled
* MacOS

> **Updating to macOS 15 Sequoia**
>
> If you recently updated to macOS 15 Sequoia and are getting
> ```console
> error: the user '_nixbld1' in the group 'nixbld' does not exist
> ```
> when running Nix commands, refer to GitHub issue [NixOS/nix#10892](https://github.com/NixOS/nix/issues/10892) for instructions to fix your installation without reinstalling.

```console
$ curl -L https://nixos.org/nix/install | sh -s -- --daemon
```

## Single-user

> Single-user is not supported on Mac.

> `warning: installing Nix as root is not supported by this script!`

This installation has less requirements than the multi-user install, however it
cannot offer equivalent sharing, isolation, or security.

This option is suitable for systems without systemd.

```console
$ curl -L https://nixos.org/nix/install | sh -s -- --no-daemon
```

## Distributions

The Nix community maintains installers for several distributions.

They can be found in the [`nix-community/nix-installers`](https://github.com/nix-community/nix-installers) repository.
