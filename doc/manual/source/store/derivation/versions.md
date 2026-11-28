# Derivation versions

There are two versions of [derivations][store derivation].
They agree on what a derivation *means* --- the [parts] it consists of --- but differ in how much of it is represented directly.

The version of a derivation is part of it: two derivations of different versions are different derivations, even if they mean the same thing.
Each version has its own form in the ["ATerm" format][aterm].

## Version 1 {#version-1}

Version 1 derivations are the derivations Nix has always had.
They are the only stable version at this time.

The [inputs] of version 1 derivations are always first-order, and the store paths of their outputs are always known in advance, though why they are known depends on the [type](#versions-and-types) of the derivation.
A version 1 derivation cannot depend on a version 2 derivation: its input derivations are always version 1 derivations too.
So the store paths of all its inputs are known in advance, and it never needs to refer to one that is not, which is why version 1 derivations need no input [placeholders].
The [input addresses][input-addressing] of version 1 derivations are computed by [masking].

Their [options] are not represented directly.
Instead they are *encoded*, in one of two ways, which give the two *modes* of version 1 derivations:

- Without [structured attributes], options are encoded as specially-named [environment variables].
- With structured attributes, options are encoded as specially-named attributes in them.

Either way, options are encoded amidst freeform data that is to be passed to the [builder], "stealing" certain values by giving them special meanings in addition to the env vars and JSON file they affect.
The encoding is the source of truth: the options of a version 1 derivation cannot be varied independently of the data they are parsed from.
Not every option can be encoded in both modes; see [Options][options].

## Version 2 {#version-2}

**Experimental feature**: Version 2 derivations, and so all the kinds of derivation that require them, are experimental.

Version 2 derivations represent everything directly.
Their [options] are fields of the derivation in their own right, just like the [builder] or the [arguments][args], and so is the [*pass as file*][pass-as-file] flag of each environment variable.
Every option can be set, including those that version 1 derivations can only encode with structured attributes.
Only the richer structured-attributes-mode options are supported, in fact.

Version 2 derivations do not have structured attributes.
This is because structured attributes are a layer violation.

> **Explanation**
>
> The reason that environment variables are supported by Nix is because they are an inherent OS level concept to the processes being run.
> Environment variables are not reducible to some other concept because they are a primitive building block on Unix and Windows --- that is simply how the interface of those operating systems is defined.
> JSON and the bash interpreting it, however, are purely.

Version 2 derivations also do not need [masking].
Their [input-addressing] is instead [based on derivation resolution][resolution-based], with *static speculation*: when the store paths of all of a derivation's inputs are known in advance, it is resolved ahead of time, and its own output paths are computed and recorded in it.
Otherwise its output paths are [deferred] until it can be resolved.
As described on the [input-addressing page][semantic properties], resolution-based input-addressing is simpler than masking-based input-addressing, and has nicer properties both conceptually and practically (more cache hits, fewer pointless rebuilds).

Version 2 derivations may have [higher-order inputs] (experimental feature [`dynamic-derivations`][xp-feature-dynamic-derivations]).

## Versions and types of derivations {#versions-and-types}

The version of a derivation is independent of its [type][types of derivations], which is determined by its outputs.
Not every combination is supported yet:

| Type of derivation | Version 1 | Version 2 |
| --- | --- | --- |
| [input-addressed][input-addressing], output paths known in advance | yes | yes, by [static speculation] |
| [input-addressed][input-addressing], output paths [deferred] | no | yes |
| [fixed content-addressed] | yes | yes |
| [floating content-addressed] (experimental feature [`ca-derivations`][xp-feature-ca-derivations]) | no | yes |
| [impure][types of derivations] (experimental feature [`impure-derivations`][xp-feature-impure-derivations]) | no | yes |

So a derivation must be a version 2 derivation if it is floating content-addressed or impure, or if it has higher-order inputs.

So must any derivation that depends on a version 2 derivation, since a version 1 derivation cannot depend on one.
Version 2 derivations, on the other hand, may depend on version 1 derivations.

[options]: ./index.md#options
[inputs]: ./index.md#inputs
[builder]: ./index.md#builder
[args]: ./index.md#args
[environment variables]: ./index.md#env
[pass-as-file]: ./index.md#pass-as-file
[structured attributes]: ./index.md#structured-attrs
[higher-order inputs]: ./index.md#dynamic
[xp-feature-ca-derivations]: @docroot@/development/experimental-features.md#xp-feature-ca-derivations
[xp-feature-dynamic-derivations]: @docroot@/development/experimental-features.md#xp-feature-dynamic-derivations
[xp-feature-impure-derivations]: @docroot@/development/experimental-features.md#xp-feature-impure-derivations
[masking]: ./outputs/input-address.md#input-masked-drv
[store derivation]: ./index.md#store-derivation
[parts]: ./index.md#parts-of-a-derivation
[aterm]: @docroot@/protocols/derivation-aterm.md
[input-addressing]: ./outputs/input-address.md
[deferred]: ./outputs/input-address.md#deferred
[semantic properties]: ./outputs/input-address.md#semantic-properties
[types of derivations]: ./outputs/index.md#types-of-derivations
[fixed content-addressed]: ./outputs/content-address.md#fixed
[floating content-addressed]: ./outputs/content-address.md#floating
[resolution-based]: ./outputs/input-address.md#resolution-based
[static speculation]: ./outputs/input-address.md#static
[placeholders]: ./index.md#placeholders
