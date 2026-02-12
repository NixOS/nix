# Store Path and Store Directory

Nix's [store object] and [file system object] data models are minimal and abstract.
But to actually be used by software, store objects need to be made available through the operating system's file system.

This is done by exposing all the store objects in a single *[store directory][store directory path]*.
Every entry in that directory is a *[store path base name]* pointing to a store object.
Store objects exposed in this way can then be referenced by *[store paths][store path]*.

[store object]: ./store-object.md
[file system object]: ./file-system-object.md
[store path]: #store-path
[store path base name]: #store-path-base-name
[store directory path]: #store-directory-path

## Store Path Base Name

Nix implements references to store objects as *store path base names*.

Think of a store path base name as an [opaque], [unique identifier]:
The only way to obtain a store path base name is by adding or building store objects.
A store path base name will always reference exactly one store object.

[opaque]: https://en.m.wikipedia.org/wiki/Opaque_data_type
[unique identifier]: https://en.m.wikipedia.org/wiki/Unique_identifier

Store path base names are pairs of

- A 20-byte digest for identification
- A symbolic name for people to read

> **Example**
>
> - Digest: `q06x3jll2yfzckz2bzqak089p43ixkkq`
> - Name:   `firefox-33.1`

A store path base name is rendered to a string as the concatenation of

- Digest rendered in a custom variant of [Base32] (20 arbitrary bytes become 32 ASCII characters)
- Digest rendered in [Nix32](@docroot@/protocols/nix32.md), a variant of [base-32](https://en.wikipedia.org/wiki/Base32) (20 hash bytes become 32 ASCII characters)
- Hyphen (`-`)
- Name

> **Example**
>
> ```
> q06x3jll2yfzckz2bzqak089p43ixkkq-firefox-33.1
> |------------------------------| |----------|
>               digest                 name
> ```

[Base32]: https://en.wikipedia.org/wiki/Base32

Exactly how the digest is calculated depends on the type of store object being referenced.
Store path digests are *supposed* to be opaque, and so for most operations, it is not necessary to know the details.
That said, the manual has a full [specification of store path digests](@docroot@/protocols/store-path.md).

## Store Directory Path

Every [Nix store] has a store directory path.
This is an absolute, lexically canonical (not containing any `..`, `.`, or similar) path which points to the directory where all store objects are to be found.

[Nix store]: ./index.md

> **Note**
>
> The store directory defaults to `/nix/store`, but is in principle arbitrary.

## Store Path

A store path is the pair of a store directory path and a [store path base name].
It is rendered to a file system path as the concatenation of

- [Store directory] (typically `/nix/store`)
- Path separator (`/`)
- The [store path base name]

> **Example**
>
> ```
>   /nix/store/q06x3jll2yfzckz2bzqak089p43ixkkq-firefox-33.1
>   |--------| |------------------------------| |----------|
> store directory            digest                 name
> ```

When we have fixed a given store, or given store directory path (that all the stores in use share), the abstract syntax for store paths and the abstract syntax for store path base names coincide: the store directory path is known from context, so only the other two fields vary from one store path to the next.

## Exposing Store Objects in OS File Systems {#exposing}

Not every store can be accessed through the file system.
But if the store has a file system representation, the following should be true:

- The store directory path is canonical: no prefix of the path (i.e. path of the first *n* path segments) points to a symlink.
  (This is a separate condition in addition to "lexical canonicity", which is a property of just the path itself, whereas regular "canonicity" is an additional property about the path and the filesystem it navigates jointly.)

- The store directory path in fact points to a directory.

- The store directory contains, for every store object in the store, the [file system object] of that store object at the (rendered) [store path base name].
  The permissions and other metadata for these files in the store directory is in the normal form described in [Exposing in OS file systems](./file-system-object/os-file-system.md).

The above properties mean that the following file accesses will work.
Suppose we have a store available on the file system per the above rules, and `b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1` is the store path base name of a store object in that store.

- Suppose that the store directory (path) is `/foo/bar`.
  Then, `/foo/bar/b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1` exists and is the file system object of that store object.

- Suppose that we don't know what the store directory path of the store is, but we do have a capability `storeDir` to the store directory on the file system.
  (This would be a "file descriptor" on Unix, or a "file handle" on Windows.)
  Then (using the Unix notation for this):
  ```
  openat(storeDir, "b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1", O_NOFOLLOW)
  ```
  will succeed (so long as the file system object is not a symlink), and the yielded capability will point to the file system object of that store object.

  (The behavior for symlinks is harder to specify because of limitations in POSIX.)

## Relocating store objects

The inclusion of the store directory path in the full rendered store path means that it is not just derived from the referenced store object itself, but depends on the store that the store object is in.
(And actually, many of the ways of computing the digest also depend on the store directory path.
So this is also true even just for store path base names, in general.)

It is therefore important to consider which store a given store object belongs to:
Files in the store object can contain store paths, and processes may read these paths.
Nix can only guarantee referential integrity if store paths do not cross store boundaries.

One can only copy store objects to a different store if

- The source and target stores' directories match

  or

- The store object in question has no references, that is, contains no store paths

One cannot copy a store object to a store with a different store directory.
Instead, it has to be rebuilt, together with all its dependencies.
It is in general not enough to replace the store directory string in file contents, as this may render executables unusable by invalidating their internal offsets or checksums.
