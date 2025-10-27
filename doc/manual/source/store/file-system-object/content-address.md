# Content-Addressing File System Objects

For many operations, Nix needs to calculate [a content addresses](@docroot@/glossary.md#gloss-content-address) of [a file system object][file system object] (FSO).
Usually this is needed as part of
[content addressing store objects](../store-object/content-address.md),
since store objects always have a root file system object.
But some command-line utilities also just work on "raw" file system objects, not part of any store object.

Every content addressing scheme Nix uses ultimately involves feeding data into a [hash function](https://en.wikipedia.org/wiki/Hash_function), and getting back an opaque fixed-size digest which is deemed a content address.
The various *methods* of content addressing thus differ in how abstract data (in this case, a file system object and its descendants) are fed into the hash function.

## Serialising File System Objects { #serial }

The simplest method is to serialise the entire file system object tree into a single binary string, and then hash that binary string, yielding the content address.
In this section we describe the currently-supported methods of serialising file system objects.

### Flat { #serial-flat }

A single file object can just be hashed by its contents.
This is not enough information to encode the fact that the file system object is a file,
but if we *already* know that the FSO is a single non-executable file by other means, it is sufficient.

Because the hashed data is just the raw file, as is, this choice is good for compatibility with other systems.
For example, Unix commands like `sha256sum` or `sha1sum` will produce hashes for single files that match this.

### Nix Archive (NAR) { #serial-nix-archive }

For the other cases of [file system objects][file system object], especially directories with arbitrary descendants, we need a more complex serialisation format.
Examples of such serialisations are the ZIP and TAR file formats.
However, for our purposes these formats have two problems:

- They do not have a canonical serialisation, meaning that given an FSO, there can
be many different serialisations.
  For instance, TAR files can have variable amounts of padding between archive members;
  and some archive formats leave the order of directory entries undefined.
  This would be bad because we use serialisation to compute cryptographic hashes over file system objects, and for those hashes to be useful as a content address or for integrity checking, uniqueness is crucial.
  Otherwise, correct hashes would report false mismatches, and the store would fail to find the content.

- They store more information than we have in our notion of FSOs, such as time stamps.
  This can cause FSOs that Nix should consider equal to hash to different values on different machines, just because the dates differ.

- As a practical consideration, the TAR format is the only truly universal format in the Unix environment.
  It has many problems, such as an inability to deal with long file names and files larger than 2^33 bytes.
  Current implementations such as GNU Tar work around these limitations in various ways.

For these reasons, Nix has its very own archive format—the Nix Archive (NAR) format,
which is carefully designed to avoid the problems described above.

The exact specification of the Nix Archive format is in [specified here](../../protocols/nix-archive/index.md).

## Content addressing File System Objects beyond a single serialisation pass

Serialising the entire tree and then hashing that binary string is not the only option for content addressing, however.
Another technique is that of a [Merkle graph](https://en.wikipedia.org/wiki/Merkle_tree), where previously computed hashes are included in subsequent byte strings to be hashed.

In particular, the Merkle graphs can match the original graph structure of file system objects:
we can first hash (serialised) child file system objects, and then hash parent objects using the hashes of their children in the serialisation (to be hashed) of the parent file system objects.

Currently, there is one such Merkle DAG content addressing method supported.

### Git ([experimental][xp-feature-git-hashing]) { #git }

> **Warning**
>
> This method is part of the [`git-hashing`][xp-feature-git-hashing] experimental feature.

Git's file system model is very close to Nix's, and so Git's content addressing method is a pretty good fit.
Just as with regular Git, files and symlinks are hashed as git "blobs", and directories are hashed as git "trees".

However, one difference between Nix's and Git's file system model needs special treatment.
Plain files, executable files, and symlinks are not differentiated as distinctly addressable objects, but by their context: by the directory entry that refers to them.
That means so long as the root object is a directory, there is no problem:
every non-directory object is owned by a parent directory, and the entry that refers to it provides the missing information.
However, if the root object is not a directory, then we have no way of knowing which one of an executable file, non-executable file, or symlink it is supposed to be.

In response to this, we have decided to treat a bare file as non-executable file.
This is similar to do what we do with [flat serialisation](#serial-flat), which also lacks this information.
To avoid an address collision, attempts to hash a bare executable file or symlink will result in an error (just as would happen for flat serialisation also).
Thus, Git can encode some, but not all of Nix's "File System Objects", and this sort of content-addressing is likewise partial.

In the future, we may support a Git-like hash for such file system objects, or we may adopt another Merkle DAG format which is capable of representing all Nix file system objects.


[file system object]: ../file-system-object.md
[store object]: ../store-object.md
[xp-feature-git-hashing]: @docroot@/development/experimental-features.md#xp-feature-git-hashing
