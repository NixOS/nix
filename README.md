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
Nix is a powerful [language], [package manager][package-management], and [CLI] for [macOS](#macos), [Linux](linux), and other Unix systems that enables you to create fully reproducible [development environments][envs], to build [packages] in sandboxed environments, to build entire Linux systems using [NixOS], and much more.

Determinate Nix is part of the [Determinate platform][determinate], which also includes [FlakeHub], a secure flake repository with features like [FlakeHub Cache][cache], [private flakes][private-flakes], and [semantic versioning][semver] (SemVer) for [flakes].

## Installing Determinate

You can install Determinate on [macOS](#macos), non-NixOS [Linux](#linux) and WSL, and [NixOS](#nixos).

### macOS

On macOS, we recommend using the graphical installer from Determinate Systems.
Click [here][gui] to download and run it.

### Linux

On Linux, including Windows Subsystem for Linux (WSL), we recommend installing Determinate Nix using [Determinate Nix Installer][installer]:

```shell
curl -fsSL https://install.determinate.systems/nix | sh -s -- install --determinate
```

### NixOS

On [NixOS], we recommend following our [dedicated installation guide][nixos-install].

## Other resources

Nix was created by [Eelco Dolstra][eelco] and developed as the subject of his 2006 PhD thesis, [The Purely Functional Software Deployment Model][thesis].
Today, a worldwide developer community contributes to Nix and the ecosystem that has grown around it.

- [Zero to Nix][z2n], Determinate Systems' guide to Nix and [flakes] for beginners
- [Nixpkgs], a collection of well over 100,000 software packages that you can build and manage using Nix
- [NixOS] is a Linux distribution that can be configured fully declaratively
- The Nix, Nixpkgs, and NixOS community on [nixos.org][website]

## Reference

The primary documentation for Determinate and Determinate Nix is available at [docs.determinate.systems][determinate].
For deeply technical reference material, see the [Determinate Nix manual][manual] which is based on the upstream Nix manual.

## License

[Upstream Nix][upstream] is released under the [LGPL v2.1][license] license.
[Determinate Nix][det-nix] is also released under LGPL v2.1 in accordance with the terms of the upstream license.

## Contributing

Check the [contributing guide][contributing] if you want to get involved with developing Nix.

[cache]: https://docs.determinate.systems/flakehub/cache
[cli]: https://manual.determinate.systems/command-ref/new-cli/nix.html
[contributing]: ./CONTRIBUTING.md
[det-nix]: https://docs.determinate.systems/determinate-nix
[determinate]: https://docs.determinate.systems
[detsys]: https://determinate.systems
[dnixd]: https://docs.determinate.systems/determinate-nix#determinate-nixd
[eelco]: https://determinate.systems/people/eelco-dolstra
[envs]: https://zero-to-nix.com/concepts/dev-env
[flakehub]: https://flakehub.com
[flakes]: https://zero-to-nix.com/concepts/flakes
[gui]: https://install.determinate.systems/determinate-pkg/stable/Universal
[installer]: https://github.com/DeterminateSystems/nix-installer
[language]: https://zero-to-nix.com/concepts/nix-language
[license]: ./COPYING
[manual]: https://manual.determinate.systems
[nixpkgs]: https://github.com/NixOS/nixpkgs
[nixos]: https://github.com/NixOS/nixpkgs/tree/master/nixos
[nixos-install]: https://docs.determinate.systems/guides/advanced-installation#nixos
[packages]: https://zero-to-nix.com/concepts/packages
[package-management]: https://zero-to-nix.com/concepts/package-management
[private-flakes]: https://docs.determinate.systems/flakehub/private-flakes
[semver]: https://docs.determinate.systems/flakehub/concepts/semver
[thesis]: https://edolstra.github.io/pubs/phd-thesis.pdf
[upstream]: https://github.com/NixOS/nix
[website]: https://nixos.org
[z2n]: https://zero-to-nix.com
