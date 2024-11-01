# Debugging Nix

This section provides instructions on how to build and debug Nix with debug
symbols enabled.
It assumes you are using Nix with the [`flakes`] and [`nix-command`]
experimental features enabled.

[`flakes`]: @docroot@/development/experimental-features.md#xp-feature-flakes
[`nix-command`]: @docroot@/development/experimental-features.md#xp-nix-command

## Building Nix with Debug Symbols

First, ensure you have set up the development environment as described in the
[building documentation](./building.md).

In the development shell, set the `mesonBuildType` environment variable to
`debug` before configuring the build:

```console
[nix-shell]$ export mesonBuildType=debugoptimized
```

Then, configure and build Nix:

```console
[nix-shell]$ mesonConfigurePhase
[nix-shell]$ ninjaInstallPhase
```

This will build Nix with debug symbols, which are essential for effective
debugging.

## Debugging the Nix Binary

### Installing a Debugger

Install your preferred debugger within the development shell. For example, to
install `lldb`:

```console
[nix-shell]$ nix shell nixpkgs#lldb
```

### Launching the Debugger

To debug the Nix binary you just built:

```console
[nix-shell]$ lldb -- ./subprojects/nix/nix
```

### Using the Debugger

Inside `lldb`, you can set breakpoints, run the program, and inspect variables:

```lldb
(lldb) breakpoint set --name main
(lldb) process launch -- <arguments>
```

Refer to the [LLDB Tutorial](https://lldb.llvm.org/use/tutorial.html) for
comprehensive usage instructions.
