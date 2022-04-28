# Store

A Nix store is a collection of [store objects](objects.md) with associated operations.

These store objects can hold arbitrary data, and Nix makes no distinction if they are used as build inputs, build results, or build plans.

A Nix store allows adding and retrieving store objects.
It can perform builds, that is, transform build inputs using instructions from the build plans into build outputs.
It also keeps track of *references* between data and can therefore garbage-collect unused store objects.

There exist different types of stores, which all follow this model.
Examples:
- store on the local file system
- remote store accessible via SSH
- binary cache store accessible via HTTP

Every store with a file system representation has a *store directory*, which contains that store’s objects accessible through [store paths](paths.md).
The store directory defaults to `/nix/store`, but is in principle arbitrary.

## A Rosetta stone for the Nix store.

The design of Nix is comparable to other build systems, even programming languages in general.
Here is a rough [Rosetta stone](https://en.m.wikipedia.org/wiki/Rosetta_Stone) for build system terminology.
If you are familiar with one of these columns, this might help the following sections make more sense.

generic build system | Nix | Bazel | Build Systems à la Carte | lazy programming language
-- | -- | -- | -- | --
data (build input, build result) | component | file (source, target) | value | value
build plan | derivation graph | action graph | `Tasks` | thunk
build step | derivation | rule | `Task` | thunk
build instructions | builder | (depends on action type) | `Task` | function
build | build | build | `Build` applied to arguments | evaluation
persistence layer | store | file system | `Store` | heap

(n.b. Bazel terms gotten from https://docs.bazel.build/versions/main/glossary.html.)

Plenty more could be said elaborating these comparisons.
We will save that for the end of this chapter, in the [Related Work](./related-work.md) section.
