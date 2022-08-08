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
    }
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

Per the [previous section on store paths](./path.md), every store path is calculated using a hashed

`<pre>` = the string `<type>:sha256:<inner-digest>:<store>:<name>`;

and within that just `<type>` and `<inner-digest>` are determined differently based on the type of store path.

### Type

#### Spec

`<type>` = one of:

- For the "text" content-addressing method:
  ```bnf
  text:<r1>:<r2>:...<rN>
  ```

- For the "fixed" content-addressing method, one of:

   - When the method is [Nix Archive (NAR)](./nar.md) (not flat) and the hash type is [SHA-256](sha-256):
     ```bnf
     source:<r1>:<r2>:...:<rN>:self
     ```

   - Otherwise:
     ```bnf
     output:out
     ```

In all of the above, `<r1> ... <rN>` are the store paths referenced by this path.
Those are encoded in the form described by `<realized-path>`.

#### Restrictions on references

Note that the `output:out` case alone doesn't have any `<r1> ... <rN>`; this is why there was the side condition stated above that cases besides [Nix Archive (NAR)](./nar.md) and [SHA-256](sha-256) didn't support references.

#### A historical accident

The `source` vs `output:out` distinction dates back to making sure that input-addressed derivation outputs and content-addressed sources would never share store paths (baring a hash collision).
This is a good property, important for security.
For instance, it shouldn't be feasible to come up with a derivation whose output path collides with the path for a copied source.
The former would have a `<type>` of `output:out`, while the latter would have a `<type>` of `source`.

However, later on derivations with content addressed outputs existed (the fixed output case much earlier than the floating output one), and they nonetheless used `output:out` despite being fixed output, muddling the distinction.
The "inner digest" still ensures our security property is intact, but the "type" no longer tells a clear store.

It would have been nicer to handle all paths for content-addressed objects in a uniform way, but we're stuck with this for now.

### Inner Digest

#### Spec

`<inner-digest>` = base-16 representation of a SHA-256 hash of:

- For the "text" content-addressing method:

  the string -- root file system object must be a single file which is that string.

- For the "fixed" content-addressing method, one of:

  - When the method is [Nix Archive (NAR)](./nar.md) (not flat) and the hash type is [SHA-256](sha-256):

    the NAR serialisation of the path from which this store path is copied

  - Otherwise:

    the string `fixed:out:<rec><algo>:<hash>:`, where

      - `<rec>` = one of:

        - `r:` for [NAR](./nar.md) (arbitrary file system object) hashes

        - `` (empty string) for flat (single file) hashes

      - `<algo>` = `md5`, `sha1` or `sha256`

      -`<hash>` = base-16 representation of the path or flat hash of the contents of the path (or expected contents of the path for fixed-output derivations).

#### Relying on invariants to encode less information

Both in the "text" and "fixed and method = flat" cases, the file system objects are just a single root file, and so we just take that directly, and don't need the extra information a NAR wrapping that single object would provide

Both in the "text" and "fixed, method = NAR, and hash type = SHA-256" (i.e. in the encoding type = `source`) cases,
We also the hash type is SHA-256, so we don't need to encode that either.

#### How many times do we hash?

In all cases, the complete contents of the file system objects of the store object affects the store hash.

In the "text" and "fixed, method = NAR, and hash type = SHA-256" (i.e. with the encoding type = `source`) cases,
we know both the hash type and file system object serialization method.
We therefore *directly* hash the file system object(s) to get the inner digest.

In the other "fixed" cases (i.e. with the encoding type = `fixed:output`), we do not know this information statically, so we need to wrap the file system objects digest with that information and hash a *second* time to produce the inner digest
