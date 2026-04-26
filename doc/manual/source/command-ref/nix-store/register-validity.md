# Name

`nix-store --register-validity` - register path validity information

# Synopsis

`nix-store` `--register-validity` [`--reregister`] [`--hash-given`]

# Description

The operation `--register-validity` reads validity registration
information from standard input and registers the specified paths as
valid in the Nix database. This is used internally by
[`exportReferencesGraph`](@docroot@/language/advanced-attributes.md#adv-attr-exportReferencesGraph)
and [`nix-store --load-db`](./load-db.md).

The input format consists of one or more entries, each describing a
single store path. Each entry has the following fields, one per line:

1. The store path (e.g., `/nix/store/...-hello-2.10`)
2. The deriver store path (e.g., `/nix/store/...-hello-2.10.drv`),
   or empty for paths with no known deriver
3. The number of references (an integer)
4. The store paths of references, one per line (repeated for the
   count given above)

When the `--hash-given` flag is used, two additional fields are
expected between the store path and the deriver:

1. The NAR hash in hexadecimal (SHA-256)
2. The NAR size in bytes

For example, the following input registers a single store path with
one reference:

```
/nix/store/...-hello-2.10

1
/nix/store/...-glibc-2.38
```

In this example, the deriver field is left empty (the blank line), and
the path has one reference.

This operation has the following flags:

  - `--reregister`

    Allow re-registration of already valid paths. By default, paths
    already present in the database are skipped.

  - `--hash-given`

    Expect the NAR hash and NAR size to be provided in the input,
    instead of computing them from the store path contents. This is
    the format produced by [`nix-store --dump-db`](./dump-db.md).

> **Note**
>
> This is a low-level operation used internally by Nix. Paths must
> already exist in the store directory before they can be registered.
> The caller is responsible for providing a complete
> [closure](@docroot@/glossary.md#gloss-closure).

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

# Examples

> **Example 1: Register a path with no deriver and no references**
>
> ```console
> $ echo "/nix/store/abc...-foo
>
> 0" | nix-store --register-validity
> ```

> **Example 2: Register a path with one reference**
>
> ```console
> $ printf '%s\n' \
>     /nix/store/abc...-hello \
>     "" \
>     1 \
>     /nix/store/xyz...-glibc \
>   | nix-store --register-validity
> ```
