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

or if you have a flake-enabled nix:

```console
$ nix develop
```

To get a shell with a different compilation environment (e.g. stdenv,
gccStdenv, clangStdenv, clang11Stdenv):

```console
$ nix-shell -A devShells.x86_64-linux.clang11StdenvPackages
```

or if you have a flake-enabled nix:

```console
$ nix develop .#clang11StdenvPackages
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

If you have a flakes-enabled Nix you can replace:

```console
$ nix-shell
```

by:

```console
$ nix develop
```

## Testing

Nix comes with three different flavors of tests: unit, functional and integration.

### Unit-tests

The unit-tests for each Nix library (`libexpr`, `libstore`, etc..) are defined
under `src/{library_name}/tests` using the
[googletest](https://google.github.io/googletest/) framework.

You can run the whole testsuite with `make check`, or the tests for a specific component with `make libfoo-tests_RUN`. Finer-grained filtering is also possible using the [--gtest_filter](https://google.github.io/googletest/advanced.html#running-a-subset-of-the-tests) command-line option.

### Functional tests

The functional tests reside under the `tests` directory and are listed in `tests/local.mk`.
The whole testsuite can be run with `make install && make installcheck`.
Individual tests can be run with `make tests/{testName}.sh.test`.

### Integration tests

The integration tests are defined in the Nix flake under the `hydraJobs.tests` attribute.
These tests include everything that needs to interact with external services or run Nix in a non-trivial distributed setup.
Because these tests are expensive and require more than what the standard github-actions setup provides, they only run on the master branch (on <https://hydra.nixos.org/jobset/nix/master>).

You can run them manually with `nix build .#hydraJobs.tests.{testName}` or `nix-build -A hydraJobs.tests.{testName}`
