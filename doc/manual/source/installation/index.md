# Installation

We recommend that macOS users install Determinate Nix using our graphical installer, [Determinate.pkg][pkg].
For Linux and Windows Subsystem for Linux (WSL) users:

```console
curl --proto '=https' --tlsv1.2 -sSf -L https://install.determinate.systems/nix | \
  sh -s -- install --determinate
```

## Distributions

The Nix community maintains installers for several distributions.

They can be found in the [`nix-community/nix-installers`](https://github.com/nix-community/nix-installers) repository.

[pkg]: https://install.determinate.systems/determinate-pkg/stable/Universal
