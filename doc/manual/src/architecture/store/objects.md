# Store Object

Nix organizes the data it manages into *store objects*.
A store object is the pair of

  - a [file system object](#file-system-object)
  - a set of [references](#reference) to store objects.

## File system object {#file-system-object}

The Nix store uses a simple file system model.

    data FileSystemObject
      = File Executable Data
      | Directory (Map FileName FileSystemObject)
      | SymLink Path

Every file system object is one of the following:
 - File: an executable flag, and arbitrary data
 - Directory: mapping of names to child file system objects
 - [Symbolic link](https://en.m.wikipedia.org/wiki/Symbolic_link): may point anywhere.

   In particular, symlinks pointing outside of their own root file system object, or to a store object without a matching reference, are allowed, but might not function as intended.

A bare file or symlink can be a root file system object.

## Reference {#reference}

A store object can refer to both other store objects and itself.

Self-reference may seem pointless, but tracking them is in fact useful.
We can best explain why later after more concepts have been established.

References are normally calculated so as to to record the presence of textual references in store object's file systems obejcts.
This process will be described precisely in the section on [building](./building.md), once more concepts are explained, as building is the primary path new store objects with non-trivial references are created.

However, scanning for references is not mandatory.
Store objects are allowed to have official references that *don't* correspond to store paths contained in their contents,
and they are also allowed to *not* have references that *do* correspond to store paths contained in their store.
Taken together, this means there is no actual rule relating the store paths contained in the contents to the store paths deemed references.

This is why it's its necessary for correctness, and not just performance, that Nix remember the references of each store object, rather than try to recompute them on the fly by scanning their contents.
