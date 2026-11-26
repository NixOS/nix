# Installation

This section describes how to install and configure Nix for first-time use.

The current recommended option on Linux and MacOS is [multi-user](#multi-user).

## Multi-user

This installation offers better sharing, improved isolation, and more security
over a single user installation.

Use the official [Nix Installer](https://github.com/NixOS/nix-installer) on:

* Linux running systemd (`x86_64` or `aarch64`)
* macOS on Apple Silicon (`aarch64`)
* WSL2 with systemd enabled (`x86_64` or `aarch64`)

> **Updating to macOS 15 Sequoia**
>
> If you recently updated to macOS 15 Sequoia and are getting
> ```console
> error: the user '_nixbld1' in the group 'nixbld' does not exist
> ```
> when running Nix commands, refer to GitHub issue [NixOS/nix#10892](https://github.com/NixOS/nix/issues/10892) for instructions to fix your installation without reinstalling.

```console
$ curl -sSfL https://channels.nixos.org/nix-latest/nix-installer | sh -s -- install
```

The installer enables the experimental `nix-command` interface and asks whether to enable `flakes`, unless you select it with `--enable-flakes`.
To add a system-wide `nixpkgs` channel, pass `--add-channel`.

The installer writes its settings to `/etc/nix/nix.conf`, which includes `/etc/nix/nix.custom.conf` for local settings.
For other supported Linux architectures, see the [shell installer instructions](shell-installer.md).

See [Uninstalling Nix](uninstall.md#nix-installer) to remove this installation.

## Single-user

> Single-user is not supported on Mac.

> `warning: installing Nix as root is not supported by this script!`

This installation has less requirements than the multi-user install, however it
cannot offer equivalent sharing, isolation, or security.

This option uses the shell installer and is suitable for systems without systemd.

```console
$ curl -L https://channels.nixos.org/nix-latest/install | sh -s -- --no-daemon
```

## Distributions

The Nix community maintains installers for several distributions.

They can be found in the [`nix-community/nix-installers`](https://github.com/nix-community/nix-installers) repository.
