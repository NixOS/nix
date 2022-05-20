# Architecture

This chapter describes how Nix works.
It should help users understand why Nix behaves as it does, and it should help developers understand how to modify Nix and how to write similar tools.

## Overview

Nix consists of hierarchical [layers](https://en.m.wikipedia.org/wiki/Multitier_architecture#Layers).

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
| |                                                             | |
| +-------------------------------------------------------------+ |
+-----------------------------------------------------------------+
```

At the top is the [command line interface](../command-ref/command-ref.md), translating from invocations of Nix executables to interactions with the underlying layers.

Below that is the [Nix expression language](../expressions/expression-language.md), a [purely functional](https://en.m.wikipedia.org/wiki/Purely_functional_programming) configuration language.
It is used to compose expressions which ultimately evaluate to self-contained *build plans*, made up *build tasks* used to derive *build results* from referenced *build inputs*.

::: {.note}
The Nix language itself does not have a notion of *packages* or *configurations*.
As far as we are concerned here, the inputs and results of a derivation are just data.
In practice this amounts to a set of files in a file system.
:::

The command line and Nix language are what users interact with most.

Underlying these is the [Nix store](./store/store.md), a mechanism to keep track of build plans, data, and references between them.
It can also execute *build instructions*, captured in the build tasks, to produce new data.

