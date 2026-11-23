# Input-addressing derivation outputs

[input addressing]: #input-addressing

"Input addressing" means the address the store object by the *way it was made* rather than *what it is*.
That is to say, an input-addressed output's store path is a function not of the output itself, but of the derivation that produced it.
Even if two store paths have the same contents, if they are produced in different ways, and one is input-addressed, then they will have different store paths, and thus guaranteed to not be the same store object.

There are two ways the store path of an input-addressed output is computed, one for each [version] of derivation:
[version 1] derivations are [masked](#input-masked-drv), and [version 2] derivations are [resolved](#resolution-based).

## Version 1: masking {#input-masked-drv}

A naive implementation of an output hash computation for input-addressed outputs would be to hash the derivation hash and output together.
This clearly has the uniqueness properties we want for input-addressed outputs, but suffers from an inefficiency.
Specifically, new builds would be required whenever a change is made to a fixed-output derivation, despite having provably no differences in the inputs to the new derivation compared to what it used to be.
Concretely, this would cause a "mass rebuild" whenever any fetching detail changes, including mirror lists, certificate authority certificates, etc.

To solve this problem, we compute output hashes differently, so that certain output hashes become identical.
We do this by *masking*: before hashing, we rewrite the parts of the derivation whose differences we do not want to see.

There are two maskings, and they are independent:

- *Input masking* replaces each input derivation's store path with a hash standing for what that input *produces*, rather than for the derivation that produces it.
  This is what makes different fixed-output derivation inputs that produce the same outputs stop mattering.

- *Output masking* blanks the derivation's own output paths --- in `outputs`, and in the environment variables named after them.
  This is needed when those very paths are what we are computing; it is not wanted when the derivation is merely being hashed to stand in for an input to something else, where the paths it already has are exactly what identifies it.

A derivation with only the first applied is an *input-masked derivation*; one with both is a *fully masked derivation*.

Every non-injective function induces an equivalence relation on its domain, by saying two elements are equivalent if they map to the same value.
But the equivalence relation induced by input masking is also pleasant to characterize directly, without reference to input masking at all.
Intuitively this makes sense, because the details of how exactly inputs are hashed (which hash function, say) shouldn't matter (ignoring hash collisions and the like) --- all that matters is which derivations are deemed equivalent.
The [semantic properties](#semantic-properties) section below develops this further.

So how do we compute the [hash part](@docroot@/store/store-path.md#digest) of the output paths of an input-addressed derivation?
This is done by the function `hashInputMaskedDerivation`, shown below, which does the input masking itself; its callers do any output masking.

First, a word on inputs.
The [inputs](@docroot@/store/derivation/index.md#inputs) of version 1 derivations are always first-order:
```typescript
type ConstantPath = {
  path: StorePath;
};

type FirstOrderOutputPath = {
  drvPath: StorePath;
  output: OutputName;
};

type FirstOrderDerivingPath = ConstantPath | FirstOrderOutputPath;

type Inputs = Set<FirstOrderDerivingPath>;
```

For the algorithm below, we adopt a derivation where the two types of (first order) derived paths are partitioned into two sets, as follows:
```typescript
type Derivation = {
  // inputs: Set<FirstOrderDerivingPath>; // replaced
  inputSrcs: Set<ConstantPath>; // new instead
  inputDrvOutputs: Set<FirstOrderOutputPath>; // new instead
  // ...other fields...
};
```

```
function hashInputMaskedDerivation(drv) -> Hash:
    assert(drv.outputs are input-addressed)
    drv′ ← drv with {
        inputDrvOutputs = ⋃(
            assert(drvPath is store path)
            case hashOutputsOrInputMaskedDerivation(readDrv(drvPath)) of
                drvHash : Hash →
                    (drvHash.toBase16(), output)
                outputHashes : Map[String, Hash] →
                    (outputHashes[output].toBase16(), "out")
            | (drvPath, output) ∈ drv.inputDrvOutputs
        )
    }
    return hashSHA256(printDrv(drv′))

function hashOutputsOrInputMaskedDerivation(drv) -> Map[String, Hash] | Hash:
    if drv.outputs are fixed content-addressed:
        return {
            outputName ↦ hashSHA256(
                "fixed:out:" + ca.printMethodAlgo() +
                ":" + ca.hash.toBase16() +
                ":" + ca.makeFixedOutputPath(drv.name, outputName))
            | (outputName ↦ output) ∈ drv.outputs
            , ca = output.contentAddress
        }
    else: // drv.outputs are input-addressed, with known paths
        return hashInputMaskedDerivation(drv)
```

### `hashInputMaskedDerivation`

We replace each element in the derivation's `inputDrvOutputs` using data from a call to `hashOutputsOrInputMaskedDerivation` on the `drvPath` of that element.
When `hashOutputsOrInputMaskedDerivation` returns a single drv hash (because the input derivation in question is input-addressing), we simply swap out the `drvPath` for that hash, and keep the same output name.
When `hashOutputsOrInputMaskedDerivation` returns a map of content addresses per-output, we look up the output in question, and pair it with the output name `out`.

That rewriting is the input masking.
The resulting pseudo-derivation (with hashes instead of store paths in `inputDrvs`) is then printed (in the ["ATerm" format](@docroot@/protocols/derivation-aterm/v1.md#input-address-encoding)) and hashed, and this becomes the hash of the input-masked derivation.

When calculating output hashes, `hashInputMaskedDerivation` is called on an almost-complete input-addressing derivation, which is just missing its input-addressed outputs paths --- that is, one that is already output masked, making it fully masked.
The derivation hash is then used to calculate output paths for each output, as the [complete store path calculation](@docroot@/protocols/store-path.md#fingerprint) specifies: it is the digest of the hashed ATerm that specification calls for, under the `"output:" id` case, where `id` is the output name.
The store object's name is the derivation's name, with `-` and the output name appended --- unless the output is named `out`, in which case the derivation's name is used as-is.

Note that one derivation hash serves every output; what distinguishes their paths is only the output name, which enters twice over, as both `id` and part of the name.

Those output paths can then be substituted into the almost-complete input-addressed derivation to complete it.

### `hashOutputsOrInputMaskedDerivation`

`hashOutputsOrInputMaskedDerivation` consists of two main cases, based on whether the outputs of the derivation are to be input-addressed or content-addressed.

#### Input-addressed outputs case

In the input-addressed case, it just calls `hashInputMaskedDerivation`, and returns that derivation hash.
This makes `hashInputMaskedDerivation` and `hashOutputsOrInputMaskedDerivation` mutually-recursive.

> **Note**
>
> In this case, `hashInputMaskedDerivation` is being called on a *complete* input-addressing derivation that already has its output paths calculated: it is not output masked, so it is input-masked only, not fully masked.
> The input masking takes place anyways.

#### Fixed content-addressed outputs case

If the outputs are [fixed content-addressed](./content-address.md#fixed), then it computes a hash for each output derived from the content address of that output.

The content addresses are specified in advance, so this always just works: the algorithm is never stuck waiting for an input to be built.
Version 1 derivations never have [floating content-addressed](./content-address.md#floating) inputs, whose content addresses are not known in advance, because they only ever depend on version 1 derivations, which have no floating content-addressed outputs.

### Performance

The recursion in the algorithm is potentially inefficient:
it could call itself once for each path by which a subderivation can be reached, i.e., `O(V^k)` times for a derivation graph with `V` derivations and with out-degree of at most `k`.
In the actual implementation, [memoisation](https://en.wikipedia.org/wiki/Memoization) is used to reduce this cost to be proportional to the total number of `inputDrvOutputs` encountered.

### Semantic properties {#semantic-properties}

*See [this chapter's appendix](@docroot@/store/math-notation.md) on grammar and metavariable conventions.*

Masking is, mathematically, a quotient, in reference to quotient types or sets: what it discards is exactly what we want two derivations to be allowed to differ in.
Accordingly, `hashInputMaskedDerivation` partitions input-addressing derivations into equivalence classes: every derivation in that equivalence class is mapped to the same derivation hash.
We can characterize this equivalence relation directly, by working bottom up.

We start by defining an equivalence relation on first-order output deriving paths that refer content-addressed derivation outputs. Two such paths are equivalent if they refer to the same store object:

\\[
\\begin{prooftree}
\\AxiomC{$d\_1$ is content-addressing}
\\AxiomC{$d\_2$ is content-addressing}
\\AxiomC{$
  {}^\*(\text{path}(d\_1), o\_1)
  \=
  {}^\*(\text{path}(d\_2), o\_2)
$}
\\TrinaryInfC{$(\text{path}(d\_1), o\_1) \\,\\sim_{\\mathrm{CA}}\\, (d\_2, o\_2)$}
\\end{prooftree}
\\]

where \\({}^*(s, o)\\) denotes the store object that the output deriving path refers to.

We will also need the following construction to lift any equivalence relation on \\(X\\) to an equivalence relation on (finite) sets of \\(X\\) (in short, \\(\\mathcal{P}(X)\\)):

\\[
\\begin{prooftree}
\\AxiomC{$\\forall a \\in A. \\exists b \\in B. a \\,\\sim\_X\\, b$}
\\AxiomC{$\\forall b \\in B. \\exists a \\in A. b \\,\\sim\_X\\, a$}
\\BinaryInfC{$A \\,\\sim_{\\mathcal{P}(X)}\\, B$}
\\end{prooftree}
\\]

Now we can define the equivalence relation \\(\\sim_\\mathrm{IA}\\) on input-addressed derivation outputs. Two input-addressed outputs are equivalent if their derivations are equivalent (via the yet-to-be-defined \\(\\sim_{\\mathrm{IADrv}}\\) relation) and their output names are the same:

\\[
\\begin{prooftree}
\\AxiomC{$d\_1$ is input-addressing}
\\AxiomC{$d\_2$ is input-addressing}
\\AxiomC{$d\_1 \\,\\sim_{\\mathrm{IADrv}}\\, d\_2$}
\\AxiomC{$o\_1 = o\_2$}
\\QuaternaryInfC{$(\text{path}(d\_1), o\_1) \\,\\sim_{\\mathrm{IA}}\\, (\text{path}(d\_2), o\_2)$}
\\end{prooftree}
\\]

And now we can define \\(\\sim_{\\mathrm{IADrv}}\\).
Two input-addressed derivations are equivalent if their content-addressed inputs are equivalent, their input-addressed inputs are also equivalent, and they are otherwise equal:

<!-- cheating a bit with the semantics to get a good layout that fits on the page -->

\\[
\\begin{prooftree}
\\alwaysNoLine
\\AxiomC{$
  \\mathrm{caInputs}(d\_1)
  \\,\\sim_{\\mathcal{P}(\\mathrm{CA})}\\,
  \\mathrm{caInputs}(d\_2)
$}
\\AxiomC{$
  \\mathrm{iaInputs}(d\_1)
  \\,\\sim_{\\mathcal{P}(\\mathrm{IA})}\\,
  \\mathrm{iaInputs}(d\_2)
$}
\\BinaryInfC{$
  d\_1\left[\\mathrm{inputDrvOutputs} := \\{\\}\right]
  \=
  d\_2\left[\\mathrm{inputDrvOutputs} := \\{\\}\right]
$}
\\alwaysSingleLine
\\UnaryInfC{$d\_1 \\,\\sim_{\\mathrm{IADrv}}\\, d\_2$}
\\end{prooftree}
\\]

where \\(\\mathrm{caInputs}(d)\\) returns the content-addressed inputs of \\(d\\) and \\(\\mathrm{iaInputs}(d)\\) returns the input-addressed inputs.

> **Note**
>
> An astute reader might notice that that nowhere does `inputSrcs` enter into these definitions.
> That means that replacing an input derivation with its outputs directly added to `inputSrcs` always results in a derivation in a different equivalence class, despite the resulting input closure (as would be mounted in the store at build time) being the same.
> [Issue #9259](https://github.com/NixOS/nix/issues/9259) is about creating a coarser equivalence relation to address this.
>
> \\(\\sim_\mathrm{Drv}\\) from [derivation resolution](@docroot@/store/resolution.md) is such an equivalence relation.
> It is coarser than this one: any two derivations which are "'hash input-masked derivation'-equivalent" (\\(\\sim_\mathrm{IADrv}\\)) are also "resolution-equivalent" (\\(\\sim_\mathrm{Drv}\\)).
> It also relates derivations whose `inputDrvOutputs` have been rewritten into `inputSrcs`.
>
> Input-addressing downstream of content addressing, as described above, anticipates this.
> Since a content-addressed input contributes only its content address, and not the derivation that produced it, derivations differing only in *how* an identical content-addressed input was produced are already \\(\\sim_\mathrm{IADrv}\\)-equivalent.
> And since a deferred derivation is completely resolved before it is input-addressed, its input-addressed `inputDrvOutputs` are normalized to `inputSrcs` as well --- so derivations differing only in how an identical *input-addressed* input was produced are \\(\\sim_\mathrm{IADrv}\\)-equivalent too.
> That is the same "identify an input by what it is, rather than by how it was made" move as \\(\\sim_\mathrm{Drv}\\).
> The difference is that \\(\\sim_\mathrm{Drv}\\) makes it unconditionally, whereas here it falls out for content-addressed inputs always, and for the rest only where deferral forced a resolution first.

## Version 2: resolution {#resolution-based}

[Version 2] derivations are input-addressed by [resolving][resolution] them, rather than by masking.

A resolved derivation names each of its inputs by the store path it denotes, rather than by the derivation that produced it.
That already identifies each input by *what it is* rather than *how it was made* --- the very thing masking exists to achieve --- so no masking of inputs is needed.
In particular, a fixed-output input is named by its output path, which depends only on its content address, so changing where it is fetched from changes nothing downstream.

The output paths of a version 2 input-addressed derivation are computed from its resolved form:

1. The derivation is resolved: each input deriving path is replaced by the store path it denotes.

2. Its own output paths are left empty.
   This is all the masking of outputs that is needed, because a version 2 derivation only ever refers to its own outputs by [reference][references], never by store path.

3. The result is serialized in the [version 2 "ATerm" format][aterm v2] and hashed with SHA-256.

4. That hash is used, as for version 1 derivations, as the digest of the [complete store path calculation](@docroot@/protocols/store-path.md#fingerprint) under the `"output:" id` case, where `id` is the output name; and the store object's name is the derivation's name, with `-` and the output name appended unless the output is named `out`.

### Static speculation {#static}

Resolving a derivation requires knowing the store paths of its inputs.
An output's store path is *static* if it is known without building anything:

- the output of a [fixed content-addressed](./content-address.md#fixed) derivation, which is determined by its content address;
- the output of a version 1 input-addressed derivation, which is always recorded in it;
- the output of a version 2 input-addressed derivation, if it is recorded in it, as described next.

If every input of a version 2 input-addressed derivation has a static store path, resolution needs no building, so it is done ahead of time, when the derivation is created: the derivation is *pre-resolved*, its output paths are computed as above, and they are recorded in the derivation.
Its outputs then have static store paths in turn, so this propagates down the derivation graph exactly as far as it can.

The recorded paths are redundant --- they are a function of the rest of the derivation --- but they must be recorded whenever they can be, and must agree with what pre-resolution computes, which is checked when a derivation is read.
Otherwise the same derivation would have two encodings, with and without them, and so two store paths.

> **Note**
>
> Because every version 2 input-addressed derivation records its own output paths whenever they can be known, computing the output paths of a derivation only requires the recorded paths of its direct inputs.
> The derivations memoize themselves:
> unlike [masking](#input-masked-drv), which recurses through the whole input graph and needs a separate cache to be [efficient](#performance), nothing is recursed into and nothing need be cached.

### Deferred outputs {#deferred}

If some input does not have a static store path --- it is the output of a [floating content-addressed](./content-address.md#floating) or [impure](./index.md#types-of-derivations) derivation, or of a version 2 input-addressed derivation whose own outputs are deferred --- the output paths cannot be computed ahead of time.
They are *deferred*: no store path is recorded, and the paths are computed in exactly the same way once the derivation is actually resolved, after the inputs in question have been built.

A deferred output does not use a third kind of addressing alongside input- and content-addressing.
It is input-addressed; it just does not know its path yet.
This costs nothing, because a derivation's own store path is [content-addressed](@docroot@/store/store-object/content-address.md#method-text) on the bytes of its serialization, which do not depend on its output paths being known.

Deferral is resolved by building, not by revisiting the same derivation.
The resolved derivation is a *different* derivation, with its own store path --- necessarily so, since it has different bytes.
It has no input derivations left, so its output paths are static, and it is what actually gets built.


[version 1]: ../versions.md#version-1
[version 2]: ../versions.md#version-2
[version]: ../versions.md
[resolution]: @docroot@/store/resolution.md
[references]: @docroot@/protocols/derivation-aterm/v2.md#interpolated
[aterm v2]: @docroot@/protocols/derivation-aterm/v2.md
