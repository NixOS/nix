# Content-Addressing Store Objects

Just [like][fso-ca] [File System Objects][File System Object],
[Store Objects][Store Object] can also be [content-addressed](@docroot@/glossary.md#gloss-content-addressed),
unless they are [input-addressed](@docroot@/glossary.md#gloss-input-addressed-store-object).

For store objects, the content address we produce will take the form of a [Store Path] rather than regular hash.
In particular, the content-addressing scheme will ensure that the digest of the store path is solely computed from the

- file system object graph (the root one and its children, if it has any)
- references
- [store directory](../store-path.md#store-directory)
- name

of the store object, and not any other information, which would not be an intrinsic property of that store object.

For the full specification of the algorithms involved, see the [specification of store path digests][sp-spec].

[File System Object]: ../file-system-object.md
[Store Object]: ../store-object.md
[Store Path]: ../store-path.md

## Content addressing each part of a store object

### File System Objects

With all currently supported store object content addressing methods, the file system object is always [content-addressed][fso-ca] first, and then that hash is incorporated into content address computation for the store object.

### References

With all currently supported store object content addressing methods,
other objects are referred to by their regular (string-encoded-) [store paths][Store Path].

Self-references however cannot be referred to by their path, because we are in the midst of describing how to compute that path!

> The alternative would require finding as hash function fixed point, i.e. the solution to an equation in the form
> ```
> digest = hash(..... || digest || ....)
> ```
> which is computationally infeasible.
> As far as we know, this is equivalent to finding a hash collision.

Instead we just have a "has self reference" boolean, which will end up affecting the digest.

### Name and Store Directory

These two items affect the digest in a way that is standard for store path digest computations and not specific to content-addressing.
Consult the [specification of store path digests][sp-spec] for further details.

## Content addressing Methods

For historical reasons, we don't support all features in all combinations.
Each currently supported method of content addressing chooses a single method of file system object hashing, and may offer some restrictions on references.
The names and store directories are unrestricted however.

### Flat { #method-flat }

This uses the corresponding [Flat](../file-system-object/content-address.md#serial-flat) method of file system object content addressing.

References are not supported: store objects with flat hashing *and* references can not be created.

### Text { #method-text }

This also uses the corresponding [Flat](../file-system-object/content-address.md#serial-flat) method of file system object content addressing.

References to other store objects are supported, but self references are not.

This is the only store-object content-addressing method that is not named identically with a corresponding file system object method.
It is somewhat obscure, mainly used for "drv files"
(derivations serialized as store objects in their ["ATerm" file format](@docroot@/protocols/derivation-aterm.md)).
Prefer another method if possible.

### Nix Archive { #method-nix-archive }

This uses the corresponding [Nix Archive](../file-system-object/content-address.md#serial-nix-archive) method of file system object content addressing.

References (to other store objects and self references alike) are supported so long as the hash algorithm is SHA-256, but not (neither kind) otherwise.

### Git { #method-git }

> **Warning**
>
> This method is part of the [`git-hashing`][xp-feature-git-hashing] experimental feature.

This uses the corresponding [Git](../file-system-object/content-address.md#serial-git) method of file system object content addressing.

References are not supported.

Only SHA-1 is supported at this time.
If [SHA-256-based Git](https://git-scm.com/docs/hash-function-transition)
becomes more widespread, this restriction will be revisited.

### Reproducibility

The above system is more complex than it needs to be to support all types of file system objects and references, owing to accretion of features over time.
However, there's a lot of value in supporting old expressions and reproducing the same hashes with any version of Nix.
Still, the fundamental property remains that if one knows how a store object is supposed to be hashed
--- all the non-Hash, non-references information above
--- one can recompute a store object's store path just from that metadata and its content proper (its references and file system objects).
Collectively, we can call this information the "content address method".

By storing the "Content address method" extra information as part of store object
--- making it data not metadata
--- we achieve the key property of making content-addressed store objects *trustless*.

What this is means is that they are just plain old data, not containing any "claim" that could be false.
All this information is free to vary, and if any of it varies one gets (ignoring the possibility of hash collisions, as usual) a different store path.
Store paths referring to content-addressed store objects uniquely identify a store object, and given that object, one can recompute the store path.
Any content-addressed store object purporting to be the referee of a store object can be readily verified to see whether it in fact does without any extra information.
No other party claiming a store object corresponds to a store path need be trusted because this verification can be done instead.

Content addressing currently is used when adding data like source code to the store.
Such data are "basal inputs", not produced from any other derivation (to our knowledge).
Content addressing is thus the only way to address them of our two options.
([Input addressing](@docroot@/glossary.md#gloss-input-addressed-store-object), is only valid for store paths produced from derivations.)

Additionally, content addressing is also used for the outputs of certain sorts of derivations.
It is very nice to be able to uniformly content-address all data rather than rely on a mix of content addressing and input addressing.
This however, is in some cases still experimental, so in practice input addressing is still (as of 2022) widely used.

[fso-ca]: ../file-system-object/content-address.md
[sp-spec]: @docroot@/protocols/store-path.md
[xp-feature-git-hashing]: @docroot@/contributing/experimental-features.md#xp-feature-git-hashing
