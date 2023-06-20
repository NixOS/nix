# Hacking

This section provides some notes on how to hack on Nix. To get the
latest version of Nix from GitHub:

```console
$ git clone https://github.com/NixOS/nix.git
$ cd nix
```

The following instructions assume you already have some version of Nix installed locally, so that you can use it to set up the development environment. If you don't have it installed, follow the [installation instructions].

[installation instructions]: ../installation/installation.md

## Building Nix with flakes

This section assumes you are using Nix with the [`flakes`] and [`nix-command`] experimental features enabled.
See the [Building Nix](#building-nix) section for equivalent instructions using stable Nix interfaces.

[`flakes`]: @docroot@/contributing/experimental-features.md#xp-feature-flakes
[`nix-command`]: @docroot@/contributing/experimental-features.md#xp-nix-command

To build all dependencies and start a shell in which all environment variables are set up so that those dependencies can be found:

```console
$ nix develop
```

This shell also adds `./outputs/bin/nix` to your `$PATH` so you can run `nix` immediately after building it.

To get a shell with one of the other [supported compilation environments](#compilation-environments):

```console
$ nix develop .#native-clang11StdenvPackages
```

> **Note**
>
> Use `ccacheStdenv` to drastically improve rebuild time.
> By default, [ccache](https://ccache.dev) keeps artifacts in `~/.cache/ccache/`.

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
[nix-shell]$ nix --version
nix (Nix) 2.12
```

To build a release version of Nix for the current operating system and CPU architecture:

```console
$ nix build
```

You can also build Nix for one of the [supported target platforms](#platforms).

## Building Nix

To build all dependencies and start a shell in which all environment variables are set up so that those dependencies can be found:

```console
$ nix-shell
```

To get a shell with one of the other [supported compilation environments](#compilation-environments):

```console
$ nix-shell --attr devShells.x86_64-linux.native-clang11StdenvPackages
```

> **Note**
>
> You can use `native-ccacheStdenvPackages` to drastically improve rebuild time.
> By default, [ccache](https://ccache.dev) keeps artifacts in `~/.cache/ccache/`.

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
nix (Nix) 2.12
```

To build a release version of Nix for the current operating system and CPU architecture:

```console
$ nix-build
```

You can also build Nix for one of the [supported target platforms](#platforms).

## Platforms

As specified in [`flake.nix`], Nix can be built for various platforms:

- `aarch64-linux`
- `i686-linux`
- `x86_64-darwin`
- `x86_64-linux`

[`flake.nix`]: https://github.com/nixos/nix/blob/master/flake.nix

In order to build Nix for a different platform than the one you're currently
on, you need to have some way for your system Nix to build code for that
platform. Common solutions include [remote builders] and [binfmt emulation]
(only supported on NixOS).

[remote builders]: ../advanced-topics/distributed-builds.md
[binfmt emulation]: https://nixos.org/manual/nixos/stable/options.html#opt-boot.binfmt.emulatedSystems

These solutions let Nix perform builds as if you're on the native platform, so
executing the build is as simple as

```console
$ nix build .#packages.aarch64-linux.default
```

for flake-enabled Nix, or

```console
$ nix-build --attr packages.aarch64-linux.default
```

for classic Nix.

You can use any of the other supported platforms in place of `aarch64-linux`.

Cross-compiled builds are available for ARMv6 and ARMv7, and Nix on unsupported platforms can be bootstrapped by adding more `crossSystems` in `flake.nix`.

## Compilation environments

Nix can be compiled using multiple environments:

- `stdenv`: default;
- `gccStdenv`: force the use of `gcc` compiler;
- `clangStdenv`: force the use of `clang` compiler;
- `ccacheStdenv`: enable [ccache], a compiler cache to speed up compilation.

To build with one of those environments, you can use

```console
$ nix build .#nix-ccacheStdenv
```

for flake-enabled Nix, or

```console
$ nix-build --attr nix-ccacheStdenv
```

for classic Nix.

You can use any of the other supported environments in place of `nix-ccacheStdenv`.

## Editor integration

The `clangd` LSP server is installed by default on the `clang`-based `devShell`s.
See [supported compilation environments](#compilation-environments) and instructions how to set up a shell [with flakes](#nix-with-flakes) or in [classic Nix](#classic-nix).

To use the LSP with your editor, you first need to [set up `clangd`](https://clangd.llvm.org/installation#project-setup) by running:

```console
make clean && bear -- make -j$NIX_BUILD_CORES install
```

Configure your editor to use the `clangd` from the shell, either by running it inside the development shell, or by using [nix-direnv](https://github.com/nix-community/nix-direnv) and [the appropriate editor plugin](https://github.com/direnv/direnv/wiki#editor-integration).

> **Note**
>
> For some editors (e.g. Visual Studio Code), you may need to install a [special extension](https://open-vsx.org/extension/llvm-vs-code-extensions/vscode-clangd) for the editor to interact with `clangd`.
> Some other editors (e.g. Emacs, Vim) need a plugin to support LSP servers in general (e.g. [lsp-mode](https://github.com/emacs-lsp/lsp-mode) for Emacs and [vim-lsp](https://github.com/prabirshrestha/vim-lsp) for vim).
> Editor-specific setup is typically opinionated, so we will not cover it here in more detail.

## Running tests

### Unit-tests

The unit-tests for each Nix library (`libexpr`, `libstore`, etc..) are defined
under `src/{library_name}/tests` using the
[googletest](https://google.github.io/googletest/) and
[rapidcheck](https://github.com/emil-e/rapidcheck) frameworks.

You can run the whole testsuite with `make check`, or the tests for a specific component with `make libfoo-tests_RUN`. Finer-grained filtering is also possible using the [--gtest_filter](https://google.github.io/googletest/advanced.html#running-a-subset-of-the-tests) command-line option.

### Functional tests

The functional tests reside under the `tests` directory and are listed in `tests/local.mk`.
Each test is a bash script.

The whole test suite can be run with:

```shell-session
$ make install && make installcheck
ran test tests/foo.sh... [PASS]
ran test tests/bar.sh... [PASS]
...
```

Individual tests can be run with `make`:

```shell-session
$ make tests/${testName}.sh.test
ran test tests/${testName}.sh... [PASS]
```

or without `make`:

```shell-session
$ ./mk/run-test.sh tests/${testName}.sh
ran test tests/${testName}.sh... [PASS]
```

To see the complete output, one can also run:

```shell-session
$ ./mk/debug-test.sh tests/${testName}.sh
+ foo
output from foo
+ bar
output from bar
...
```

The test script will then be traced with `set -x` and the output displayed as it happens, regardless of whether the test succeeds or fails.

#### Debugging failing functional tests

When a functional test fails, it usually does so somewhere in the middle of the script.

To figure out what's wrong, it is convenient to run the test regularly up to the failing `nix` command, and then run that command with a debugger like GDB.

For example, if the script looks like:

```bash
foo
nix blah blub
bar
```
edit it like so:

```diff
 foo
-nix blah blub
+gdb --args nix blah blub
 bar
```

Then, running the test with `./mk/debug-test.sh` will drop you into GDB once the script reaches that point:

```shell-session
$ ./mk/debug-test.sh tests/${testName}.sh
...
+ gdb blash blub
GNU gdb (GDB) 12.1
...
(gdb)
```

One can debug the Nix invocation in all the usual ways.
For example, enter `run` to start the Nix invocation.

### Integration tests

The integration tests are defined in the Nix flake under the `hydraJobs.tests` attribute.
These tests include everything that needs to interact with external services or run Nix in a non-trivial distributed setup.
Because these tests are expensive and require more than what the standard github-actions setup provides, they only run on the master branch (on <https://hydra.nixos.org/jobset/nix/master>).

You can run them manually with `nix build .#hydraJobs.tests.{testName}` or `nix-build -A hydraJobs.tests.{testName}`

### Installer tests

After a one-time setup, the Nix repository's GitHub Actions continuous integration (CI) workflow can test the installer each time you push to a branch.

Creating a Cachix cache for your installer tests and adding its authorization token to GitHub enables [two installer-specific jobs in the CI workflow](https://github.com/NixOS/nix/blob/88a45d6149c0e304f6eb2efcc2d7a4d0d569f8af/.github/workflows/ci.yml#L50-L91):

- The `installer` job generates installers for the platforms below and uploads them to your Cachix cache:
  - `x86_64-linux`
  - `armv6l-linux`
  - `armv7l-linux`
  - `x86_64-darwin`

- The `installer_test` job (which runs on `ubuntu-latest` and `macos-latest`) will try to install Nix with the cached installer and run a trivial Nix command.

#### One-time setup

1. Have a GitHub account with a fork of the [Nix repository](https://github.com/NixOS/nix).
2. At cachix.org:
    - Create or log in to an account.
    - Create a Cachix cache using the format `<github-username>-nix-install-tests`.
    - Navigate to the new cache > Settings > Auth Tokens.
    - Generate a new Cachix auth token and copy the generated value.
3. At github.com:
    - Navigate to your Nix fork > Settings > Secrets > Actions > New repository secret.
    - Name the secret `CACHIX_AUTH_TOKEN`.
    - Paste the copied value of the Cachix cache auth token.

#### Using the CI-generated installer for manual testing

After the CI run completes, you can check the output to extract the installer URL:
1. Click into the detailed view of the CI run.
2. Click into any `installer_test` run (the URL you're here to extract will be the same in all of them).
3. Click into the `Run cachix/install-nix-action@v...` step and click the detail triangle next to the first log line (it will also be `Run cachix/install-nix-action@v...`)
4. Copy the value of `install_url`
5. To generate an install command, plug this `install_url` and your GitHub username into this template:

    ```console
    curl -L <install_url> | sh -s -- --tarball-url-prefix https://<github-username>-nix-install-tests.cachix.org/serve
    ```

<!-- #### Manually generating test installers

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

### Checking links in the manual

The build checks for broken internal links.
This happens late in the process, so `nix build` is not suitable for iterating.
To build the manual incrementally, run:

```console
make html -j $NIX_BUILD_CORES
```

In order to reflect changes to the [Makefile], clear all generated files before re-building:

[Makefile]: https://github.com/NixOS/nix/blob/master/doc/manual/local.mk

```console
rm $(git ls-files doc/manual/ -o | grep -F '.md') && rmdir doc/manual/src/command-ref/new-cli && make html -j $NIX_BUILD_CORES
```

[`mdbook-linkcheck`] does not implement checking [URI fragments] yet.

[`mdbook-linkcheck`]: https://github.com/Michael-F-Bryan/mdbook-linkcheck
[URI fragments]: https://en.wikipedia.org/wiki/URI_fragment

#### `@docroot@` variable

`@docroot@` provides a base path for links that occur in reusable snippets or other documentation that doesn't have a base path of its own.

If a broken link occurs in a snippet that was inserted into multiple generated files in different directories, use `@docroot@` to reference the `doc/manual/src` directory.

If the `@docroot@` literal appears in an error message from the `mdbook-linkcheck` tool, the `@docroot@` replacement needs to be applied to the generated source file that mentions it.
See existing `@docroot@` logic in the [Makefile].
Regular markdown files used for the manual have a base path of their own and they can use relative paths instead of `@docroot@`.

## API documentation

Doxygen API documentation is [available
online](https://hydra.nixos.org/job/nix/master/internal-api-docs/latest/download-by-type/doc/internal-api-docs). You
can also build and view it yourself:

```console
# nix build .#hydraJobs.internal-api-docs
# xdg-open ./result/share/doc/nix/internal-api/html/index.html
```

or inside a `nix develop` shell by running:

```
# make internal-api-html
# xdg-open ./outputs/doc/share/doc/nix/internal-api/html/index.html
```

## Coverage analysis

A coverage analysis report is [available
online](https://hydra.nixos.org/job/nix/master/coverage/latest/download-by-type/report/coverage). You
can build it yourself:

```
# nix build .#hydraJobs.coverage
# xdg-open ./result/coverage/index.html
```

Metrics about the change in line/function coverage over time are also
[available](https://hydra.nixos.org/job/nix/master/coverage#tabs-charts).
