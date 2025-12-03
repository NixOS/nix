# Debugging Nix

This section shows how to build and debug Nix with debug symbols enabled.

Additionally, see [Testing Nix](./testing.md) for further instructions on how to debug Nix in the context of a unit test or functional test.

## Building Nix with Debug Symbols

In the development shell, set the `mesonBuildType` environment variable to `debug` before configuring the build:

```console
[nix-shell]$ export mesonBuildType=debugoptimized
```

Then, proceed to build Nix as described in [Building Nix](./building.md).
This will build Nix with debug symbols, which are essential for effective debugging.

It is also possible to build without optimization for faster build:

```console
[nix-shell]$ NIX_HARDENING_ENABLE=$(printLines $NIX_HARDENING_ENABLE | grep -v fortify)
[nix-shell]$ export mesonBuildType=debug
```

(The first line is needed because `fortify` hardening requires at least some optimization.)

## Building Nix with sanitizers

Nix can be built with [Address](https://clang.llvm.org/docs/AddressSanitizer.html) and
[UB](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) sanitizers using LLVM
or GCC. This is useful when debugging memory corruption issues.

```console
[nix-shell]$ export mesonBuildType=debugoptimized
[nix-shell]$ appendToVar mesonFlags "-Dlibexpr:gc=disabled" # Disable Boehm
[nix-shell]$ appendToVar mesonFlags "-Dbindings=false" # Disable nix-perl
[nix-shell]$ appendToVar mesonFlags "-Db_sanitize=address,undefined"
```

## Debugging the Nix Binary

Obtain your preferred debugger within the development shell:

```console
[nix-shell]$ nix-shell -p gdb
```

On macOS, use `lldb`:

```console
[nix-shell]$ nix-shell -p lldb
```

### Launching the Debugger

To debug the Nix binary, run:

```console
[nix-shell]$ gdb --args ../outputs/out/bin/nix
```

On macOS, use `lldb`:

```console
[nix-shell]$ lldb -- ../outputs/out/bin/nix
```

### Using the Debugger

Inside the debugger, you can set breakpoints, run the program, and inspect variables.

```gdb
(gdb) break main
(gdb) run <arguments>
```

Refer to the [GDB Documentation](https://www.gnu.org/software/gdb/documentation/) for comprehensive usage instructions.

On macOS, use `lldb`:

```lldb
(lldb) breakpoint set --name main
(lldb) process launch -- <arguments>
```

Refer to the [LLDB Tutorial](https://lldb.llvm.org/use/tutorial.html) for comprehensive usage instructions.
