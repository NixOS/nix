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

[fso-ca]: ../file-system-object/content-address.md
[sp-spec]: @docroot@/protocols/store-path.md
[xp-feature-git-hashing]: @docroot@/contributing/experimental-features.md#xp-feature-git-hashing
