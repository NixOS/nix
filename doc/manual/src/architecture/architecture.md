# Architecture

This chapter describes how Nix works.
It should help users understand why Nix behaves as it does, and it should help developers understand how to modify Nix and how to write similar tools.

## Overview

Nix consists of [hierarchical layers].

[hierarchical layers]: https://en.m.wikipedia.org/wiki/Multitier_architecture#Layers

```
+---------------------------------------------------------------+
| Nix                                                           |
|                 [ commmand line interface ]------,            |
|                              |                   |            |
|                          evaluates               |            |
|                              |                manages         |
|                              V                   |            |
|                 [ configuration language  ]      |            |
|                              |                   |            |
| +----------------------------|-------------------V----------+ |
| | store                 evaluates to                        | |
| |                            |                              | |
| |            referenced by   V       builds                 | |
| | [ build input ] ---> [ build plan ] ---> [ build result ] | |
| |                                                           | |
| +-----------------------------------------------------------+ |
+---------------------------------------------------------------+
```

At the top is the [command line interface](../command-ref/command-ref.md), translating from invocations of Nix executables to interactions with the underlying layers.

Below that is the [Nix language](../language/index.md), a [purely functional] configuration language.
It is used to compose expressions which ultimately evaluate to self-contained *build plans*, used to derive *build results* from referenced *build inputs*.

[purely functional]: https://en.m.wikipedia.org/wiki/Purely_functional_programming

The command line interface and the Nix language are what users interact with most.

> **Note**
> The Nix language itself does not have a notion of *packages* or *configurations*.
> As far as we are concerned here, the inputs and results of a build plan are just data.

Underlying the command line interface and the Nix language is the [Nix store](../glossary.md#gloss-store), a mechanism to keep track of build plans, data, and references between them.
It can also execute build plans to produce new data.

A build plan is a series of *build tasks*.
Each build task has a special build input, which is used as *build instructions*.
The result of a build task can be input to another build task.

> **Important**
> A build task in Nix is called [derivation](../glossary#gloss-derivation).

```
+----------------------------------------------------------------------------------+
| store           ..............................................                   |
|                 : build plan                                 :                   |
|                 :                                            :                   |
| [ build input ]---instructions-,                             :                   |
|                 :              |                             :                   |
|                 :              v                             :                   |
| [ build input ]--------->[ build task ]-instructions-,       :                   |
|                 :                                    |       :                   |
|                 :                                    v       :                   |
| [ build input ]---instructions-,              [ build task ]--->[ build result ] |
|                 :              |                     ^       :                   |
|                 :              v                     |       :                   |
| [ build input ]--------->[ build task ]--------------'       :                   |
|                 :              ^                             :                   |
|                 :              |                             :                   |
| [ build input ]----------------'                             :                   |
|                 :                                            :                   |
|                 :............................................:                   |
+----------------------------------------------------------------------------------+
```

