# Store

A Nix store is a collection of [store objects](objects.md) with associated operations.

These store objects can hold arbitrary data, and Nix makes no distinction if they are used as build inputs, build results, or build tasks.

A Nix store allows adding, retrieving, and deleting store objects.
It can perform builds, that is, transform build inputs using instructions from the build tasks into build outputs.
It also keeps track of *references* between data and can therefore garbage-collect unused store objects.

There exist different types of stores, which all follow this model.
Examples:
- store on the local file system
- remote store accessible via SSH
- binary cache store accessible via HTTP

Every store with a file system representation has a *store directory*, which contains that store’s objects accessible through [store paths](paths.md).
The store directory defaults to `/nix/store`, but is in principle arbitrary.

## A [Rosetta stone][rosetta-stone] for build system terminology

The Nix store's design is comparable to other build systems.
Usage of terms is, for historic reasons, not entirely consistent within the Nix ecosystem, and still subject to slow change.

The following translation table points out similarities and equivalent terms, to help clarify their meaning and inform consistent use in the future.

generic build system | Nix | [Bazel][bazel] | [Build Systems à la Carte][bsalc] | programming language
-- | -- | -- | -- | --
data (build input, build result) | store object | [artifact][bazel-artifact] | value | value
build instructions | builder | ([depends on action type][bazel-actions]) | function | function
build task | derivation | [action][bazel-action] | `Task` | [thunk][thunk]
build plan | derivation graph | [action graph][bazel-action-graph], [build graph][bazel-build-graph] | `Tasks` | [call graph][call-graph]
build | build | build | application of `Build` | evaluation
persistence layer | store | [action cache][bazel-action-cache] | `Store` | heap

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
