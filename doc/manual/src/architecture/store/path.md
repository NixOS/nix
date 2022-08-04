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
  - [digest](#digest) rendered in a custom variant of [base-32](https://en.m.wikipedia.org/wiki/Base32) (20 arbitrary bytes become 32 ASCII characters)
  - hyphen (`-`)
  - name

Example:

      /nix/store/b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1
      |--------| |------------------------------| |----------|
    store directory            digest                 name

## Store Directory {#store-directory}

Every [store](./store.md) has a store directory.

If the store has a [file system representation](./store.md#files-and-processes), this directory contains the store’s [file system objects](#file-system-object), which can be addressed by [store paths](#store-path).

This means a store path is not just derived from the referenced store object itself, but depends on the store the store object is in.

::: {.note}
The store directory defaults to `/nix/store`, but is in principle arbitrary.
:::

It is important which store a given store object belongs to:
Files in the store object can contain store paths, and processes may read these paths.
Nix can only guarantee [referential integrity](store.md#closure) if store paths do not cross store boundaries.

Therefore one can only copy store objects to a different store if

- the source and target stores' directories match

  or

- the store object in question has no references, that is, contains no store paths.

One cannot copy a store object to a store with a different store directory.
Instead, it has to be rebuilt, together with all its dependencies.
It is in general not enough to replace the store directory string in file contents, as this may render executables unusable by invalidating their internal offsets or checksums.

# Digest {#digest}

In a [store path](#store-path), the [digest][digest] is the output of a [cryptographic hash function][hash] of either all *inputs* involved in building the referenced store object or its actual *contents*.

Store objects are therefore said to be either [input-addressed](#input-addressing) or [content-addressed](#content-addressing).

::: {.note}
**Historical note**: The 20 byte restriction is because originally digests were [SHA-1][sha-1] hashes.
Nix now uses [SHA-256][sha-256], and longer hashes are still reduced to 20 bytes for compatibility.
:::

[digest]: https://en.m.wiktionary.org/wiki/digest#Noun
[hash]: https://en.m.wikipedia.org/wiki/Cryptographic_hash_function
[sha-1]: https://en.m.wikipedia.org/wiki/SHA-1
[sha-256]: https://en.m.wikipedia.org/wiki/SHA-256

### Reference scanning

When a new store object is built, Nix scans its file contents for store paths to construct its set of references.

The special format of a store path's [digest](#digest) allows reliably detecting it among arbitrary data.
Nix uses the [closure](store.md#closure) of build inputs to derive the list of allowed store paths, to avoid false positives.

This way, scanning files captures run time dependencies without the user having to declare them explicitly.
Doing it at build time and persisting references in the store object avoids repeating this time-consuming operation.

::: {.note}
In practice, it is sometimes still necessary for users to declare certain dependencies explicitly, if they are to be preserved in the build result's closure.
This depends on the specifics of the software to build and run.

For example, Java programs are compressed after compilation, which obfuscates any store paths they may refer to and prevents Nix from automatically detecting them.
:::

## Input Addressing {#input-addressing}

Input addressing means that the digest derives from how the store object was produced, namely its build inputs and build plan.

To compute the hash of a store object one needs a deterministic serialisation, i.e., a binary string representation which only changes if the store object changes.

Nix has a custom serialisation format called Nix Archive (NAR)

Store object references of this sort can *not* be validated from the content of the store object.
Rather, a cryptographic signature has to be used to indicate that someone is vouching for the store object really being produced from a build plan with that digest.

## Content Addressing {#content-addressing}

Content addressing means that the digest derives from the store object's contents, namely its file system objects and references.
If one knows content addressing was used, one can recalculate the reference and thus verify the store object.

Content addressing is currently only used for the special cases of source files and "fixed-output derivations", where the contents of a store object are known in advance.
Content addressing of build results is still an [experimental feature subject to some restrictions](https://github.com/tweag/rfcs/blob/cas-rfc/rfcs/0062-content-addressed-paths.md).

