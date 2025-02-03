# Name

`nix-store --add-fixed` - add paths to store using given hashing algorithm

## Synopsis

`nix-store` `--add-fixed` [`--recursive`] *algorithm* *paths…*

## Description

The operation `--add-fixed` adds the specified paths to the Nix store.
Unlike `--add` paths are registered using the specified hashing
algorithm, resulting in the same output path as a fixed-output
derivation. This can be used for sources that are not available from a
public url or broke since the download expression was written.

This operation has the following options:

- `--recursive`

  Use recursive instead of flat hashing mode, used when adding
  directories to the store.

  *paths* that refer to symlinks are not dereferenced, but added to the store
  as symlinks with the same target.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

## Example

```console
$ nix-store --add-fixed sha256 ./hello-2.10.tar.gz
/nix/store/3x7dwzq014bblazs7kq20p9hyzz0qh8g-hello-2.10.tar.gz
```

