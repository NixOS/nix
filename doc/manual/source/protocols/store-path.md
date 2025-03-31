# Complete Store Path Calculation

This is the complete specification for how [store path]s are calculated.

The format of this specification is close to [Extended Backus–Naur form](https://en.wikipedia.org/wiki/Extended_Backus%E2%80%93Naur_form), but must deviate for a few things such as hash functions which we treat as bidirectional for specification purposes.

Regular users do *not* need to know this information --- store paths can be treated as black boxes computed from the properties of the store objects they refer to.
But for those interested in exactly how Nix works, e.g. if they are reimplementing it, this information can be useful.

[store path]: @docroot@/store/store-path.md

## Store path proper

```ebnf
store-path = store-dir "/" digest "-" name
```
where

- `name` = the name of the store object.

- `store-dir` = the [store directory](@docroot@/store/store-path.md#store-directory)

- `digest` = base-32 representation of the compressed to 160 bits [SHA-256] hash of `fingerprint`

For the definition of the hash compression algorithm, please refer to the section 5.1 of
the [Nix thesis](https://edolstra.github.io/pubs/phd-thesis.pdf), which also defines the
specifics of base-32 encoding. Note that base-32 encoding processes the hash bytestring from
the end, while base-16 processes in from the beginning.

## Fingerprint

- ```ebnf
  fingerprint = type ":sha256:" inner-digest ":" store ":" name
  ```

  Note that it includes the location of the store as well as the name to make sure that changes to either of those are reflected in the hash
  (e.g. you won't get `/nix/store/<digest>-name1` and `/nix/store/<digest>-name2`, or `/gnu/store/<digest>-name1`, with equal hash parts).

- `type` = one of:

  - ```ebnf
    | "text" { ":" store-path }
    ```

    This is for the
    ["Text"](@docroot@/store/store-object/content-address.md#method-text)
    method of content addressing store objects.
    The optional trailing store paths are the references of the store object.

  - ```ebnf
    | "source" { ":" store-path } [ ":self" ]
    ```

    This is for the
    ["Nix Archive"](@docroot@/store/store-object/content-address.md#method-nix-archive)
    method of content addressing store objects,
    if the hash algorithm is [SHA-256].
    Just like in the "Text" case, we can have the store objects referenced by their paths.
    Additionally, we can have an optional `:self` label to denote self-reference.

  - ```ebnf
    | "output:" id
    ```

    For either the outputs built from derivations,
    or content-addressed store objects that are not using one of the two above cases.
    To be explicit about the latter, that is currently these methods:

    - ["Flat"](@docroot@/store/store-object/content-address.md#method-flat)
    - ["Git"](@docroot@/store/store-object/content-address.md#method-git)
    - ["Nix Archive"](@docroot@/store/store-object/content-address.md#method-nix-archive) if the hash algorithm is not [SHA-256].

    `id` is the name of the output (usually, "out").
    For content-addressed store objects, `id`, is always "out".

- `inner-digest` = base-16 representation of a SHA-256 hash of `inner-fingerprint`.
  The base-16 encoding uses lower-cased hex digits.

## Inner fingerprint

- `inner-fingerprint` = one of the following based on `type`:

  - if `type` = `"text:" ...`:

    the string written to the resulting store path.

  - if `type` = `"source:" ...`:

    the [Nix Archive (NAR)] serialization of the [file system object](@docroot@/store/file-system-object.md) of the store object.

  - if `type` = `"output:" id`:

    - For input-addressed derivation outputs:

      the [ATerm](@docroot@/protocols/derivation-aterm.md) serialization of the derivation modulo fixed output derivations.

    - For content-addressed store paths:

      ```ebnf
      "fixed:out:" rec algo ":" hash ":"
      ```

      where

      - `rec` = one of:

        - ```ebnf
          | ""
          ```
          (empty string) for hashes of the flat (single file) serialization

        - ```ebnf
          | "r:"
          ```
          hashes of the for [Nix Archive (NAR)] (arbitrary file system object) serialization

        - ```ebnf
          | "git:"
          ```
          hashes of the [Git blob/tree](https://git-scm.com/book/en/v2/Git-Internals-Git-Objects) [Merkel tree](https://en.wikipedia.org/wiki/Merkle_tree) format

      - ```ebnf
        algo = "md5" | "sha1" | "sha256"
        ```

      - `hash` = base-16 representation of the path or flat hash of the contents of the path (or expected contents of the path for fixed-output derivations).

      Note that `id` = `"out"`, regardless of the name part of the store path.
      Also note that NAR + SHA-256 must not use this case, and instead must use the `type` = `"source:" ...` case.

[Nix Archive (NAR)]: @docroot@/store/file-system-object/content-address.md#serial-nix-archive
[SHA-256]: https://en.m.wikipedia.org/wiki/SHA-256

### Historical Note

The `type` = `"source:" ...` and `type` = `"output:out"` grammars technically overlap in purpose,
in that both can represent data hashed by its SHA-256 NAR serialization.

The original reason for this way of computing names was to prevent name collisions (for security).
For instance, the thinking was that it shouldn't be feasible to come up with a derivation whose output path collides with the path for a copied source.
The former would have an `inner-fingerprint` starting with `output:out:`, while the latter would have an `inner-fingerprint` starting with `source:`.

Since `64519cfd657d024ae6e2bb74cb21ad21b886fd2a` (2008), however, it was decided that separating derivation-produced vs manually-hashed content-addressed data like this was not useful.
Now, data that is content-addressed with SHA-256 + NAR-serialization always uses the `source:...` construction, regardless of how it was produced (manually or by derivation).
This allows freely switching between using [fixed-output derivations](@docroot@/glossary.md#gloss-fixed-output-derivation) for fetching, and fetching out-of-band and then manually adding.
It also removes the ambiguity from the grammar.
