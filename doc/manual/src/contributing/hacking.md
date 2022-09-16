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

## Testing Nix

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

## Testing the install scripts

Testing the install scripts has traditionally been tedious, but you can now do this much more easily via the GitHub Actions CI runs (at least for platforms that Github Actions supports).

If you've already pushed to a fork of Nix on GitHub before, you may have noticed that the CI workflows in your fork list skipped `installer` and `installer_test` jobs. Once your Nix fork is set up correctly, pushing to it will also run these jobs.
- The `installer` job will generate installers for these platforms:
  - `x86_64-linux`
  - `armv6l-linux`
  - `armv7l-linux`
  - `x86_64-darwin`.
  
  While this installer is in your Cachix cache, you can use it for manual testing on any of these platforms.
- the `installer_test` job will try to use this installer and run a trivial Nix command on `ubuntu-latest` and `macos-latest`.

### One-time setup

1. Have a GitHub account with a fork of the Nix repo.
2. At cachix.org:
    - Create or log in to an account.
    - Create a Cachix cache using the format `<github-username>-nix-install-tests`.
    - Navigate to the new cache > Settings > Auth Tokens.
    - Generate a new cachix auth token and copy the generated value.
4. At github.com:
    - Navigate to your Nix fork > Settings > Secrets > Actions > New repository secret.
    - Name the secret `CACHIX_AUTH_TOKEN`
    - Paste the copied value of the Cachix cache auth token.

### Using the CI-generated installer for manual testing

After the CI run completes, you can check the output to extract the installer URL:
1. Click into the detailed view of the CI run.
2. Click into any `installer_test` run (the URL you're here to extract will be the same in all of them).
3. Click into the `Run cachix/install-nix-action@v...` step and click the detail triangle next to the first log line (it will also be `Run cachix/install-nix-action@v...`)
4. Copy the value of `install_url`
5. To generate an install command, plug this `install_url` and your GitHub username into this template:

    ```console
    sh <(curl -L <install_url>) --tarball-url-prefix https://<github-username>-nix-install-tests.cachix.org/serve
    ```

<!-- ### Manually generating test installers

There's obviously a manual way to do this, and it's still the only way for
platforms that lack GA runners.

I did do this back in Fall 2020 (before the GA approach encouraged here). I'll
sketch what I recall in case it encourages someone to fill in detail, but: I
didn't know what I was doing at the time and had to fumble/ask around a lot--
so I don't want to uphold any of it as "right". It may have been dumb or
the _hard_ way from the getgo. Fundamentals may have changed since.

Here's the build command I used to do this on and for x86_64-darwin:
nix build --out-link /tmp/foo ".#checks.x86_64-darwin.binaryTarball"

I used the stable out-link to make it easier to script the next steps:
link=$(readlink /tmp/foo)
cp $link/*-darwin.tar.xz ~/somewheres

I've lost the last steps and am just going from memory:

From here, I think I had to extract and modify the `install` script to point
it at this tarball (which I scped to my own site, but it might make more sense
to just share them locally). I extracted this script once and then just
search/replaced in it for each new build.

The installer now supports a `--tarball-url-prefix` flag which _may_ have
solved this need?
-->
