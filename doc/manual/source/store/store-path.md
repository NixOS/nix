# Store Path

> **Example**
>
> `/nix/store/a040m110amc4h71lds2jmr8qrkj2jhxd-git-2.38.1`
>
> A rendered store path

Nix implements references to [store objects](./store-object.md) as *store paths*.

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

The part of the render does not contain the store dir, the "base name" of the path, is called a *store path base name*.

> **Example**
>
> ```
>   b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1
>   |------------------------------| |----------|
>                 digest                 name
> ```

Semantically, this simply *is* the store path without the additional store directory prepended.
But because the usual rendering of a store path contains the base name, the longer phrase is useful to destinguish this alternative rendering.

Exactly how the digest is calculated depends on the type of store path.
Store path digests are *supposed* to be opaque, and so for most operations, it is not necessary to know the details.
That said, the manual has a full [specification of store path digests](@docroot@/protocols/store-path.md).

## Store Directory

Every [Nix store](./index.md) has a store directory.
This is an absolute, lexically canonnical (not containing any `..`, `.`, or similar) path which points to the directory where all store objects are to be found.

> **Note**
>
> The store directory defaults to `/nix/store`, but is in principle arbitrary.

Not every store can be accessed through the file system.
But if the store has a file system representation, the following should be true:

- The store directory path is canonical: no prefix of the path (i.e. path of the first *n* path segments) points to a symlink.
  (This is a separate condition in addition to "lexical canonicity", which is a property of just the path itself, whereas regular "canonicity" is an additional property about the path and the filesystem it navigates jointly.)

- The store directory path in fact points to a directory.

- The store directory contains, for every store object in the store, the [file system object] of that store object at the store path base name.

[file system objects]: ./file-system-object.md

The above properties mean that the following file acceses will work.
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
(And actually, many of the ways of computing the digest also dependent on the store directory path.
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
