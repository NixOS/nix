# Name

`nix-store --export` - export store paths to a Nix Archive

## Synopsis

`nix-store` `--export` *paths…*

## Description

The operation `--export` writes a serialisation of the given [store objects](@docroot@/glossary.md#gloss-store-object) to standard output in a format that can be imported into another [Nix store](@docroot@/store/index.md) with [`nix-store --import`](./import.md).

> **Warning**
>
> This command *does not* produce a [closure](@docroot@/glossary.md#gloss-closure) of the specified store paths.
> Trying to import a store object that refers to store paths not available in the target Nix store will fail.
>
> Use [`nix-store --query`](@docroot@/command-ref/nix-store/query.md) to obtain the closure of a store path.

This command is different from [`nix-store --dump`](./dump.md), which produces a [Nix archive](@docroot@/glossary.md#gloss-nar) that *does not* contain the set of [references](@docroot@/glossary.md#gloss-reference) of a given store path.

> **Note**
>
> For efficient transfer of closures to remote machines over SSH, use [`nix-copy-closure`](@docroot@/command-ref/nix-copy-closure.md).

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
