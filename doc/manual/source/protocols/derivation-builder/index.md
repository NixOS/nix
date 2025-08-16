# Derivation Builder Protocol

This is the specification of the Derivation Builder protocol, which allows derivation builders to communicate with Nix.

The protocol is defined using [Varlink](https://varlink.org/), an Interface Description Language (IDL) for defining service interfaces.

> **Warning**
>
> This protocol is currently experimental and subject to change.

## Background

As described in the [Building](@docroot@/store/building.md) documentation, there are two methods for processing derivation outputs:

1. **Traditional (post-build) processing**: After the builder process exits, Nix scans the output directories left behind, normalizes file permissions, calculates references, and registers the outputs as store objects.
   This is the original method and does not require any special communication between the builder and Nix.

2. **Concurrent processing via Varlink IPC**: The builder can communicate with Nix *during* the build to add store objects and submit outputs incrementally.
   This protocol enables more advanced use cases like pipelining and avoids the need for Nix-side hash rewriting.

This page describes the second method.

## Varlink Interface Specification

```varlink
{{#include derivation-builder.varlink}}
```

## Overview

The Derivation Builder protocol provides three main methods:

- `AddToStore`: Add a file or directory to the store with content addressing
- `AddDerivation`: Add a derivation to the store (takes a Derivation object in JSON format)
- `SubmitOutput`: Associate an output name with a store object path

The `AddToStore` and `AddDerivation` methods are both similar, creating a [file system object] inside the store given some input.
Derivations can create directories and files with `AddToStore`, but not derivations, i.e. paths that end in `.drv`.
Derivations can create additional derivations with `AddDerivation`.
However, they do not make the files appear in the builder's sandbox.

The `SubmitOutput` method links a file system object in the store created by `AddToStore` or `AddDerivation` to an output of the calling
derivation. This output name must be declared beforehand in the `outputs` field of the calling derivation.

Not all file system objects need be directly registered as an output. File system objects that are not registered to an output but included
in the [closure] of an object that is will be kept around by the garbage collector. Those that are not in a closure may be garbage collected.

This protocol enables interesting capabilities:

1. **Pipelining**: If something downstream just needs e.g. a "dev" or "headers" output, it need not block on waiting for the other outputs of the upstream derivation.

2. **Content addressing without rewriting**: Instead of requiring Nix-side rewriting, it is the responsibility of the builder to add outputs in reference order, and arrange for the store paths that resulted from earlier adds to be used in later adds. This takes what would be a very hard problem to solve in "build system space", and punts it back to userspace, where arbitrary strategies can be employed.

3. **Dynamic derivations with dependencies**: A derivation may call `AddDerivation` a number of times to create small dependency derivations
(e.g. a single C file) before creating a final derivation that combines them (e.g. a linker). Only the final needs to be registered to an output,
and it could then be executed with [dynamic derivations](@docroot@/store/derivation/index.md#dynamic).
