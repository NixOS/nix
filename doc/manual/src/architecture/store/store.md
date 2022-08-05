# Store

A Nix store is a collection of *store objects* with references between them.
It supports operations to manipulate that collection.

The following concept map is a graphical outline of this chapter.
Arrows indicate suggested reading order.

```
                      ,--------------[ store ]----------------,
                      |                  |                    |
                      v                  v                    v
               [ store object ]     [ closure ]--,      [ operations ]
                      |               |   |      |        |        |
                      v               |   |      v        v        |
           [ files and processes ]    |   | [ garbage collection ] |
               /          \           |   |                        |
              v            v          |   v                        v
[ file system object ] [ store path ] | [ derivation ]--->[ building ]
                  |        ^      |   |                         |
                  v        |      v   v                         |
             [ digest ]----' [ reference scanning ]<------------'
              /      \
             v        v
[ input addressing ] [ content addressing ]
```

## Store Object

A store object can hold

- arbitrary *data*
- *references* to other store objects.

Store objects can be build inputs, build results, or build tasks.

Store objects are [immutable][immutable-object]: once created, they do not change until they are deleted.

[immutable-object]: https://en.m.wikipedia.org/wiki/Immutable_object

## Reference

A store object reference is an [opaque][opaque-data-type], [unique identifier][unique-identifier]:
The only way to obtain references is by adding or building store objects.
A reference will always point to exactly one store object.

## Operations

A Nix store can *add*, *retrieve*, and *delete* store objects.

                [ data ]
                    |
                    V
    [ store ] ---> add ----> [ store' ]
                    |
                    V
              [ reference ]

<!-- -->

              [ reference ]
                    |
                    V
    [ store ] ---> get
                    |
                    V
             [ store object ]

<!-- -->

              [ reference ]
                    |
                    V
    [ store ] --> delete --> [ store' ]


It can *perform builds*, that is, create new store objects by transforming build inputs into build outputs, using instructions from the build tasks.


              [ reference ]
                    |
                    V
    [ store ] --> build --(maybe)--> [ store' ]
                             |
                             V
                       [ reference ]


As it keeps track of references, it can [garbage-collect][garbage-collection] unused store objects.


    [ store ] --> collect garbage --> [ store' ]

[garbage-collection]: https://en.m.wikipedia.org/wiki/Garbage_collection_(computer_science)

## Files and Processes

Nix maps between its store model and the [Unix paradigm][unix-paradigm] of [files and processes][file-descriptor], by encoding immutable store objects and opaque identifiers as file system primitives: files and directories, and paths.
That allows processes to resolve references contained in files and thus access the contents of store objects.

Store objects are therefore implemented as the pair of

  - a [file system object](fso.md) for data
  - a set of [store paths](path.md) for references.

[unix-paradigm]: https://en.m.wikipedia.org/wiki/Everything_is_a_file
[file-descriptor]: https://en.m.wikipedia.org/wiki/File_descriptor

The following diagram shows a radical simplification of how Nix interacts with the operating system:
It uses files as build inputs, and build outputs are files again.
On the operating system, files can be run as processes, which in turn operate on files.
A build function also amounts to an operating system process (not depicted).

```
+-----------------------------------------------------------------+
| Nix                                                             |
|                  [ commmand line interface ]------,             |
|                               |                   |             |
|                           evaluates               |             |
|                               |                manages          |
|                               V                   |             |
|                  [ configuration language  ]      |             |
|                               |                   |             |
| +-----------------------------|-------------------V-----------+ |
| | store                  evaluates to                         | |
| |                             |                               | |
| |             referenced by   V       builds                  | |
| |  [ build input ] ---> [ build plan ] ---> [ build result ]  | |
| |         ^                                        |          | |
| +---------|----------------------------------------|----------+ |
+-----------|----------------------------------------|------------+
            |                                        |
    file system object                          store path
            |                                        |
+-----------|----------------------------------------|------------+
| operating system        +------------+             |            |
|           '------------ |            | <-----------'            |
|                         |    file    |                          |
|                     ,-- |            | <-,                      |
|                     |   +------------+   |                      |
|          execute as |                    | read, write, execute |
|                     |   +------------+   |                      |
|                     '-> |  process   | --'                      |
|                         +------------+                          |
+-----------------------------------------------------------------+
```

There exist different types of stores, which all follow this model.
Examples:
- store on the local file system
- remote store accessible via SSH
- binary cache store accessible via HTTP

To make store objects accessible to processes, stores ultimately have to expose store objects through the file system.

