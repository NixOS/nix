# Input-addressing derivation outputs

[input addressing]: #input-addressing

"Input addressing" means the address the store object by the *way it was made* rather than *what it is*.
That is to say, an input-addressed output's store path is a function not of the output itself, but of the derivation that produced it.
Even if two store paths have the same contents, if they are produced in different ways, and one is input-addressed, then they will have different store paths, and thus guaranteed to not be the same store object.

## Modulo content addressed derivation outputs {#hash-quotient-drv}

A naive implementation of an output hash computation for input-addressed outputs would be to hash the derivation hash and output together.
This clearly has the uniqueness properties we want for input-addressed outputs, but suffers from an inefficiency.
Specifically, new builds would be required whenever a change is made to a fixed-output derivation, despite having provably no differences in the inputs to the new derivation compared to what it used to be.
Concretely, this would cause a "mass rebuild" whenever any fetching detail changes, including mirror lists, certificate authority certificates, etc.

To solve this problem, we compute output hashes differently, so that certain output hashes become identical.
We call this concept quotient hashing, in reference to quotient types or sets.

So how do we compute the [hash part](@docroot@/store/store-path.md#digest) of the output paths of an input-addressed derivation?
This is done by the function `hashQuotientDerivation`, shown below.

First, a word on inputs.
`hashQuotientDerivation` is only defined on derivations whose [inputs](@docroot@/store/derivation/index.md#inputs) take the first-order form:
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

In the [currently-experimental][xp-feature-dynamic-derivations] higher-order case where outputs of outputs are allowed as [deriving paths][deriving-path] and thus derivation inputs, derivations using that generalization are not valid arguments to this function.
Those derivations must be (partially) [resolved](@docroot@/store/resolution.md) enough first, to the point where no such higher-order inputs remain.
Then, and only then, can input addresses be assigned.

```
function hashQuotientDerivation(drv) -> Hash:
    assert(drv.outputs are input-addressed)
    drv′ ← drv with {
        inputDrvOutputs = ⋃(
            assert(drvPath is store path)
            case hashOutputsOrQuotientDerivation(readDrv(drvPath)) of
                drvHash : Hash →
                    (drvHash.toBase16(), output)
                outputHashes : Map[String, Hash] →
                    (outputHashes[output].toBase16(), "out")
            | (drvPath, output) ∈ drv.inputDrvOutputs
        )
    }
    return hashSHA256(printDrv(drv′))

function hashOutputsOrQuotientDerivation(drv) -> Map[String, Hash] | Hash:
    if drv.outputs are content-addressed:
        return {
            outputName ↦ hashSHA256(
                "fixed:out:" + ca.printMethodAlgo() +
                ":" + ca.hash.toBase16() +
                ":" + ca.makeFixedOutputPath(drv.name, outputName))
            | (outputName ↦ output) ∈ drv.outputs
            , ca = output.contentAddress // or get from build trace if floating
        }
    else: // drv.outputs are input-addressed
        return hashQuotientDerivation(drv)
```

### `hashQuotientDerivation`

We replace each element in the derivation's `inputDrvOutputs` using data from a call to `hashOutputsOrQuotientDerivation` on the `drvPath` of that element.
When `hashOutputsOrQuotientDerivation` returns a single drv hash (because the input derivation in question is input-addressing), we simply swap out the `drvPath` for that hash, and keep the same output name.
When `hashOutputsOrQuotientDerivation` returns a map of content addresses per-output, we look up the output in question, and pair it with the output name `out`.

The resulting pseudo-derivation (with hashes instead of store paths in `inputDrvs`) is then printed (in the ["ATerm" format](@docroot@/protocols/derivation-aterm.md#input-address-encoding)) and hashed, and this becomes the hash of the "quotient derivation".

When calculating output hashes, `hashQuotientDerivation` is called on an almost-complete input-addressing derivation, which is just missing its input-addressed outputs paths.
The derivation hash is then used to calculate output paths for each output, as the [complete store path calculation](@docroot@/protocols/store-path.md#fingerprint) specifies: it is the digest of the hashed ATerm that specification calls for, under the `"output:" id` case, where `id` is the output name.
The store object's name is the derivation's name, with `-` and the output name appended --- unless the output is named `out`, in which case the derivation's name is used as-is.

Note that one derivation hash serves every output; what distinguishes their paths is only the output name, which enters twice over, as both `id` and part of the name.

Those output paths can then be substituted into the almost-complete input-addressed derivation to complete it.

> **Note**
>
> There may be an unintentional deviation from specification currently implemented in the `(outputHashes[output].toBase16(), "out")` case.
> This is not fatal because the deviation would only apply for content-addressing derivations with more than one output, and that only occurs in the floating case, which is [experimental][xp-feature-ca-derivations].
> Once this bug is fixed, this note will be removed.

### `hashOutputsOrQuotientDerivation`

How does `hashOutputsOrQuotientDerivation` in turn work?
It consists of two main cases, based on whether the outputs of the derivation are to be input-addressed or content-addressed.

#### Input-addressed outputs case

In the input-addressed case, it just calls `hashQuotientDerivation`, and returns that derivation hash.
This makes `hashQuotientDerivation` and `hashOutputsOrQuotientDerivation` mutually-recursive.

> **Note**
>
> In this case, `hashQuotientDerivation` is being called on a *complete* input-addressing derivation that already has its output paths calculated.
> The `inputDrvs` substitution takes place anyways.

#### Content-addressed outputs case

If the outputs are [content-addressed](./content-address.md), then it computes a hash for each output derived from the content-address of that output.

> **Note**
>
> In the [fixed](./content-address.md#fixed) content-addressing case, the outputs' content addresses are statically specified in advance, so this always just works.
> (The fixed case is what the pseudo-code shows.)
>
> In the [floating](./content-address.md#floating) case, the content addresses are not specified in advance.
> This is what the "or get from [build trace](@docroot@/store/build-trace.md) if floating" comment refers to.
> In this case, the algorithm is *stuck* until the input in question is built, and we know what the actual contents of the output in question is.
>
> That is OK however, because there is no problem with delaying the assigning of input addresses (which, remember, is what `hashQuotientDerivation` is ultimately for) until all inputs are known.

### Deferring input-addressing when downstream of unknown CA store objects {#deferred}

As the previous note says, the algorithm is *stuck* when an input is a [floating content-addressed](./content-address.md#floating) output that has not been built yet: there is no content address to substitute for it, so the quotient derivation cannot be completed and no input address can be computed.

The derivation is still written to the store, with its outputs *deferred*: an output in this state has no store path at all, and the environment variable named after it is empty.
This costs nothing, because a derivation's own store path is [content-addressed](@docroot@/store/store-object/content-address.md#method-text) on the bytes of its serialization, which do not depend on its output paths being known.

A deferred output does not use a third kind of addressing alongside input- and content-addressing.
It is input-addressed; it just does not know its path yet.

Being stuck is contagious.
A derivation with deferred outputs has, for this purpose, unknown outputs of its own, so `hashOutputsOrQuotientDerivation` is stuck on it in turn, and anything depending on it must also defer.
The condition therefore propagates downstream from the floating output that caused it.

Deferral is resolved by building, not by revisiting the same derivation.
Once the offending inputs are built, their content addresses are known, and the derivation can be [resolved](@docroot@/store/resolution.md): its inputs are replaced by the plain store paths they turned out to denote.
The resolved derivation is a *different* derivation, with its own store path --- necessarily so, since it has different bytes.
It has no input derivations left to be stuck on, so its input addresses are computable, and it is what actually gets built.

> **Note**
>
> Nothing gates deferred outputs themselves.
> They only *arise* downstream of [floating content-addressed](./content-address.md#floating) outputs.

### Performance

The recursion in the algorithm is potentially inefficient:
it could call itself once for each path by which a subderivation can be reached, i.e., `O(V^k)` times for a derivation graph with `V` derivations and with out-degree of at most `k`.
In the actual implementation, [memoisation](https://en.wikipedia.org/wiki/Memoization) is used to reduce this cost to be proportional to the total number of `inputDrvOutputs` encountered.

### Semantic properties

*See [this chapter's appendix](@docroot@/store/math-notation.md) on grammar and metavariable conventions.*

In essence, `hashQuotientDerivation` partitions input-addressing derivations into equivalence classes: every derivation in that equivalence class is mapped to the same derivation hash.
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
> It is coarser than this one: any two derivations which are "'hash quotient derivation'-equivalent" (\\(\\sim_\mathrm{IADrv}\\)) are also "resolution-equivalent" (\\(\\sim_\mathrm{Drv}\\)).
> It also relates derivations whose `inputDrvOutputs` have been rewritten into `inputSrcs`.
>
> Input-addressing downstream of content addressing, as described above, anticipates this.
> Since a content-addressed input contributes only its content address, and not the derivation that produced it, derivations differing only in *how* an identical content-addressed input was produced are already \\(\\sim_\mathrm{IADrv}\\)-equivalent.
> And since a deferred derivation is completely resolved before it is input-addressed, its input-addressed `inputDrvOutputs` are normalized to `inputSrcs` as well --- so derivations differing only in how an identical *input-addressed* input was produced are \\(\\sim_\mathrm{IADrv}\\)-equivalent too.
> That is the same "identify an input by what it is, rather than by how it was made" move as \\(\\sim_\mathrm{Drv}\\).
> The difference is that \\(\\sim_\mathrm{Drv}\\) makes it unconditionally, whereas here it falls out for content-addressed inputs always, and for the rest only where deferral forced a resolution first.

[deriving-path]: @docroot@/store/derivation/index.md#deriving-path
[xp-feature-dynamic-derivations]: @docroot@/development/experimental-features.md#xp-feature-dynamic-derivations
[xp-feature-ca-derivations]: @docroot@/development/experimental-features.md#xp-feature-ca-derivations
