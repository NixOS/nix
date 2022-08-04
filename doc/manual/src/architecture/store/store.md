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

These store objects can hold arbitrary data.
Store objects can be build inputs, build results, or build tasks.

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

## Two models, abstract and concrete

The Nix store layer is the heart of Nix, the cornerstone of its design.
It comes from two basic insights: a vision for build systems in the abstract based on functional programming, and an application of the vision to conventional software for conventional operating system.
We could just present the combination of those two in the form of the current design of the Nix store, but we believe there is value introducing them separately.
This still describes how Nix works, so this section still serves as a spec, but it also demonstrates with Nix's authors believe is a good way to think* about Nix.
If one tries to learn the concrete specifics before learning the abstract model, the following text might come across as a wall of details without sufficient motivation.
Conversely, if one learns the abstract model first, many of the concrete specifics will make more sense as miscellaneous details placed in the "slots" where the abstract model expects.
The hope is that makes the material far less daunting, and helps it make sense in the mind of the reader.
