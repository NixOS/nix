# Building Nix

This section provides some notes on how to start hacking on Nix.
To get the latest version of Nix from GitHub:

```console
$ git clone https://github.com/NixOS/nix.git
$ cd nix
```

> **Note**
>
> The following instructions assume you already have some version of Nix installed locally, so that you can use it to set up the development environment.
> If you don't have it installed, follow the [installation instructions](../installation/index.md).


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
[nix-shell]$ mesonFlags+=" --prefix=$(pwd)/outputs/out"
[nix-shell]$ dontAddPrefix=1 mesonConfigurePhase
[nix-shell]$ ninjaBuildPhase
```

To test it:

```console
[nix-shell]$ mesonCheckPhase
```

To install it in `$(pwd)/outputs`:

```console
[nix-shell]$ ninjaInstallPhase
[nix-shell]$ ./outputs/out/bin/nix --version
nix (Nix) 2.12
```

To build a release version of Nix for the current operating system and CPU architecture:

```console
$ nix-build
```

You can also build Nix for one of the [supported platforms](#platforms).

## Building Nix with flakes

This section assumes you are using Nix with the [`flakes`] and [`nix-command`] experimental features enabled.

[`flakes`]: @docroot@/development/experimental-features.md#xp-feature-flakes
[`nix-command`]: @docroot@/development/experimental-features.md#xp-nix-command

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
[nix-shell]$ mesonConfigurePhase
[nix-shell]$ ninjaBuildPhase
```

To test it:

```console
[nix-shell]$ mesonCheckPhase
```

To install it in `$(pwd)/outputs`:

```console
[nix-shell]$ ninjaInstallPhase
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
- `riscv64-linux`

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

Cross-compiled builds are available for:
- `armv6l-linux`
- `armv7l-linux`
- `riscv64-linux`
Add more [system types](#system-type) to `crossSystems` in `flake.nix` to bootstrap Nix on unsupported platforms.

### Building for multiple platforms at once

It is useful to perform multiple cross and native builds on the same source tree,
for example to ensure that better support for one platform doesn't break the build for another.
Meson thankfully makes this very easy by confining all build products to the build directory --- one simple shares the source directory between multiple build directories, each of which contains the build for Nix to a different platform.

Nixpkgs's `mesonConfigurePhase` always chooses `build` in the current directory as the name and location of the build.
This makes having multiple build directories slightly more inconvenient.
The good news is that Meson/Ninja seem to cope well with relocating the build directory after it is created.

Here's how to do that

1. Configure as usual

   ```bash
   mesonConfigurePhase
   ```

2. Rename the build directory

   ```bash
   cd .. # since `mesonConfigurePhase` cd'd inside
   mv build build-linux # or whatever name we want
   cd build-linux
   ```

3. Build as usual

   ```bash
   ninjaBuildPhase
   ```

> **N.B.**
> [`nixpkgs#335818`](https://github.com/NixOS/nixpkgs/issues/335818) tracks giving `mesonConfigurePhase` proper support for custom build directories.
> When it is fixed, we can simplify these instructions and then remove this notice.

## System type

Nix uses a string with the following format to identify the *system type* or *platform* it runs on:

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

To use the LSP with your editor, you will want a `compile_commands.json` file telling `clangd` how we are compiling the code.
Meson's configure always produces this inside the build directory.

Configure your editor to use the `clangd` from the `.#native-clangStdenvPackages` shell. You can do that either by running it inside the development shell, or by using [nix-direnv](https://github.com/nix-community/nix-direnv) and [the appropriate editor plugin](https://github.com/direnv/direnv/wiki#editor-integration).

> **Note**
>
> For some editors (e.g. Visual Studio Code), you may need to install a [special extension](https://open-vsx.org/extension/llvm-vs-code-extensions/vscode-clangd) for the editor to interact with `clangd`.
> Some other editors (e.g. Emacs, Vim) need a plugin to support LSP servers in general (e.g. [lsp-mode](https://github.com/emacs-lsp/lsp-mode) for Emacs and [vim-lsp](https://github.com/prabirshrestha/vim-lsp) for vim).
> Editor-specific setup is typically opinionated, so we will not cover it here in more detail.

## Formatting and pre-commit hooks

You may run the formatters as a one-off using:

```console
./maintainers/format.sh
```

If you'd like to run the formatters before every commit, install the hooks:

```
pre-commit-hooks-install
```

This installs [pre-commit](https://pre-commit.com) using [cachix/git-hooks.nix](https://github.com/cachix/git-hooks.nix).

When making a commit, pay attention to the console output.
If it fails, run `git add --patch` to approve the suggestions _and commit again_.

To refresh pre-commit hook's config file, do the following:
1. Exit the development shell and start it again by running `nix develop`.
2. If you also use the pre-commit hook, also run `pre-commit-hooks-install` again.
