# Store

A Nix store is a collection of *store objects* which refer to one another.

These store objects can hold arbitrary data, and Nix makes no distinction if they are used as build inputs, build results, or build tasks.

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
    [ store ] --> build
                       \
                      (maybe) --> [ store' ]
                         |
                         V
                   [ reference ]


As it keeps track of references, it can [garbage-collect][garbage-collection] unused store objects.


    [ store ] --> collect garbage --> [ store' ]

[garbage-collection]: https://en.m.wikipedia.org/wiki/Garbage_collection_(computer_science)

## Two models, abstract and concrete

The Nix store layer is the heart of Nix, the cornerstone of its design.
It comes from two basic insights: a vision for build systems in the abstract based on functional programming, and an application of the vision to conventional software for conventional operating system.
We could just present the combination of those two in the form of the current design of the Nix store, but we believe there is value introducing them separately.
This still describes how Nix works, so this section still serves as a spec, but it also demonstrates with Nix's authors believe is a good way to think* about Nix.
If one tries to learn the concrete specifics before learning the abstract model, the following text might come across as a wall of details without sufficient motivation.
Conversely, if one learns the abstract model first, many of the concrete specifics will make more sense as miscellaneous details placed in the "slots" where the abstract model expects.
The hope is that makes the material far less daunting, and helps it make sense in the mind of the reader.

## A [Rosetta stone][rosetta-stone] for build system terminology

Nix is far from the other project to try to envision build systems abstractly, and indeed the design of the Nix store is comparable to other work.
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
