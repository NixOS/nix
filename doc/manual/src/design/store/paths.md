# Store Paths

A store path is a pair of a 20-byte digest and a name.

## String representation

A store path is rendered as the concatenation of

  - a store directory

  - a path-separator (`/`)

  - the digest rendered as Base-32 (20 arbitrary bytes becomes 32 ASCII chars)

  - a hyphen (`-`)

  - the name

Let's take the store path from the very beginning of this manual as an example:

    /nix/store/b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1

This parses like so:

    /nix/store/b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1
    ^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^
    store dir  digest                           name

We then can discard the store dir to recover the conceptual pair that is a store path:

    {
      digest: "b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z",
      name:   "firefox-33.1",
    }

### Where did the "store directory" come from?

If you notice, the above references a "store directory", but that is *not* part of the definition of a store path.
We can discard it when parsing, but what about when printing?
We need to get a store directory from *somewhere*.

The answer is, the store directory is a property of the store that contains the store path.
The explanation for this is simple enough: a store is notionally mounted as a directory at some location, and the store object's root file system likewise mounted at this path within that directory.

This does, however, mean the string representation of a store path is not derived just from the store path itself, but is in fact "context dependent".

## The digest

The calculation of the digest is quite complicated for historical reasons.
Some of the details will be saved for later.

> Historical note. The 20 byte restriction is because originally a digests were SHA-1 hashes.
> This is no longer true, but longer hashes and other information is still boiled down to 20 bytes.

Store paths are either content-addressed or "input-addressed".

Content addressing means that the digest ultimately derives from referred store object's file system objects and references, and thus can be verified.
There is more than one *method* of content-addressing, however.
Still, if one does know the the content addressing schema that was used,
(or guesses, there isn't that many yet!)
one can recalcuate the store path and thus verify the store object.

Input addressing means that the digest derives from how the store path was produced -- the "inputs" and plan that it was built from.
Store paths of this sort can *not* be validated from the content of the store object.
Rather, the store object might come with the store path it expects to be referred to by, and a signature of that path, the contents of the store path, and other metadata.
The signature indicates that someone is vouching for the store object really being the results of a plan with that digest.

While metadata is included in the digest calculation explaining which method it was calculated by, this only serves to thwart pre-image attacks.
That metadata is scrambled with everything else so that it is difficult to tell how a given store path was produced short of a brute-force search.
In the parlance of referencing schemes, this means that store paths are not "self-describing".
