# Content-Addressing Store Objects

Before in the section on [store paths](./paths.md), we talked abstractly about content-adressing store objects to preduce the digest part of their store paths.
Now we can make that precise.

Rather than reasoning *backwards* from the grammar to the underlying semantics, as the [summary](./path.md#summary) of the previous section on store paths does,
let's reasons *forwards* first describing the semantics, and then the string encoding.

## Semantics

### File System Objects

File system objects are hashed in

- [Nix Archive (NAR)](./nar.md) hashing, where an arbitrary FSOs and its children are serialized according to that format and then hashed.

- Flat hashing, where the FSO must be a single file, and its contents are hashed.

Single files can thus be hashed in two ways, as a NAR can contain a single file.
All other file system objects must be hashed via their NAR representation, as flat hashing is not an option.

### References

Reference to other objects are referred to by regular (string-encoded-) store paths.

Self-references however cannot be referred to by their path, because we are in the midst of describing how to compute that path!

> The alternative would require finding as hash function fixed point, i.e. the solution to an equation in the form
> ```
> digest = hash(..... || digest || ....)
> ```
> which is computationally infeasible, being no easier than finding a hash collision.

Instead we just have a "has self reference" boolean, which effects the eventual serialization

### Putting it all together

For historical reasons, we don't support all features in all combinations.
Instead we support 2 cases: "text" content-addressing vs regular "fixed" content-addressing,
with "fixed" content-addressing additionally broken into two cases depending on which type of file system object content addressing is used.

The resulting abstract syntax is this:

```idris
data HashType
  = MD5
  | SHA-1
  | SHA-256
  | SHA-512

record Hash where
  type : HashType
  hash : Bytes[hashSize type]

data Method
  = NixArchive
  | FlatFile

data ContentAddress
  = Text {
      hash             : Hash,
      references       : Set StorePath
    }
  | Fixed {
      method           : Method,
      hash             : Hash,
      references       : Set StorePath
      hasSelfReference : Bool
    } -- has side condition
```

Firstly, note that "text" hashing supports references to other paths, but no "has self reference" boolean:
texted-hashed store object must not have a self reference.
Only regular "fixed" hashing supports the boolean is thus allowed to represent store paths.

There is an additional side condition that a regular "fixed" output addressing only supports references (to self or other objects) if the method is NAR and the hash type is SHA-256.

> We could encode this in the abstract syntax, but it would only further complicate it.
> It is better that we instead lift this restriction :)
> It need not exist, it just the result of no demand to change it + doing the simplest thing to maintain to backwards compatibility in the encoding so old store objects' store paths do not change.
> With just a bit of thinking we could further extend the format to lift this restriction in a backwards-compatible way.

### Reproducibility

The above system is rather more complex than it needs to be, owning to accretion of features over time.
Still, the fundamental property remains that if one knows how a store object is supposed to be hashed
--- all the non-Hash, non-references information above
--- one can recompute a store object's store path just from that metadata and its content proper (its references and file system objects).
Collectively, we can call this information the "content addressing method":

```idris
data ContentAddressMethod
  = TextMethod {
    }
  | Fixed {
      method           : Method,
    } -- has side condition
```

The following simple record

```idris
record SimpleContentAddress where
  method           : ContentAddressMethod
  hash             : Hash
  references       : Set StorePath
  hasSelfReference : Bool
```

can thus represent every `ContentAddress`, and also some combinations that are currently disallowed (but do make sense in principle).

By storing the `ContentAddressMethod` extra information as part of store object
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
([Input addressing](./drv/ia.md), discussed later, is only valid for store paths produced from derivations.)

Additionally, content addressing is also used for the outputs of [certain sorts of derivations](./drv/ca.md).
It is very nice to be able to uniformly content-address all data rather than rely on a mix of content addressing and input addressing.
This however, is in some cases still experimental, so in practice input addressing is still (as of 2022) widely used.

## Encoding

With the semantics of content-addressing thus clarified, we can now discuss the encoding to a string.

TODO

Note that since a derivation output has always type `output`, while something added to the store manually can have type `output` or `source` depending on the hash,
this means that the same input can be hashed differently if added to the store via addToStore or via a derivation, in the [SHA-256](sha-256) & [NAR](./nar.md) case.

It would have been nicer to handle fixed-output derivations under `source`, e.g. have something like `source:<rec><algo>`, but we're stuck with this for now.

The main reason for this way of computing names is to prevent name collisions (for security).
For instance, it shouldn't be feasible to come up with a derivation whose output path collides with the path for a copied source.
The former would have a `<pre>` starting with `output:out:`, while the latter would have a `<pre>` starting with `source:`.
