<p align="center">
  <a href="https://determinate.systems" target="_blank"><img src="https://raw.githubusercontent.com/determinatesystems/.github/main/.github/banner.jpg"></a>
</p>
<p align="center">
  &nbsp;<a href="https://determinate.systems/discord" target="_blank"><img alt="Discord" src="https://img.shields.io/discord/1116012109709463613?style=for-the-badge&logo=discord&logoColor=%23ffffff&label=Discord&labelColor=%234253e8&color=%23e4e2e2"></a>&nbsp;
  &nbsp;<a href="https://bsky.app/profile/determinate.systems" target="_blank"><img alt="Bluesky" src="https://img.shields.io/badge/Bluesky-0772D8?style=for-the-badge&logo=bluesky&logoColor=%23ffffff"></a>&nbsp;
  &nbsp;<a href="https://hachyderm.io/@determinatesystems" target="_blank"><img alt="Mastodon" src="https://img.shields.io/badge/Mastodon-6468fa?style=for-the-badge&logo=mastodon&logoColor=%23ffffff"></a>&nbsp;
  &nbsp;<a href="https://twitter.com/DeterminateSys" target="_blank"><img alt="Twitter" src="https://img.shields.io/badge/Twitter-303030?style=for-the-badge&logo=x&logoColor=%23ffffff"></a>&nbsp;
  &nbsp;<a href="https://www.linkedin.com/company/determinate-systems" target="_blank"><img alt="LinkedIn" src="https://img.shields.io/badge/LinkedIn-1667be?style=for-the-badge&logo=linkedin&logoColor=%23ffffff"></a>&nbsp;
</p>

# Determinate Nix

[![CI](https://github.com/DeterminateSystems/nix-src/workflows/CI/badge.svg)](https://github.com/DeterminateSystems/nix-src/actions/workflows/ci.yml)

This repository houses the source for [**Determinate Nix**][det-nix], a downstream distribution of [Nix][upstream] created and maintained by [Determinate Systems][detsys].
Nix is a powerful language, package manager, and CLI for Linux and other Unix systems that makes package management reliable and reproducible.

Determinate Nix is

[Determinate]
[FlakeHub]

## Installing Determinate

You can install Determinate on [macOS](#macos), non-NixOS [Linux](#linux), and [NixOS](#nixos).

### macOS

On macOS, we recommend using the graphical installer from Determinate Systems.
Click [here][gui] to download and run it.

### Linux

On Linux, including Windows Subsystem for Linux (WSL), we recommend installing Determinate using [Determinate Nix Installer][installer]:

```shell
curl -fsSL https://install.determinate.systems/nix | sh -s -- install --determinate
```


---

## Installation and first steps

Visit [nix.dev](https://nix.dev) for [installation instructions](https://nix.dev/tutorials/install-nix) and [beginner tutorials](https://nix.dev/tutorials/first-steps).

Full reference documentation can be found in the [Nix manual](https://nix.dev/reference/nix-manual).

## Building and developing

Follow instructions in the Nix reference manual to [set up a development environment and build Nix from source](https://nix.dev/manual/nix/development/development/building.html).

## Contributing

Check the [contributing guide][contributing] if you want to get involved with developing Nix.

## Additional resources

Nix was created by [Eelco Dolstra][eelco] and developed as the subject of his 2006 PhD thesis, [The Purely Functional Software Deployment Model](https://edolstra.github.io/pubs/phd-thesis.pdf).
Today, a world-wide developer community contributes to Nix and the ecosystem that has grown around it.

- [The Nix, Nixpkgs, NixOS Community on nixos.org][website]
- [Nixpkgs], a collection of well over 100,000 software packages that can be built and managed using Nix
- [Official documentation on nix.dev][nix.dev]
- [NixOS] is a Linux distribution that can be configured fully declaratively

## License

[Upstream Nix][upstream] is released under the [LGPL v2.1][license] license.
[Determinate Nix][det-nix] is also released under LGPL v2.1 based on the terms of that license.

[contributing]: ./CONTRIBUTING.md
[det-nix]: https://docs.determinate.systems/determinate-nix
[determinate]: https://docs.determinate.systems
[detsys]: https://determinate.systems
[dnixd]: https://docs.determinate.systems/determinate-nix#determinate-nixd
[eelco]: https://determinate.systems/people/eelco-dolstra
[flakehub]: https://flakehub.com
[gui]: https://install.determinate.systems/determinate-pkg/stable/Universal
[license]: ./COPYING
[nix.dev]: https://nix.dev
[nixpkgs]: https://github.com/NixOS/nixpkgs
[thesis]: https://edolstra.github.io/pubs/phd-thesis.pdf
[upstream]: https://github.com/NixOS/nix
[website]: https://nixos.org
