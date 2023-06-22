# Store Path

Nix implements [references](store.md#reference) to [store objects](store.md#store-object) as *store paths*.

Store paths are pairs of

- a 20-byte [digest](#digest) for identification
- a symbolic name for people to read.

Example:

- digest: `b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z`
- name:   `firefox-33.1`

It is rendered to a file system path as the concatenation of

  - [store directory](#store-directory)
  - path-separator (`/`)
  - [digest](#digest)
  - hyphen (`-`)
  - name

Example:

```
  /nix/store/b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1
  |--------| |------------------------------| |----------|
store directory            digest                 name
```

## Store Directory

Every [store](./store.md) has a store directory.

If the store has a [file system representation](./store.md#files-and-processes), this directory contains the store’s [file system objects](#file-system-object), which can be addressed by [store paths](#store-path).

This means a store path is not just derived from the referenced store object itself, but depends on the store the store object is in.

> **Note**
> The store directory defaults to `/nix/store`, but is in principle arbitrary.

It is important which store a given store object belongs to:
Files in the store object can contain store paths, and processes may read these paths.
Nix can only guarantee [referential integrity](closure.md) if store paths do not cross store boundaries.

Therefore one can only copy store objects to a different store if either

- the source and target stores' directories match

- the store object in question has no references, that is, contains no store paths.

One cannot copy a store object to a store with a different store directory.
Instead, it has to be rebuilt, together with all its dependencies.
It is in general not enough to replace the store directory string in file contents, as this may render executables unusable by invalidating their internal offsets or checksums.

## Name

The name part is a human readable name for the path,

> TODO move derivation parts later, haven't introduce these details yet

It is typically obtained from the name and output name of the derivation, or the name of the source file from which the store path is created.
For derivation outputs other than the default "out" output, the string `-<id>` is suffixed to `<name>`.

## Digest

The [digest][digest] is a [cryptographic hash][hash] of enough information to determine the store object.

It is rendered as in a custom variant of [base-32](https://en.m.wikipedia.org/wiki/Base32), with 20 arbitrary bytes of information expanding 32 ASCII characters.

The hash itself is the first 20 byte (160 bits) of a [SHA-256][sha-256] hash of the underlying information.

> **Historical Note**
> The 20 byte length is because originally digests were [SHA-1][sha-1] hashes.

Finally, the underlying information is a string (let's call it `pre`) in the following form:

```bnf
<pre> ::= <type>:sha256:<inner-digest>:<store-dir>:<name>
```

Note how it includes the other parts of the store path --- the store dir as well as the name.
This makes sure that changes to either of those are reflected in the hash.
For example, you won't get `/nix/store/<digest>-name1` and `/nix/store/<digest>-name2` with equal hash parts (absent an exceedingly improbable hash collision).

Putting it altogether, we have

```idris
digest = truncate (sha256 (intercallate ":" type "sha256" innerDigest storeDir name))
```

[digest]: https://en.m.wiktionary.org/wiki/digest#Noun
[hash]: https://en.m.wikipedia.org/wiki/Cryptographic_hash_function
[sha-1]: https://en.m.wikipedia.org/wiki/SHA-1
[sha-256]: https://en.m.wikipedia.org/wiki/SHA-256


### The Type and the Inner Digest

The remaining bits of information making up the pre-image of the digest are the *store object type* and the *inner digest*.

Unlike the information discussed so far, these two values depend on the type of store object we are referencing.
Broadly speaking, store objects are referenced in one of two ways:

 - *content-addressed*: where the store object is referred to its own contents

 - *input-addressed*: where the store object is referred to by the way in which it was made.

We will go over the details of how these two methods determine the type and inner digest in subsequent sections.

## Summary

This is the complete store path grammar.
This includes the details of the type and digest which, as stated just above, are yet to be described.
We do this to collect the entirety of the grammar in one place.

```bnf
<realized-path> ::= <store-dir>/<digest>-<name>
```
and that `<digest>` = base-32 representation of the first 160 bits of a SHA-256
hash of `<pre>`; the hash part of the store name

- `<pre>` = the string `<type>:sha256:<inner-digest>:<store>:<name>`;

  Note that it includes the location of the store as well as the name to make sure that changes to either of those are reflected in the hash
  (e.g. you won't get `/nix/store/<digest>-name1` and `/nix/store/<digest>-name2` with equal hash parts).

- `<type>` = one of:

  - ```bnf
    text:<r1>:<r2>:...<rN>
    ```

    for encoded derivations written to the store.
    `<r1> ... <rN>` are the store paths referenced by this path.
    Those are encoded in the form described by `<realized-path>`.

  - ```bnf
    source:<r1>:<r2>:...:<rN>:self
    ```

    For paths copied to the store and hashed via a [Nix Archive (NAR)](./ca-fso.md#nix-archive) and [SHA-256](sha-256).
    Just like in the text case, we can have the store objects referenced by their paths.
    Additionally, we can have an optional `:self` label to denote self reference.

  - ```bnf
    output:<id>
    ```

    For either the outputs built from derivations, OR paths copied to the store hashed that area single file hashed directly, or the via a hash algorithm other than [SHA-256](sha-256).
    (in that case "source" is used; it's silly, but it's done that way for compatibility).

    `<id>` is the name of the output (usually, "out").
    For content-addressed store objects, `<id>`, is always "out".

- `<inner-digest>` = base-16 representation of a SHA-256 hash of `<inner-pre>`

- `<inner-pre>` = one of the following based on `<type>`:

  - if `<type>` = `text:...`:

    the string written to the resulting store path.

  - if `<type>` = `source:...`:

    the serialisation of the path from which this store path is copied, as returned by hashPath()

  - if `<type>` = `output:<id>`:

    - For input-addressed derivation outputs:

      the hash of the derivation modulo fixed output derivations.


    - For content-addressed store paths:

      the string `fixed:out:<rec><algo>:<hash>:`, where

        - `<rec>` = one of:

          - `r:` for [NAR](./ca-fso.md#nix-archive) (arbitrary file system object) hashes

          - `` (empty string) for flat (single file) hashes

        - `<algo>` = `md5`, `sha1` or `sha256`

        -`<hash>` = base-16 representation of the path or flat hash of the contents of the path (or expected contents of the path for fixed-output derivations).
