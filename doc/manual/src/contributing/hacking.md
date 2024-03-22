# Hacking

This section provides some notes on how to hack on Nix. To get the
latest version of Nix from GitHub:

```console
$ git clone https://github.com/NixOS/nix.git
$ cd nix
```

The following instructions assume you already have some version of Nix installed locally, so that you can use it to set up the development environment. If you don't have it installed, follow the [installation instructions].

[installation instructions]: ../installation/index.md

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
$ nix develop .#native-clangStdenvPackages
```

> **Note**
>
> Use `ccacheStdenv` to drastically improve rebuild time.
> By default, [ccache](https://ccache.dev) keeps artifacts in `~/.cache/ccache/`.

To build Nix itself in this shell:

```console
[nix-shell]$ autoreconfPhase
[nix-shell]$ configurePhase
[nix-shell]$ make -j $NIX_BUILD_CORES OPTIMIZE=0
```

To install it in `$(pwd)/outputs` and test it:

```console
[nix-shell]$ make install OPTIMIZE=0
[nix-shell]$ make installcheck check -j $NIX_BUILD_CORES
[nix-shell]$ nix --version
nix (Nix) 2.12
```

For more information on running and filtering tests, see
[`testing.md`](./testing.md).

To build a release version of Nix for the current operating system and CPU architecture:

```console
$ nix build
```

You can also build Nix for one of the [supported platforms](#platforms).

## Building Nix

To build all dependencies and start a shell in which all environment variables are set up so that those dependencies can be found:

```console
$ nix-shell
```

To get a shell with one of the other [supported compilation environments](#compilation-environments):

```console
$ nix-shell --attr devShells.x86_64-linux.native-clangStdenvPackages
```

> **Note**
>
> You can use `native-ccacheStdenvPackages` to drastically improve rebuild time.
> By default, [ccache](https://ccache.dev) keeps artifacts in `~/.cache/ccache/`.

To build Nix itself in this shell:

```console
[nix-shell]$ autoreconfPhase
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

You can also build Nix for one of the [supported platforms](#platforms).

## Makefile variables

You may need `profiledir=$out/etc/profile.d` and `sysconfdir=$out/etc` to run `make install`.

Run `make` with [`-e` / `--environment-overrides`](https://www.gnu.org/software/make/manual/make.html#index-_002de) to allow environment variables to override `Makefile` variables:

- `ENABLE_BUILD=yes` to enable building the C++ code.
- `ENABLE_DOC_GEN=yes` to enable building the documentation (manual, man pages, etc.).

  The docs can take a while to build, so you may want to disable this for local development.
- `ENABLE_FUNCTIONAL_TESTS=yes` to enable building the functional tests.
- `ENABLE_UNIT_TESTS=yes` to enable building the unit tests.
- `OPTIMIZE=1` to enable optimizations.
- `libraries=libutil programs=` to only build a specific library.

  This will fail in the linking phase if the other libraries haven't been built, but is useful for checking types.
- `libraries= programs=nix` to only build a specific program.

  This will not work in general, because the programs need the libraries.

## Platforms

Nix can be built for various platforms, as specified in [`flake.nix`]:

[`flake.nix`]: https://github.com/nixos/nix/blob/master/flake.nix

- `x86_64-linux`
- `x86_64-darwin`
- `i686-linux`
- `aarch64-linux`
- `aarch64-darwin`
- `armv6l-linux`
- `armv7l-linux`

In order to build Nix for a different platform than the one you're currently
on, you need a way for your current Nix installation to build code for that
platform. Common solutions include [remote build machines] and [binary format emulation]
(only supported on NixOS).

[remote builders]: @docroot@/language/derivations.md#attr-builder
[binary format emulation]: https://nixos.org/manual/nixos/stable/options.html#opt-boot.binfmt.emulatedSystems

Given such a setup, executing the build only requires selecting the respective attribute.
For example, to compile for `aarch64-linux`:

```console
$ nix-build --attr packages.aarch64-linux.default
```

or for Nix with the [`flakes`] and [`nix-command`] experimental features enabled:

```console
$ nix build .#packages.aarch64-linux.default
```

Cross-compiled builds are available for ARMv6 (`armv6l-linux`) and ARMv7 (`armv7l-linux`).
Add more [system types](#system-type) to `crossSystems` in `flake.nix` to bootstrap Nix on unsupported platforms.

### Building for multiple platforms at once

It is useful to perform multiple cross and native builds on the same source tree,
for example to ensure that better support for one platform doesn't break the build for another.
In order to facilitate this, Nix has some support for being built out of tree – that is, placing build artefacts in a different directory than the source code:

1. Create a directory for the build, e.g.

   ```bash
   mkdir build
   ```

2. Run the configure script from that directory, e.g.

   ```bash
   cd build
   ../configure <configure flags>
   ```

3. Run make from the source directory, but with the build directory specified, e.g.

   ```bash
   make builddir=build <make flags>
   ```

## System type

Nix uses a string with he following format to identify the *system type* or *platform* it runs on:

```
<cpu>-<os>[-<abi>]
```

It is set when Nix is compiled for the given system, and based on the output of [`config.guess`](https://github.com/nixos/nix/blob/master/config/config.guess) ([upstream](https://git.savannah.gnu.org/cgit/config.git/tree/config.guess)):

```
<cpu>-<vendor>-<os>[<version>][-<abi>]
```

When Nix is built such that `./configure` is passed any of the `--host`, `--build`, `--target` options, the value is based on the output of [`config.sub`](https://github.com/nixos/nix/blob/master/config/config.sub) ([upstream](https://git.savannah.gnu.org/cgit/config.git/tree/config.sub)):

```
<cpu>-<vendor>[-<kernel>]-<os>
```

For historic reasons and backward-compatibility, some CPU and OS identifiers are translated from the GNU Autotools naming convention in [`configure.ac`](https://github.com/nixos/nix/blob/master/configure.ac) as follows:

| `config.guess`             | Nix                 |
|----------------------------|---------------------|
| `amd64`                    | `x86_64`            |
| `i*86`                     | `i686`              |
| `arm6`                     | `arm6l`             |
| `arm7`                     | `arm7l`             |
| `linux-gnu*`               | `linux`             |
| `linux-musl*`              | `linux`             |

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
make compile_commands.json
```

Configure your editor to use the `clangd` from the `.#native-clangStdenvPackages` shell. You can do that either by running it inside the development shell, or by using [nix-direnv](https://github.com/nix-community/nix-direnv) and [the appropriate editor plugin](https://github.com/direnv/direnv/wiki#editor-integration).

> **Note**
>
> For some editors (e.g. Visual Studio Code), you may need to install a [special extension](https://open-vsx.org/extension/llvm-vs-code-extensions/vscode-clangd) for the editor to interact with `clangd`.
> Some other editors (e.g. Emacs, Vim) need a plugin to support LSP servers in general (e.g. [lsp-mode](https://github.com/emacs-lsp/lsp-mode) for Emacs and [vim-lsp](https://github.com/prabirshrestha/vim-lsp) for vim).
> Editor-specific setup is typically opinionated, so we will not cover it here in more detail.

## Add a release note

`doc/manual/rl-next` contains release notes entries for all unreleased changes.

User-visible changes should come with a release note.

### Add an entry

Here's what a complete entry looks like. The file name is not incorporated in the document.

```
---
synopsis: Basically a title
issues: 1234
prs: 1238
---

Here's one or more paragraphs that describe the change.

- It's markdown
- Add references to the manual using @docroot@
```

Significant changes should add the following header, which moves them to the top.

```
significance: significant
```

<!-- Keep an eye on https://codeberg.org/fgaz/changelog-d/issues/1 -->
See also the [format documentation](https://github.com/haskell/cabal/blob/master/CONTRIBUTING.md#changelog).

### Build process

Releases have a precomputed `rl-MAJOR.MINOR.md`, and no `rl-next.md`.

## Branches

- [`master`](https://github.com/NixOS/nix/commits/master)

  The main development branch. All changes are approved and merged here.
  When developing a change, create a branch based on the latest `master`.

  Maintainers try to [keep it in a release-worthy state](#reverting).

- [`maintenance-*.*`](https://github.com/NixOS/nix/branches/all?query=maintenance)

  These branches are the subject of backports only, and are
  also [kept](#reverting) in a release-worthy state.

  See [`maintainers/backporting.md`](https://github.com/NixOS/nix/blob/master/maintainers/backporting.md)

- [`latest-release`](https://github.com/NixOS/nix/tree/latest-release)

  The latest patch release of the latest minor version.

  See [`maintainers/release-process.md`](https://github.com/NixOS/nix/blob/master/maintainers/release-process.md)

- [`backport-*-to-*`](https://github.com/NixOS/nix/branches/all?query=backport)

  Generally branches created by the backport action.

  See [`maintainers/backporting.md`](https://github.com/NixOS/nix/blob/master/maintainers/backporting.md)

- [_other_](https://github.com/NixOS/nix/branches/all)

  Branches that do not conform to the above patterns should be feature branches.

## Reverting

If a change turns out to be merged by mistake, or contain a regression, it may be reverted.
A revert is not a rejection of the contribution, but merely part of an effective development process.
It makes sure that development keeps running smoothly, with minimal uncertainty, and less overhead.
If maintainers have to worry too much about avoiding reverts, they would not be able to merge as much.
By embracing reverts as a good part of the development process, everyone wins.

However, taking a step back may be frustrating, so maintainers will be extra supportive on the next try.
