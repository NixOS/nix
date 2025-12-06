# Content-addressing derivation outputs

The content-addressing of an output only depends on that store object itself, not any other information external (such has how it was made, when it was made, etc.).
As a consequence, a store object will be content-addressed the same way regardless of whether it was manually inserted into the store, outputted by some derivation, or outputted by a some other derivation.

The output spec for a content-addressed output must contains the following field:

- *method*: how the data of the store object is digested into a content address

The possible choices of *method* are described in the [section on content-addressing store objects](@docroot@/store/store-object/content-address.md).
Given the method, the output's name (computed from the derivation name and output spec mapping as described above), and the data of the store object, the output's store path will be computed as described in that section.

## Fixed-output content-addressing {#fixed}

In this case the content address of the *fixed* in advanced by the derivation itself.
In other words, when the derivation has finished [building](@docroot@/store/building.md), and the provisional output' content-address is computed as part of the process to turn it into a *bona fide* store object, the calculated content address must much that given in the derivation, or the build of that derivation will be deemed a failure.

The output spec for an output with a fixed content addresses additionally contains:

- *hash*, the hash expected from digesting the store object's file system objects.
  This hash may be of a freely-chosen hash algorithm (that Nix supports)

> **Design note**
>
> In principle, the output spec could also specify the references the store object should have, since the references and file system objects are equally parts of a content-addressed store object proper that contribute to its content-addressed.
> However, at this time, the references are not done because all fixed content-addressed outputs are required to have no references (including no self-reference).
>
> Also in principle, rather than specifying the references and file system object data with separate hashes, a single hash that constraints both could be used.
> This could be done with the final store path's digest, or better yet, the hash that will become the store path's digest before it is truncated.
>
> These possible future extensions are included to elucidate the core property of fixed-output content addressing --- that all parts of the output must be cryptographically fixed with one or more hashes --- separate from the particulars of the currently-supported store object content-addressing schemes.

### Design rationale

What is the purpose of fixing an output's content address in advanced?
In abstract terms, the answer is carefully controlled impurity.
Unlike a regular derivation, the [builder] executable of a derivation that produced fixed outputs has access to the network.
The outputs' guaranteed content-addresses are supposed to mitigate the risk of the builder being given these capabilities;
regardless of what the builder does *during* the build, it cannot influence downstream builds in unanticipated ways because all information it passed downstream flows through the outputs whose content-addresses are fixed.

[builder]: @docroot@/store/derivation/index.md#builder

In concrete terms, the purpose of this feature is fetching fixed input data like source code from the network.
For example, consider a family of "fetch URL" derivations.
These derivations download files from given URL.
To ensure that the downloaded file has not been modified, each derivation must also specify a cryptographic hash of the file.
For example,

```jsonc
{
  "outputs: {
    "out": {
      "method": "nar",
      "hashAlgo": "sha256",
      "hash: "1md7jsfd8pa45z73bz1kszpp01yw6x5ljkjk2hx7wl800any6465",
    },
  },
  "env": {
    "url": "http://ftp.gnu.org/pub/gnu/hello/hello-2.1.1.tar.gz"
    // ...
  },
  // ...
}
```

It sometimes happens that the URL of the file changes,
e.g., because servers are reorganised or no longer available.
In these cases, we then must update the call to `fetchurl`, e.g.,

```diff
   "env": {
-    "url": "http://ftp.gnu.org/pub/gnu/hello/hello-2.1.1.tar.gz"
+    "url": "ftp://ftp.nluug.nl/pub/gnu/hello/hello-2.1.1.tar.gz"
     // ...
   },
```

If a `fetchurl` derivation's outputs were [input-addressed][input addressing], the output paths of the derivation and of *all derivations depending on it* would change.
For instance, if we were to change the URL of the Glibc source distribution in Nixpkgs (a package on which almost all other packages depend on Linux) massive rebuilds would be needed.
This is unfortunate for a change which we know cannot have a real effect as it propagates upwards through the dependency graph.

For content-addressed outputs (fixed or floating), on the other hand, the outputs' store path only depends on the derivation's name, data, and the `method` of the outputs' specs.
The rest of the derivation is ignored for the purpose of computing the output path.

> **History Note**
>
> Fixed content-addressing is especially important both today and historically as the *only* form of content-addressing that is stabilized.
> This is why the rationale above contrasts it with [input addressing].

## (Floating) Content-Addressing {#floating}

> **Warning**
> This is part of an [experimental feature](@docroot@/development/experimental-features.md).
>
> To use this type of output addressing, you must enable the
> [`ca-derivations`][xp-feature-ca-derivations] experimental feature.
> For example, in [nix.conf](@docroot@/command-ref/conf-file.md) you could add:
>
> ```
> extra-experimental-features = ca-derivations
> ```

With this experimemental feature enabled, derivation outputs can also be content-addressed *without* fixing in the output spec what the outputs' content address must be.

### Purity

Because the derivation output is not fixed (just like with [input addressing]), the [builder] is not given any impure capabilities [^purity].

> **Configuration note**
>
> Strictly speaking, the extent to which sandboxing and deprivilaging is possible varies with the environment Nix is running in.
> Nix's configuration settings indicate what level of sandboxing is required or enabled.
> Builds of derivations will fail if they request an absence of sandboxing which is not allowed.
> Builds of derivations will also fail if the level of sandboxing specified in the configure exceeds what is possible in the given environment.
>
> (The "environment", in this case, consists of attributes such as the Operating System Nix runs atop, along with the operating-system-specific privileges that Nix has been granted.
> Because of how conventional operating systems like macos, Linux, etc. work, granting builders *fewer* privileges may ironically require that Nix be run with *more* privileges.)

That said, derivations producing floating content-addressed outputs may declare their builders as impure (like the builders of derivations producing fixed outputs).
This is provisionally supported as part of the [`impure-derivations`][xp-feature-impure-derivations] experimental feature.

### Compatibility negotiation

Any derivation producing a floating content-addressed output implicitly requires the `ca-derivations` [system feature](@docroot@/command-ref/conf-file.md#conf-system-features).
This prevents scheduling the building of the derivation on a machine without the experimental feature enabled.
Even once the experimental feature is stabilized, this is still useful in order to be allow using remote builder running odler versions of Nix, or alternative implementations that do not support floating content addressing.

### Determinism

In the earlier [discussion of how self-references are handled when content-addressing store objects](@docroot@/store/store-object/content-address.html#self-references), it was pointed out that methods of producing store objects ought to be deterministic regardless of the choice of provisional store path.
For store objects produced by manually inserting into the store to create a store object, the "method of production" is an informally concept --- formally, Nix has no idea where the store object came from, and content-addressing is crucial in order to ensure that the derivation is *intrinsically* tamper-proof.
But for store objects produced by derivation, the "method is quite formal" --- the whole point of derivations is to be a formal notion of building, after all.
In this case, we can elevate this informal property to a formal one.

A *deterministic* content-addressing derivation should produce outputs with the same content addresses:

1. Every time the builder is run

  This is because either the builder is completely sandboxed, or because all any remaining impurities that leak inside the build sandbox are ignored by the builder and do not influence its behavior.

2. Regardless of the choice of any provisional outputs paths

  Provisional store paths must be chosen for any output that has a self-reference.
  The choice of provisional store path can be thought of as an impurity, since it is an arbitrary choice.

  If provisional outputs paths are deterministically chosen, we are in the first branch of part (1).
  The builder the data it produces based on it in arbitrary ways, but this gets us closer to [input addressing].
  Deterministically choosing the provisional path may be considered "complete sandboxing" by removing an impurity, but this is unsatisfactory

  <!--

  TODO
  (Both these points will be expanded-upon below.)

  -->

  If provisional outputs paths are randomly chosen, we are in the second branch of part (1).
  The builder *must* not let the random input affect the final outputs it produces, and multiple builds may be performed and the compared in order to ensure that this is in fact the case.

### Floating versus Fixed

While the distinction between content- and input-addressing is one of *mechanism*, the distinction between fixed and floating content addressing is more one of *policy*.
A fixed output that passes its content address check is just like a floating output.
It is only in the potential for that check to fail that they are different.

> **Design Note**
>
> In a future world where floating content-addressing is also stable, we in principle no longer need separate [fixed](#fixed) content-addressing.
> Instead, we could always use floating content-addressing, and separately assert the precise value content address of a given store object to be used as an input (of another derivation).
> A stand-alone assertion object of this sort is not yet implemented, but its possible creation is tracked in [issue #11955](https://github.com/NixOS/nix/issues/11955).
>
> In the current version of Nix, fixed outputs which fail their hash check are still registered as valid store objects, just not registered as outputs of the derivation which produced them.
> This is an optimization that means if the wrong output hash is specified in a derivation, and then the derivation is recreated with the right output hash, derivation does not need to be rebuilt &mdash; avoiding downloading potentially large amounts of data twice.
> This optimisation prefigures the design above:
> If the output hash assertion was removed outside the derivation itself, Nix could additionally not only register that outputted store object like today, but could also make note that derivation did in fact successfully download some data.
For example, for the "fetch URL" example above, making such a note is tantamount to recording what data is available at the time of download at the given URL.
> It would only be when Nix subsequently tries to build something with that (refining our example) downloaded source code that Nix would be forced to check the output hash assertion, preventing it from e.g. building compromised malware.
>
> Recapping, Nix would
>
> 1. successfully download data
> 2. insert that data into the store
> 3. associate (presumably with some sort of expiration policy) the downloaded data with the derivation that downloaded it
>
> But only use the downloaded store object in subsequent derivations that depended upon the assertion if the assertion passed.
>
> This possible future extension is included to illustrate this distinction:

[input addressing]: ./input-address.md
[xp-feature-ca-derivations]: @docroot@/development/experimental-features.md#xp-feature-ca-derivations
[xp-feature-git-hashing]: @docroot@/development/experimental-features.md#xp-feature-git-hashing
[xp-feature-impure-derivations]: @docroot@/development/experimental-features.md#xp-feature-impure-derivations
