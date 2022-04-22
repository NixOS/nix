# Nix Store

A Nix store is a collection of *store objects* referred to by *store paths*.
Every store also has a "store directory path", which is a path prefix used for various purposes.

There are many types of stores, but all of them at least respect this model.
Some however offer additional functionality.

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
