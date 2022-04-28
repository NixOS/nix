# Store Objects

Data in Nix is chiefly organized into *store objects*.
A store object is the pair of

  - A (root) file system object
  - A set of references to store objects

## File system objects

The Nix store uses a simple filesystem model.

    data FileSystemObject
      = Regular Executable ByteString
      | Directory (Map FileName FSO)
      | SymLink ByteString

    data Executable
      = Executable
      | NonExecutable

In particular, every file system object falls into these three cases:

 - File: an executable flag, and arbitrary data

 - Directory: mapping of names to child file system objects.

 - Symlink: may point anywhere.

   In particular, symlinks that do not point within the containing root file system object or that of another store object referenced by the containing store object are allowed, but might not function as intended.

A bare file or symlink as the "root" file system object is allowed.


## References

Store objects can refer to both other store objects and themselves.

Self-reference may seem pointless, but tracking them is in fact useful.
We can best explain why later after more concepts have been established.

References are normally calculated so as to to record the presence of textual references in store object's file systems obejcts.
This process will be described precisely in the section on [building](./building.md), once more concepts are explained, as building is the primary path new store objects with non-trivial references are created.

However, scanning for references is not mandatory.
Store objects are allowed to have official references that *don't* correspond to store paths contained in their contents,
and they are also allowed to *not* have references that *do* correspond to store paths contained in their store.
Taken together, this means there is no actual rule relating the store paths contained in the contents to the store paths deemed references.

This is why it's its necessary for correctness, and not just performance, that Nix remember the references of each store object, rather than try to recompute them on the fly by scanning their contents.
