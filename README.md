# Nix

[![Open Collective supporters](https://opencollective.com/nixos/tiers/supporter/badge.svg?label=Supporters&color=brightgreen)](https://opencollective.com/nixos)
[![Test](https://github.com/NixOS/nix/workflows/Test/badge.svg)](https://github.com/NixOS/nix/actions)

Nix is a powerful package manager for Linux and other Unix systems that makes package
management reliable and reproducible. Please refer to the [Nix manual](https://nixos.org/nix/manual)
for more details.

## Installation

On Linux and macOS the easiest way to Install Nix is to run the following shell command
(as a user other than root):

```
$ curl -L https://nixos.org/nix/install | sh
```

Information on additional installation methods is available on the [Nix download page](https://nixos.org/download.html).

## Building And Developing

### Building Nix

You can build Nix using one of the targets provided by [release.nix](./release.nix):

```
$ nix-build ./release.nix -A build.aarch64-linux
$ nix-build ./release.nix -A build.x86_64-darwin
$ nix-build ./release.nix -A build.i686-linux
$ nix-build ./release.nix -A build.x86_64-linux
```

### Development Environment

You can use the provided `shell.nix` to get a working development environment:

```
$ nix-shell
$ ./bootstrap.sh
$ ./configure
$ make
```

## Additional Resources

- [Nix manual](https://nixos.org/nix/manual)
- [Nix jobsets on hydra.nixos.org](https://hydra.nixos.org/project/nix)
- [NixOS Discourse](https://discourse.nixos.org/)
- [IRC - #nixos on freenode.net](irc://irc.freenode.net/#nixos)

## License

Nix is released under the [LGPL v2.1](./COPYING).
