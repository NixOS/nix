# Store Path

> **Example**
>
> `/nix/store/a040m110amc4h71lds2jmr8qrkj2jhxd-git-2.38.1`
>
> A rendered store path

Nix implements references to [store objects](object.md) as *store paths*.

Think of a store path as an [opaque], [unique identifier]:
The only way to obtain store path is by adding or building store objects.
A store path will always reference exactly one store object.

[opaque]: https://en.m.wikipedia.org/wiki/Opaque_data_type
[unique identifier]: https://en.m.wikipedia.org/wiki/Unique_identifier

Store paths are pairs of

- A 20-byte digest for identification
- A symbolic name for people to read

> **Example**
>
> - Digest: `b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z`
> - Name:   `firefox-33.1`

To make store objects accessible to operating system processes, stores have to expose store objects through the file system.

A store path is rendered to a file system path as the concatenation of

- [Store directory](#store-directory) (typically `/nix/store`)
- Path separator (`/`)
- Digest rendered in a custom variant of [Base32](https://en.wikipedia.org/wiki/Base32) (20 arbitrary bytes become 32 ASCII characters)
- Hyphen (`-`)
- Name

> **Example**
>
> ```
>   /nix/store/b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1
>   |--------| |------------------------------| |----------|
> store directory            digest                 name
> ```

Exactly how the digest is calculated depends on the type of store path.
Store path digests are *supposed* to be opaque, and so for most operations, it is not necessary to know the details.
That said, the manual has a full [specification of store path digests](@docroot@/protocols/store-path.md).

## Store Directory

Every [Nix store](./index.md) has a store directory.

Not every store can be accessed through the file system.
But if the store has a file system representation, the store directory contains the store’s [file system objects], which can be addressed by [store paths](#store-path).

[file system objects]: ./file-system-object.md

This means a store path is not just derived from the referenced store object itself, but depends on the store that the store object is in.

> **Note**
>
> The store directory defaults to `/nix/store`, but is in principle arbitrary.

It is important which store a given store object belongs to:
Files in the store object can contain store paths, and processes may read these paths.
Nix can only guarantee referential integrity if store paths do not cross store boundaries.

Therefore one can only copy store objects to a different store if

- The source and target stores' directories match

  or

- The store object in question has no references, that is, contains no store paths

One cannot copy a store object to a store with a different store directory.
Instead, it has to be rebuilt, together with all its dependencies.
It is in general not enough to replace the store directory string in file contents, as this may render executables unusable by invalidating their internal offsets or checksums.

## Name

The name part is a human readable name for the path.

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
