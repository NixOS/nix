# Name

`nix-store --export` - export store paths to a Nix Archive

## Synopsis

`nix-store` `--export` *paths…*

## Description

The operation `--export` writes a serialisation of the specified store
paths to standard output in a format that can be imported into another
Nix store with `nix-store --import`. This is like `nix-store
--dump`, except that the NAR archive produced by that command doesn’t
contain the necessary meta-information to allow it to be imported into
another Nix store (namely, the set of references of the path).

This command does not produce a *closure* of the specified paths, so if
a store path references other store paths that are missing in the target
Nix store, the import will fail.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

# Examples

To copy a whole closure, do something
like:

```console
$ nix-store --export $(nix-store --query --requisites paths) > out
```

To import the whole closure again, run:

```console
$ nix-store --import < out
```
