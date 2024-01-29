# Nix

[![Open Collective supporters](https://opencollective.com/nixos/tiers/supporter/badge.svg?label=Supporters&color=brightgreen)](https://opencollective.com/nixos)
[![Test](https://github.com/NixOS/nix/workflows/Test/badge.svg)](https://github.com/NixOS/nix/actions)

Nix makes computation repeatable.

It treats file system data like memory in a data flow program:
- Ensures referential integrity
- Enables caching and deduplication
- Performs garbage collection

This allows getting computer programs from one machine to another — and having them still work when they get there.

![](./figures/memory-filesystem.svg)
management reliable and reproducible.

It forces operating system processes to act like pure functions:
- Output only changes if input changes
- Composable as independent, reusable units
- Deterministic and safe to parallelise

Think of it as running `execve()` in a clean environment on read-only data and caching the output.
Or think of it as functional programming with files and directories.

![](./figures/function-process.svg)

## Installation and first steps

Visit [nix.dev](https://nix.dev) for [installation instructions](https://nix.dev/tutorials/install-nix) and [beginner tutorials](https://nix.dev/tutorials/first-steps).

Full reference documentation can be found in the [Nix manual](https://nixos.org/nix/manual).

## Building And Developing

See our [Hacking guide](https://nixos.org/manual/nix/unstable/contributing/hacking.html) in our manual for instruction on how to
 set up a development environment and build Nix from source.

## Contributing

Check the [contributing guide](./CONTRIBUTING.md) if you want to get involved with developing Nix.

## Additional Resources

- [Nix manual](https://nixos.org/nix/manual)
- [Nix jobsets on hydra.nixos.org](https://hydra.nixos.org/project/nix)
- [NixOS Discourse](https://discourse.nixos.org/)
- [Matrix - #nix:nixos.org](https://matrix.to/#/#nix:nixos.org)

## License

Nix is released under the [LGPL v2.1](./COPYING).
