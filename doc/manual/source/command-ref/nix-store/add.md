# Name

`nix-store --add` - add paths to Nix store

# Synopsis

`nix-store` `--add` *paths…*

# Description

The operation `--add` adds the specified paths to the Nix store. It
prints the resulting paths in the Nix store on standard output.

*paths* that refer to symlinks are not dereferenced, but added to the store
as symlinks with the same target.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

# Example

```console
$ nix-store --add ./foo.c
/nix/store/m7lrha58ph6rcnv109yzx1nk1cj7k7zf-foo.c
```
