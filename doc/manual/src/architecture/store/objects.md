# Store Object

Nix organizes the data it manages into *store objects*.
A store object is the pair of

  - a [file system object](#file-system-object)
  - a set of [references](#reference) to store objects.

## File system object {#file-system-object}

The Nix store uses a simple file system model.

    data FileSystemObject
      = File Executable Contents
      | Directory (Map FileName FileSystemObject)
      | SymLink Path

Every file system object is one of the following:
 - File: an executable flag, and arbitrary data for contents
 - Directory: mapping of names to child file system objects
 - [Symbolic link](https://en.m.wikipedia.org/wiki/Symbolic_link): may point anywhere.

   In particular, symlinks pointing outside of their own root file system object, or to a store object without a matching reference, are allowed, but might not function as intended.

A bare file or symlink can be a root file system object.

## Reference {#reference}

A store object can refer to other store objects or itself.

Nix collects these references by scanning file contents for [store paths](./paths.md) when a new store object is created.

While references could be arbitrary paths, Nix requires them to be store paths to ensure correctness:
Anything outside a given store is not under control of Nix, and therefore cannot be guaranteed to be present when needed.

However, having references match store paths in files is not enforced by the data model:
Store objects could have excess or incomplete references with respect to store paths found in their file contents.

Scanning files therefore allows reliably capturing run time dependencies without declaring them explicitly.
Doing it at build time and persisting references in the store object avoids repeating this time-consuming operation.

