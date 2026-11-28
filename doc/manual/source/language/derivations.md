# Derivations

The most important built-in function is `derivation`, which is used to describe a single store-layer [store derivation].
Consult the [store chapter](@docroot@/store/derivation/index.md) for what a store derivation is;
this section just concerns how to create one from the Nix language.

This builtin function takes as input an attribute set, the attributes of which specify the inputs to the process.
It outputs an attribute set, and produces a [store derivation] as a side effect of evaluation.

[store derivation]: @docroot@/glossary.md#gloss-store-derivation

There is one function per [version](@docroot@/store/derivation/versions.md) of derivation:

- [`derivation`](./derivations/v1.md) creates [version 1] derivations.
- [`builtins.derivationStrictV2`](./derivations/v2.md) creates [version 2] derivations.

[version 1]: @docroot@/store/derivation/versions.md#version-1
[version 2]: @docroot@/store/derivation/versions.md#version-2
