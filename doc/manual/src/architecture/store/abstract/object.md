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
