# Installation

This section describes how to install and configure Nix for first-time use.
Nix follows a [multi-user](./nix-security.md#multi-user-model) model on both Linux
and macOS.

```console
$ curl -L https://nixos.org/nix/install | sh -s -- --daemon
```

> **Updating to macOS 15 Sequoia**
>
> If you recently updated to macOS 15 Sequoia and are getting
> ```console
> error: the user '_nixbld1' in the group 'nixbld' does not exist
> ```
> when running Nix commands, refer to GitHub issue [NixOS/nix#10892](https://github.com/NixOS/nix/issues/10892) for instructions to fix your installation without reinstalling.

## Distributions

The Nix community maintains installers for several distributions.

They can be found in the [`nix-community/nix-installers`](https://github.com/nix-community/nix-installers) repository.
