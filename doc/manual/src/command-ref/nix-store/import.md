# Name

`nix-store --import` - import Nix Archive into the store

# Synopsis

`nix-store` `--import`

# Description

The operation `--import` reads a serialisation of a set of [store objects](@docroot@/glossary.md#gloss-store-object) produced by [`nix-store --export`](./export.md) from standard input, and adds those store objects to the specified [Nix store](@docroot@/store/index.md).
Paths that already exist in the target Nix store are ignored.
If a path [refers](@docroot@/glossary.md#gloss-reference) to another path that doesn’t exist in the target Nix store, the import fails.

> **Note**
>
> For efficient transfer of closures to remote machines over SSH, use [`nix-copy-closure`](@docroot@/command-ref/nix-copy-closure.md).

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}
