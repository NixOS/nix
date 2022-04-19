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

### Comparison with Git

This is close to Git's model, but with one crucial difference:
Git puts the "permission" info within the directory map's values instead of making it part of the file (blob, in it's parlance) object.

    data GitObject
      = Blob ByteString
      | Tree (Map FileName (Persission, FSO))

    data Persission
      = Directory -- IFF paired with tree
      -- Iff paired with blob, one of:
      | RegFile
      | ExecutableFile
      | Symlink

So long as the root object is a directory, the representations are isomorphic.
There is no "wiggle room" the git way since whenever the permission info wouldn't matter (e.g. the child object being mapped to is a directory), the permission info must be a sentinel value.

However, if the root object is a blob, there is loss of fidelity.
Since the permission info is used to distinguish executable files, non-executable files, and symlinks, but there isn't a "parent" directory of the root to contain that info, these 3 cases cannot be distinguished.

Git's model matches Unix tradition, but Nix's model is more natural.

## References

Store objects can refer to both other store objects and themselves.

Self-reference may seem pointless, but tracking them is in fact useful.
We can best explain why later after more concepts have been established.

References are normally calculated by scanning the rooted file system objects for store paths (which we describe in the next section) referring to store objects.
For now, it suffices to say that a store path is a string encoding of a reference to a store paths, and therefore it is something that we can search for in the contents of files, and thus in store objects by searching in all their files.   
When we get to building in a future section, this process will be described in precise detail.

However, scanning for references is not mandatory.
Store objects are allowed to have official references that *don't* correspond to store paths contained in their contents,
and they are also allowed to *not* have references that *do* correspond to store paths contained in their store.
Taken together, this means there is no actual rule relating the store paths contained in the contents to the store paths deemed references.

This is why it's its necessary for correctness, and not just performance, that Nix remember the references of each store object, rather than try to recompute them on the fly by scanning their contents.
