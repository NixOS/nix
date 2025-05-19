# Determinate Nix

[![CI](https://github.com/DeterminateSystems/nix-src/workflows/CI/badge.svg)](https://github.com/DeterminateSystems/nix-src/actions/workflows/ci.yml)

This repository houses the source for [Determinate Nix][det-nix], a downstream distribution of [Nix][upstream].
Nix is a powerful language, package manager, and CLI for Linux and other Unix systems that makes package management reliable and reproducible.

---

## Installation and first steps

Visit [nix.dev](https://nix.dev) for [installation instructions](https://nix.dev/tutorials/install-nix) and [beginner tutorials](https://nix.dev/tutorials/first-steps).

Full reference documentation can be found in the [Nix manual](https://nix.dev/reference/nix-manual).

## Building and developing

Follow instructions in the Nix reference manual to [set up a development environment and build Nix from source](https://nix.dev/manual/nix/development/development/building.html).

## Contributing

Check the [contributing guide](./CONTRIBUTING.md) if you want to get involved with developing Nix.

## Additional resources

Nix was created by Eelco Dolstra and developed as the subject of his PhD thesis [The Purely Functional Software Deployment Model](https://edolstra.github.io/pubs/phd-thesis.pdf), published 2006.
Today, a world-wide developer community contributes to Nix and the ecosystem that has grown around it.

- [The Nix, Nixpkgs, NixOS Community on nixos.org][website]
- [Official documentation on nix.dev][nix.dev]
- [NixOS] is a Linux distribution that can be configured fully declaratively
- [Discourse]
- [Matrix]

## License

[Upstream Nix][upstream] is released under the [LGPL v2.1][license] license.
[Determinate Nix][det-nix] is also released under LGPL v2.1 based on the terms of that license.

[det-nix]: https://docs.determinate.systems/determinate-nix
[discourse]: https://discourse.nixos.org
[license]: ./COPYING
[matrix]: https://matrix.to/#/#nix:nixos.org
[nix.dev]: https://nix.dev
[nixos]: https://github.com/NixOS/nixpkgs/tree/master/nixos
[upstream]: https://github.com/NixOS/nix
[website]: https://nixos.org
