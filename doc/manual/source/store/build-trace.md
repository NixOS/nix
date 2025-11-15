# Build Trace

> **Warning**
>
> This entire concept is currently
> [**experimental**](@docroot@/development/experimental-features.md#xp-feature-ca-derivations)
> and subject to change.

The *build trace* is a [memoization table](https://en.wikipedia.org/wiki/Memoization) for builds.
It maps the inputs of builds to the outputs of builds.
Concretely, that means it maps [derivations][derivation] to maps of [output] names to [store objects][store object].

In general the derivations used as a key should be [*resolved*](./resolution.md).
A build trace with all-resolved-derivation keys is also called a *base build trace* for extra clarity.
If all the resolved inputs of a derivation are content-addressed, that means the inputs will be fully determined, leaving no ambiguity for what build was performed.
(Input-addressed inputs however are still ambiguous. They too should be locked down, but this is left as future work.)

Accordingly, to look up an unresolved derivation, one must first resolve it to get a resolved derivation.
Resolving itself involves looking up entries in the build trace, so this is a mutually recursive process that will end up inspecting possibly many entries.

Except for the issue with input-addressed paths called out above, base build traces are trivially *coherent* -- incoherence is not possible.
That means that the claims that each key-value base build try entry makes are independent, and no mapping invalidates another mapping.

Whether the mappings are *true*, i.e. the faithful recording of actual builds performed, is another matter.
Coherence is about the multiple claims of the build trace being mutually consistent, not about whether the claims are individually true or false.

In general, there is no way to audit a build trace entry except for by performing the build again from scratch.
And even in that case, a different result doesn't mean the original entry was a "lie", because the derivation being built may be non-deterministic.
As such, the decision of whether to trust a counterparty's build trace is a fundamentally subject policy choice.
Build trace entries are typically *signed* in order to enable arbitrary public-key-based trust polices.

## Derived build traces {#derived}

Implementations that wish to memoize the above may also keep additional *derived* build trace entries that do map unresolved derivations.
But if they do so, they *must* also keep the underlying base entries with resolved derivation keys around.
Firstly, this ensures that the derived entries are merely cache, which could be recomputed from scratch.
Secondly, this ensures the coherence of the derived build trace.

Unlike with base build traces, incoherence with derived build traces is possible.
The key ingredient is that derivation resolution is only deterministic with respect to a fixed base build trace.
Without fixing the base build trace, it inherits the subjectivity of base build traces themselves.

Concretely, suppose there are three derivations \\(a\\), \\(b\\), and \\(c\\).
Let \\(a\\) be a resolved derivation, but let \\(b\\) and \\(c\\) be unresolved and both take as an input an output of \\(a\\).
Now suppose that derived entries are made for \\(b\\) and \\(c\\) based on two different entries of \\(a\\).
(This could happen if \\(a\\) is non-deterministic, \\(a\\) and \\(b\\) are built in one store, \\(a\\) and \\(c\\) are built in another store, and then a third store substitutes from both of the first two stores.)

If trusting the derived build trace entries for \\(b\\) and \\(c\\) requires that each's underlying entry for \\(a\\) be also trusted, the two different mappings for \\(a\\) will be caught.
However, if \\(b\\) and \\(c\\)'s entries can be combined in isolation, there will be nothing to catch the contradiction in their hidden assumptions about \\(a\\)'s output.

[derivation]: ./derivation/index.md
[output]: ./derivation/outputs/index.md
[store object]: @docroot@/store/store-object.md
