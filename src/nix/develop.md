R""(

# Examples

* Start a shell with the build environment of the default package of
  the flake in the current directory:

  ```console
  # nix develop
  ```

  Typical commands to run inside this shell are:

  ```console
  # configurePhase
  # buildPhase
  # installPhase
  ```

  Alternatively, you can run whatever build tools your project uses
  directly, e.g. for a typical Unix project:

  ```console
  # ./configure --prefix=$out
  # make
  # make install
  ```

* Run a particular build phase directly:

  ```console
  # nix develop --configure
  # nix develop --build
  # nix develop --check
  # nix develop --install
  # nix develop --installcheck
  ```

* Start a shell with the build environment of GNU Hello:

  ```console
  # nix develop nixpkgs#hello
  ```

* Record a build environment in a profile:

  ```console
  # nix develop --profile /tmp/my-build-env nixpkgs#hello
  ```

* Use a build environment previously recorded in a profile:

  ```console
  # nix develop /tmp/my-build-env
  ```

* Replace all occurences of the store path corresponding to
  `glibc.dev` with a writable directory:

  ```console
  # nix develop --redirect nixpkgs#glibc.dev ~/my-glibc/outputs/dev
  ```

  Note that this is useful if you're running a `nix develop` shell for
  `nixpkgs#glibc` in `~/my-glibc` and want to compile another package
  against it.

# Description

`nix develop` starts a `bash` shell that provides an interactive build
environment nearly identical to what Nix would use to build
*installable*. Inside this shell, environment variables and shell
functions are set up so that you can interactively and incrementally
build your package.

Nix determines the build environment by building a modified version of
the derivation *installable* that just records the environment
initialised by `stdenv` and exits. This build environment can be
recorded into a profile using `--profile`.

The prompt used by the `bash` shell can be customised by setting the
`bash-prompt` and `bash-prompt-suffix` settings in `nix.conf` or in
the flake's `nixConfig` attribute.

# Flake output attributes

If no flake output attribute is given, `nix develop` tries the following
flake output attributes:

* `devShell.<system>`

* `defaultPackage.<system>`

If a flake output *name* is given, `nix develop` tries the following flake
output attributes:

* `devShells.<system>.<name>`

* `packages.<system>.<name>`

* `legacyPackages.<system>.<name>`

)""
