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

You can also build Nix for one of the [supported platforms](#platforms).

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

You can also build Nix for one of the [supported platforms](#platforms).

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
platform. Common solutions include [remote builders] and [binary format emulation]
(only supported on NixOS).

[remote builders]: ../advanced-topics/distributed-builds.md
[binfmt emulation]: https://nixos.org/manual/nixos/stable/options.html#opt-boot.binfmt.emulatedSystems

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

For historic reasons and backward-compatibility, some CPU and OS identifiers are translated from the GNU Autotools naming convention in [`configure.ac`](https://github.com/nixos/nix/blob/master/config/config.sub) as follows:

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
make clean && bear -- make -j$NIX_BUILD_CORES install
```

Configure your editor to use the `clangd` from the shell, either by running it inside the development shell, or by using [nix-direnv](https://github.com/nix-community/nix-direnv) and [the appropriate editor plugin](https://github.com/direnv/direnv/wiki#editor-integration).

> **Note**
>
> For some editors (e.g. Visual Studio Code), you may need to install a [special extension](https://open-vsx.org/extension/llvm-vs-code-extensions/vscode-clangd) for the editor to interact with `clangd`.
> Some other editors (e.g. Emacs, Vim) need a plugin to support LSP servers in general (e.g. [lsp-mode](https://github.com/emacs-lsp/lsp-mode) for Emacs and [vim-lsp](https://github.com/prabirshrestha/vim-lsp) for vim).
> Editor-specific setup is typically opinionated, so we will not cover it here in more detail.

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
