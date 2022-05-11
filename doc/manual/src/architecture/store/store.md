# Store

A Nix store is a collection of *store objects*.

Store objects can hold arbitrary *data* and *references* to one another.
Nix makes no distinction if they are used as build inputs, build results, or build tasks.

```haskell
data Store = Set StoreObject

data StoreObject = StoreObject {
  data       :: Data
, references :: Set Reference
}
```

A Nix store can *add*, *retrieve*, and *delete* store objects.

It can *perform builds*, that is, create new store objects by transforming build inputs, using instructions from the build tasks, into build outputs.

As it keeps track of references, it can [garbage-collect](https://en.m.wikipedia.org/wiki/Garbage_collection_(computer_science)) unused store objects.

```haskell
add    :: Store -> Data -> (Store, Reference)
get    :: Store -> Reference -> StoreObject
delete :: Store -> Reference -> Store

build  :: Store -> Reference -> Maybe (Store, Reference)

collectGarbage :: Store -> Store
```

Store objects are [immutable](https://en.m.wikipedia.org/wiki/Immutable_object): once created, they do not change until they are deleted.

References are [opaque](https://en.m.wikipedia.org/wiki/Opaque_data_type), [unique identifiers](https://en.m.wikipedia.org/wiki/Unique_identifier):
The only way to obtain references is by adding or building store objects.
A reference will always point to exactly one store object.

An added store object cannot have references, unless it is a build task.

Building a store object will add appropriate references, according to provided build instructions.
These references can only come from declared build inputs, and are not known to build instructions a priori.

```haskell
data Data = Data | Task BuildTask

data BuildTask = BuildTask {
  instructions ::  Reference
, inputs       :: [Reference]
}
```

A store object cannot be deleted as long as it is reachable from a reference still in use.
Garbage collection will delete all store objects that cannot be reached from any reference in use.

<!-- more details in section on garbage collection, link to it once it exists -->

## Files and Processes

Nix provides a mapping between its store model and the [Unix paradigm](https://en.m.wikipedia.org/wiki/Everything_is_a_file) on the interplay of [files and processes](https://en.m.wikipedia.org/wiki/File_descriptor).

Nix encodes immutable store objects and opaque identifiers as file system primitives: files, directories, and paths.
That allows processes to resolve references contained in files and thus access the contents of store objects.

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

Store objects are therefore implemented as the pair of

  - a *file system object* for data
  - a set of *store paths* for references.

There exist different types of stores, which all follow this model.
Examples:
- store on the local file system
- remote store accessible via SSH
- binary cache store accessible via HTTP

Every store ultimately has to make store objects accessible to processes through the file system.

## A [Rosetta stone][rosetta-stone] for build system terminology

The Nix store's design is comparable to other build systems.
Usage of terms is, for historic reasons, not entirely consistent within the Nix ecosystem, and still subject to slow change.

The following translation table points out similarities and equivalent terms, to help clarify their meaning and inform consistent use in the future.

| generic build system             | Nix              | [Bazel][bazel]                                                       | [Build Systems à la Carte][bsalc] | programming language     |
| -------------------------------- | ---------------- | -------------------------------------------------------------------- | --------------------------------- | ------------------------ |
| data (build input, build result) | store object     | [artifact][bazel-artifact]                                           | value                             | value                    |
| build instructions               | builder          | ([depends on action type][bazel-actions])                            | function                          | function                 |
| build task                       | derivation       | [action][bazel-action]                                               | `Task`                            | [thunk][thunk]           |
| build plan                       | derivation graph | [action graph][bazel-action-graph], [build graph][bazel-build-graph] | `Tasks`                           | [call graph][call-graph] |
| build                            | build            | build                                                                | application of `Build`            | evaluation               |
| persistence layer                | store            | [action cache][bazel-action-cache]                                   | `Store`                           | heap                     |

All of these systems share features of [declarative programming][declarative-programming] languages, a key insight first put forward by Eelco Dolstra et al. in [Imposing a Memory Management Discipline on Software Deployment][immdsd] (2004), elaborated in his PhD thesis [The Purely Functional Software Deployment Model][phd-thesis] (2006), and further refined by Andrey Mokhov et al. in [Build Systems à la Carte][bsalc] (2018).

[rosetta-stone]: https://en.m.wikipedia.org/wiki/Rosetta_Stone
[bazel]: https://bazel.build/start/bazel-intro
[bazel-artifact]: https://bazel.build/reference/glossary#artifact
[bazel-actions]: https://docs.bazel.build/versions/main/skylark/lib/actions.html
[bazel-action]: https://bazel.build/reference/glossary#action
[bazel-action-graph]: https://bazel.build/reference/glossary#action-graph
[bazel-build-graph]: https://bazel.build/reference/glossary#build-graph
[bazel-action-cache]: https://bazel.build/reference/glossary#action-cache
[thunk]: https://en.m.wikipedia.org/wiki/Thunk
[call-graph]: https://en.m.wikipedia.org/wiki/Call_graph
[declarative-programming]: https://en.m.wikipedia.org/wiki/Declarative_programming
[immdsd]: https://edolstra.github.io/pubs/immdsd-icse2004-final.pdf
[phd-thesis]: https://edolstra.github.io/pubs/phd-thesis.pdf
[bsalc]: https://www.microsoft.com/en-us/research/uploads/prod/2018/03/build-systems.pdf
