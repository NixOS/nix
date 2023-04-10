# Name

`nix-store --import` - import Nix Archive into the store

# Synopsis

`nix-store` `--import`

# Description

The operation `--import` reads a serialisation of a set of store paths
produced by `nix-store --export` from standard input and adds those
store paths to the Nix store. Paths that already exist in the Nix store
are ignored. If a path refers to another path that doesn’t exist in the
Nix store, the import fails.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}
