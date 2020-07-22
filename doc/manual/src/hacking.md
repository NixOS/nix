# Hacking

This section provides some notes on how to hack on Nix. To get the
latest version of Nix from GitHub:

    $ git clone https://github.com/NixOS/nix.git
    $ cd nix

To build Nix for the current operating system/architecture use

    $ nix-build

or if you have a flakes-enabled nix:

    $ nix build

This will build `defaultPackage` attribute defined in the `flake.nix`
file. To build for other platforms add one of the following suffixes to
it: aarch64-linux, i686-linux, x86\_64-darwin, x86\_64-linux. i.e.

    $ nix-build -A defaultPackage.x86_64-linux

To build all dependencies and start a shell in which all environment
variables are set up so that those dependencies can be found:

    $ nix-shell

To build Nix itself in this shell:

    [nix-shell]$ ./bootstrap.sh
    [nix-shell]$ ./configure $configureFlags
    [nix-shell]$ make -j $NIX_BUILD_CORES

To install it in `$(pwd)/inst` and test it:

    [nix-shell]$ make install
    [nix-shell]$ make installcheck
    [nix-shell]$ ./inst/bin/nix --version
    nix (Nix) 2.4

If you have a flakes-enabled nix you can replace:

    $ nix-shell

by:

    $ nix develop
