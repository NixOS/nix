## Store Object

A Nix store is a collection of *store objects* with *references* between them.
A store object consists of

  - A [file system object](./file-system-object.md) as data
  - A set of [store paths](./store-path.md) as references to other store objects

Store objects are [immutable](https://en.wikipedia.org/wiki/Immutable_object):
Once created, they do not change until they are deleted.
