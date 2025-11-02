# Input-addressing derivation outputs

[input addressing]: #input-addressing

"Input addressing" means the address the store object by the *way it was made* rather than *what it is*.
That is to say, an input-addressed output's store path is a function not of the output itself, but of the derivation that produced it.
Even if two store paths have the same contents, if they are produced in different ways, and one is input-addressed, then they will have different store paths, and thus guaranteed to not be the same store object.

<!---

### Modulo fixed-output derivations

**TODO hash derivation modulo.**

So how do we compute the hash part of the output path of a derivation?
This is done by the function `hashDrv`, shown in Figure 5.10.
It distinguishes between two cases.
If the derivation is a fixed-output derivation, then it computes a hash over just the `outputHash` attributes.

If the derivation is not a fixed-output derivation, we replace each element in the derivation’s inputDrvs with the result of a call to `hashDrv` for that element.
(The derivation at each store path in `inputDrvs` is converted from its on-disk ATerm representation back to a `StoreDrv` by the function `parseDrv`.) In essence, `hashDrv` partitions store derivations into equivalence classes, and for hashing purpose it replaces each store path in a derivation graph with its equivalence class.

The recursion in Figure 5.10 is inefficient:
it will call itself once for each path by which a subderivation can be reached, i.e., `O(V k)` times for a derivation graph with `V` derivations and with out-degree of at most `k`.
In the actual implementation, memoisation is used to reduce this to `O(V + E)` complexity for a graph with E edges.

-->

[xp-feature-ca-derivations]: @docroot@/development/experimental-features.md#xp-feature-ca-derivations
[xp-feature-git-hashing]: @docroot@/development/experimental-features.md#xp-feature-git-hashing
[xp-feature-impure-derivations]: @docroot@/development/experimental-features.md#xp-feature-impure-derivations
