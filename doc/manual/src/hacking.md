# Hacking

This section provides some notes on how to hack on Nix. To get the
latest version of Nix from GitHub:

```console
$ git clone https://github.com/NixOS/nix.git
$ cd nix
```

To build Nix for the current operating system/architecture use

```console
$ nix-build
```

or if you have a flake-enabled nix:

```console
$ nix build
```

This will build `defaultPackage` attribute defined in the `flake.nix`
file. To build for other platforms add one of the following suffixes to
it: aarch64-linux, i686-linux, x86\_64-darwin, x86\_64-linux. i.e.

```console
$ nix-build -A defaultPackage.x86_64-linux
```

To build all dependencies and start a shell in which all environment
variables are set up so that those dependencies can be found:

```console
$ nix-shell
```

To build Nix itself in this shell:

```console
[nix-shell]$ ./bootstrap.sh
[nix-shell]$ ./configure $configureFlags --prefix=$(pwd)/outputs/out
[nix-shell]$ make -j $NIX_BUILD_CORES
```

To install it in `$(pwd)/outputs` and test it:

```console
[nix-shell]$ make install
[nix-shell]$ make installcheck -j $NIX_BUILD_CORES
[nix-shell]$ ./outputs/out/bin/nix --version
nix (Nix) 3.0
```

To run a functional test:

```console
make tests/test-name-should-auto-complete.sh.test
```

To run the unit-tests for C++ code:

```
make check
```

If you have a flakes-enabled Nix you can replace:

```console
$ nix-shell
```

by:

```console
$ nix develop
```
