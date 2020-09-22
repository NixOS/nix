# Nix

[![Open Collective supporters](https://opencollective.com/nixos/tiers/supporter/badge.svg?label=Supporters&color=brightgreen)](https://opencollective.com/nixos)
[![Test](https://github.com/NixOS/nix/workflows/Test/badge.svg)](https://github.com/NixOS/nix/actions)

Nix is a powerful package manager for Linux and other Unix systems that makes package
management reliable and reproducible. Please refer to the [Nix manual](https://nixos.org/nix/manual)
for more details.

## Installation

On Linux and macOS the easiest way to Install Nix is to run the following shell command
(as a user other than root):

```console
$ curl -L https://nixos.org/nix/install | sh
```

Information on additional installation methods is available on the [Nix download page](https://nixos.org/download.html).

## Building And Developing

See our [Hacking guide](https://hydra.nixos.org/job/nix/master/build.x86_64-linux/latest/download-by-type/doc/manual/hacking.html) in our manual for instruction on how to
build nix from source with nix-build or how to get a development environment.

## Additional Resources

- [Nix manual](https://nixos.org/nix/manual)
- [Nix jobsets on hydra.nixos.org](https://hydra.nixos.org/project/nix)
- [NixOS Discourse](https://discourse.nixos.org/)
- [IRC - #nixos on freenode.net](irc://irc.freenode.net/#nixos)

## License

Nix is released under the [LGPL v2.1](./COPYING).
