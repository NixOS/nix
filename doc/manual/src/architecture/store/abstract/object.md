# Abstract store object

Nix organizes the data it manages into *store objects*.
An abstract store object is a black box that can reference other store objects.

In pseudo-code:

```idris
data StoreObject
data StoreObjectRef

getReferences : StoreObject -> Set StoreObjectRef
```

Store objects are [immutable][immutable-object]: once created, they do not change until they are deleted.

## Reference

A store object reference is an [opaque][opaque-data-type], [unique identifier][unique-identifier]:
The only way to obtain references is by adding store objects.
A reference will always point to exactly one store object.

## Closure

Nix stores have the *closure property*: for each store object in the store, all the store objects it references must also be in the store.

This invariant implies that store objects and their references form a graph.
Furthermore, this invariant combined with the immutability of store objects implies that the graph is acyclic:
to avoid both dangling references and mutation, any referenced objects must exist prior to the creation of objects.
By induction then, store objects with references must be "built up** from store objects without them, and this makes the graph acyclic.

Adding, building, copying and deleting store objects must be done in a way that obeys this property:

- A newly added store object cannot have references, unless it is a build task.

- Build results must only refer to store objects in the closure of the build inputs.

  Building a store object will add appropriate references, according to the build task.
  These references can only come from declared build inputs.

- Store objects being copied must refer to objects already in the destination store.

  Recursive copying must either proceed in dependency order or be atomic.

- We can only safely delete store objects which are not reachable from any reference still in use.

  Garbage collection will delete those store objects that cannot be reached from any reference in use.

  <!-- more details in section on garbage collection, link to it once it exists -->

[immutable-object]: https://en.m.wikipedia.org/wiki/Immutable_object
[opaque-data-type]: https://en.m.wikipedia.org/wiki/Opaque_data_type
[unique-identifier]: https://en.m.wikipedia.org/wiki/Unique_identifier
