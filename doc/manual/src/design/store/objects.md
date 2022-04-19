# Store Objects

File system data in Nix is organized into *store objects*.
A store object is the combination of

  - A (root) file system object
  - references to store objects

## File system objects

The nix store uses a simple filesystem model, similar to the one Git uses.
In particular, every file system object falls into these three cases:

 - File: arbitrary data

 - Directory: mapping of names to child file system objects.
   File children additionally have an executable flag.

 - Symlink: may point anywhere.
   In particular, Symlinks that do not point within the containing root file system object or that of another store object referenced by the containing store object are allowed, but might not function as intended.

A bare file as the "root" file system object is allowed.
Note that it cannot be executable, though.
This is a consequence of the executable flags being part of the child entries of the directory, rather than the child files themselves.
A root file has no parent directory; so there is no child entry about the root file, and therefore no executable flag for it.
Without a flag saying which, whether root files are executable or non-executable by default must be decided by convention, and the choice of Nix (and git) is to make them non-executable.

## References

Store objects can refer to both other store objects and themselves.

References are normally calculated by scanning the rooted file system objects for store paths (which we describe in the next section) referring to store objects.
For now, it suffices to say that a store path is a string encoding of a reference to a store paths, and therefore it is something that we can search for in the contents of files, and thus in store objects by searching in all their files.   
When we get to building in a future section, this process will be described in precise detail.

However, scanning for references is not mandatory.
Store objects are allowed to have official references that *don't* correspond to store paths contained in their contents,
and they are also allowed to *not* have references that *do* correspond to store paths contained in their store.
Taken together, this means there is no actual rule relating the store paths contained in the contents to the store paths deemed references.

This is why it's its necessary for correctness, and not just performance, that Nix remember the references of each store object, rather than try to recompute them on the fly by scanning their contents.
