# Nix

[![Open Collective supporters](https://opencollective.com/nixos/tiers/supporter/badge.svg?label=Supporters&color=brightgreen)](https://opencollective.com/nixos)
[![CI](https://github.com/NixOS/nix/workflows/CI/badge.svg)](https://github.com/NixOS/nix/actions/workflows/ci.yml)

Nix is a powerful package manager for Linux and other Unix systems that makes package
management reliable and reproducible. It provides atomic upgrades and rollbacks, side-by-side installation of multiple versions of a package, multi-user package management and easy setup of build environments. Please refer to the [Nix manual](https://nix.dev/reference/nix-manual)
for more details.

## Installation and first steps

Visit [nix.dev](https://nix.dev) for [installation instructions](https://nix.dev/tutorials/install-nix) and [beginner tutorials](https://nix.dev/tutorials/first-steps).

Full reference documentation can be found in the [Nix manual](https://nix.dev/reference/nix-manual).

## Building and developing

Follow instructions in the Nix reference manual to [set up a development environment and build Nix from source](https://nix.dev/manual/nix/development/development/building.html).

## Contributing

Check the [contributing guide](./CONTRIBUTING.md) if you want to get involved with developing Nix.

## Key Features of Nix

So you may be wondering, why nix and why should you contribute to nix? What sets us apart from other package managers is the list of key features we have to offer. Nix offers several unique features that makes it more reliable and efficient in comparison to other traditional package managers:

1. Reproducibility: Nix ensures reproducible builds by using cryptographic hashes to identify dependencies and build outputs. This means that a package built on one machnes will be identical on another as long as the inputs (i.e dependencies, build scripts, etc.) are the same.

2. Atomic Upgrades an Rollbacks: With Nix, when a package is updated, the old version remains availabe, and the upgrade happens atomically. This means that the package won't be left in an inconsistent state during the process, allowing for safer rollbacks.

3. Multi-user Management: Nix supports multi-user environments, where each user can have their own configurations without impacting others. For example, if two users install the same packagem Nix ensures it is only stored once in the Nix store, making it the perfect option for team projects.

4. Garbage Collection: It is very common for developers to download packages and end up no longer needing them, but they also tend to be forgotten about. Nix allows for garbage collectionof unused versions which helps free up disk space. It removes paths that are no longer needed and not reference by any user environment.

If these features weren't enough to convince you that Nix is the package manager for you, you can take a look at our website to learn more about other features that Nix has to offer!

## Best Practices for Nix Development

1. Always specify the exact version of Nixpkgs you're using to ensure reproducibility.

2. Use nix-shell to create isolated development environments for your projects

3. Implement proper testing and create comprehensive test suites for your packages

4.. Employ nix-review to test your changes before submitting pull requests to Nixpkgs.

## Common Development Pitfalls and how to avoid them 

1. Lack of documentation: Poorly documented Nix expressions can be difficult to understand and maintain. Make sure to add comments to your Nix expressions and maintain and seperate documentation file for complex setups.

2. Inconsistent Formatting: Inconsistent formatting can make Nix expressions hard to read and maintain. To avoid this use tools available such as nixpkgs-fmt to automatically format your nix code.

3. Dependency hel: Avoid conflicting dependencies by leveraging Nix's ability to have multiple versions of a package. To do this, use commands such as override or overrideAttrs to custmize package dependencies when needed. 

By understanding these features, best practices, and common pitfalls, you'll be better equipped to leverage the full power of Nix in your projects!


## Additional resources

Nix was created by Eelco Dolstra and developed as the subject of his PhD thesis [The Purely Functional Software Deployment Model](https://edolstra.github.io/pubs/phd-thesis.pdf), published 2006.
Today, a world-wide developer community contributes to Nix and the ecosystem that has grown around it.

- [The Nix, Nixpkgs, NixOS Community on nixos.org](https://nixos.org/)
- [Official documentation on nix.dev](https://nix.dev)
- [Nixpkgs](https://github.com/NixOS/nixpkgs) is [the largest, most up-to-date free software repository in the world](https://repology.org/repositories/graphs)
- [NixOS](https://github.com/NixOS/nixpkgs/tree/master/nixos) is a Linux distribution that can be configured fully declaratively
- [Discourse](https://discourse.nixos.org/)
- [Matrix](https://matrix.to/#/#nix:nixos.org)

## License

Nix is released under the [LGPL v2.1](./COPYING).
