# Overview

Nix consists of layers that operate fairly independently.

At the top is the *command line interface*, translating from invocations of Nix executables to interactions with the underlying layers.

Below that is the *Nix language*, a [purely functional programming](https://en.m.wikipedia.org/wiki/Purely_functional_programming) language.
It is used to compose  expressions which ultimately evaluate to *derivations* — self-contained *build plans* to derive new data from referenced input data.

::: {.note}
The Nix language itself does not have a notion of *packages* or *configurations*.
As far as we are concerned here, the result of a derivation is just data.
In practice this amounts to a set of files in a file system.
:::

The command line and Nix language are what users interact with most.

Underlying everything is the *Nix store*, a mechanism to keep track of derivations, data, and references between them.
It can also *realise derivations*, that is, *execute build instructions* to produce new data.
It uses the file system as a persistence layer, and a database to keep track of references.

This chapter describes Nix starting at the bottom with the store layer, working its way up to the user-facing components described in the rest of the manual.

