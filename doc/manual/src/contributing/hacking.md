# Hacking Nix itself

This section provides some notes on how to hack on Nix.


### Get the latest version of Nix from GitHub

```console
$ git clone https://github.com/NixOS/nix.git
$ cd nix
```


### Build the Nix package

**For the current operating system/architecture**:

```console
$ nix-build

$ # or for a flake-enabled nix:
$ nix build
```
This will build the `defaultPackage.${builtins.currentSystem}` attribute
defined in the `flake.nix` file.

**For another operating system/architecture**:

To build for other platforms use one of the following platform
identifiers: `aarch64-linux`, `i686-linux`, `x86_64-darwin`, `x86_64-linux`.

For example:

```console
$ nix-build -A defaultPackage.x86_64-linux

$ # or for a flake-enabled nix:
$ nix build .#defaultPackage.x86_64-linux
```


### Open a development shell to hack on Nix

To download (or build if they are not available) all dependencies and
start a shell in which all environment variables are set up so that those
dependencies can be found:

```console
$ nix-shell
[dev-shell]$ # You are now in a development shell

$ # or for a flake-enabled nix:
$ nix develop
[dev-shell]$ # You are now in a development shell
```

**To get a shell with a different compilation environment**:

(e.g. stdenv, gccStdenv, clangStdenv, clang11Stdenv):
```console
$ nix-shell -A devShells.x86_64-linux.clang11StdenvPackages

$ # or for a flake-enabled nix:
$ nix develop .#clang11StdenvPackages
```

**You can use your prefered shell with**:

```console
$ nix-shell --run "$SHELL"

$ # or for a flake-enabled nix:
$ nix develop -c $SHELL
```

### Build Nix

To build Nix itself in the development shell:

```console
[dev-shell]$ ./bootstrap.sh
[dev-shell]$ ./configure $configureFlags --prefix=$(pwd)/outputs/out
[dev-shell]$ make -j $NIX_BUILD_CORES
... (build log)
[dev-shell]$ ./outputs/out/bin/nix --version
nix (Nix) 3.0
```


### Install (in a local dir)

To install everything (bins, libs, docs, configs) to `$(pwd)/outputs/out` (chosen at the `./configure ...` step above):

```console
[dev-shell]$ make install
```
NOTE: This will also (re-)build the HTML docs if needed.


### Run tests

Run the unit-tests for C++ code:

```console
[dev-shell]$ make check
```

Run all functional tests:

```console
[dev-shell]$ make install    # need to install first if you haven't
[dev-shell]$ make installcheck -j $NIX_BUILD_CORES
```

Run a functional test (e.g: `tests/flakes.sh`):

```console
[dev-shell]$ make tests/flakes.sh.test    # note the .test suffix!
```


---

Have fun! 🚀 🚀 🚀

And make the world a better place! (with Nix of course 😀)
