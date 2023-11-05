# Store Object

A Nix store is a collection of *store objects* with *references* between them.
Concrete Store objects comes in a few variations of Nix, but the basic model of a store object is the triple of

  - A [file system object](./file-system-object.md) as data
  - A set of [store paths](./path.md) as references to other store objects
  - a name

Store objects are [immutable](https://en.wikipedia.org/wiki/Immutable_object):
Once created, they do not change until they are deleted.

While the abstract model allows references and other data to be intermingled, the concrete model strictly separates them in this way.

```idris
data StoreObjectRef

record StoreObject where
  root       : FileSystemObject
  references : Set StoreObjectRef
  name       : String

getReferences so = so.references
```

We call a store object's outermost file system object its *root*.

The string name is subject to this condition (taken from an error message in the implementation):

> names are alphanumeric and can include the symbols +-._?= and must not begin with a period.

## References

The references independence from the file system reflects the fact that Unix has no built-in concept of a file system reference that fit's Nix's needs:

- Symbolic links are allowed to dangle or be acyclic.
  Nix does not impose those restrictions for compatibility with existing software they may use symlinks in these ways.

- Hard links are only permitted to files to trivially avoid cycles (since files do not have children/outgoing links.)
  Local stores allow hard links as a space optimization (which a safe one since store objects are immutable) but has no semantic content.

Exactly what form the references take depends on the type of store object.
We will provide more details in the following sections, leaving "store path reference" abstract for now.
