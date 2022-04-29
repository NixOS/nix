# Store Object

Nix organizes the data it manages into *store objects*.
A store object is the pair of

  - a [file system object](#file-system-object)
  - a set of [references](#reference) to store objects.

We call a store object's outermost file system object the *root*.

```haskell
data StoreOject = StoreObject {
  root       :: FileSystemObject
, references :: Set StoreObject
}
```

## File system object {#file-system-object}

The Nix store uses a simple file system model.

Every file system object is one of the following:
 - File: an executable flag, and arbitrary data for contents
 - Directory: mapping of names to child file system objects
 - [Symbolic link](https://en.m.wikipedia.org/wiki/Symbolic_link): may point anywhere.

```haskell
data FileSystemObject
  = File { isExecutable :: Bool, contents :: Bytes }
  | Directory { entries ::  Map FileName FileSystemObject }
  | SymLink { target :: Path }
```

A bare file or symlink can be a root file system object.

Symlinks pointing outside of their own root, or to a store object without a matching reference, are allowed, but might not function as intended.

## Reference {#reference}

A store object can reference other store objects.

Nix stores have the *closure property*: for each store object in the store, all the store objects it references must also be in the store.

Building, copying and deleting store objects must be done in a way that obeys this property:

- Build results must only refer to store objects in the closure of the build inputs.

- Store objects being copied must refer to objects already in the destination store.
  Recursive copying must either proceed in dependency order or be atomic.

- We can only safely delete unreferenced objects.

### Reference scanning

While references could be arbitrary paths, Nix requires them to be store paths to ensure correctness.
Anything outside a given store is not under control of Nix, and therefore cannot be guaranteed to be present when needed.

However, having references match store paths in files is not enforced by the data model:
Store objects could have excess or incomplete references with respect to store paths found in their file contents.

Scanning files therefore allows reliably capturing run time dependencies without declaring them explicitly.
Doing it at build time and persisting references in the store object avoids repeating this time-consuming operation.

