# Closure

Nix stores ensure [referential integrity][referential-integrity]: for each store object in the store, all the store objects it references must also be in the store.

The set of all store objects reachable by following references from a given initial set of store objects is called a *closure*.

Adding, building, copying and deleting store objects must be done in a way that preserves referential integrity:

- A newly added store object cannot have references, unless it is a build task.

- Build results must only refer to store objects in the closure of the build inputs.

  Building a store object will add appropriate references, according to the build task.

- Store objects being copied must refer to objects already in the destination store.

  Recursive copying must either proceed in dependency order or be atomic.

- We can only safely delete store objects which are not reachable from any reference still in use.

  <!-- more details in section on garbage collection, link to it once it exists -->

[referential-integrity]: https://en.m.wikipedia.org/wiki/Referential_integrity
[garbage-collection]: https://en.m.wikipedia.org/wiki/Garbage_collection_(computer_science)
[immutable-object]: https://en.m.wikipedia.org/wiki/Immutable_object
[opaque-data-type]: https://en.m.wikipedia.org/wiki/Opaque_data_type
[unique-identifier]: https://en.m.wikipedia.org/wiki/Unique_identifier


