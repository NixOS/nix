# Store Paths

A store path is a pair of a 20-byte digest and a name.

Historically it is the triple of those two and also the store directory, but the modern implementation's internal representation is just the pair.
This change is because in the vast majority of cases, the store dir is fully determined by the context in which the store path occurs.

## String representation

A store path is rendered as the concatenation of

  - the store directory

  - a path-separator (`/`)

  - the digest rendered as Base-32 (20 bytes becomes 32 bytes)

  - a hyphen (`-`)

  - the name

Let's take the store path from the very beginning of this manual as an example:

    /nix/store/b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1/

This parses like so:

    /nix/store/b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1/
    ^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^
    store dir  digest                           name

## The digest

The calculation of the digest is quite complicated for historical reasons.
Some of the details will be saved for later.

> Historical note. The 20 byte restriction is because originally a digests were SHA-1 hashes.
> This is no longer true, but longer hashes and other information is still boiled down to 20 bytes.

Store paths are either content-addressed or "input-addressed".

Content addressing means that the digest ultimately derives from referred store object's file system data and references, and thus can be verified (if one knows how it was calculated).

Input addressing means that the digest derives from how the store path was produced -- the "inputs" and plan that it was built from.
Store paths of this sort can not be validated from the content of the store object.
Rather, the store object might come with the store path it expects to be referred to by, and a signature of that path, the contents of the store path, and other metadata.
The signature indicates that someone is vouching for the store object really being the results of a plan with that digest.

While metadata is included in the digest calculation explaining which method it was calculated by, this only serves to thwart pre-image attacks.
That metadata is scrambled with everything else so that it is difficult to tell how a given store path was produced short of a brute-force search.
In the parlance of referencing schemes, this means that store paths are not "self-describing".
