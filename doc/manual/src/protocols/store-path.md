# Complete Store Path Calculation

This is the complete specification for how store paths are calculated.

Regular users do *not* need to know this information --- store paths can be treated as black boxes computed from the properties of the store objects they refer to.
But for those interested in exactly how Nix works, e.g. if they are reimplementing it, this information can be useful.

```ebnf
store-path = store-dir "/" digest "-" name
```
where

- `digest` = base-32 representation of the first 160 bits of a [SHA-256] hash of `pre`

  Th is :the hash part of the store name

- `pre` = the string

  ```ebnf
  type ":" sha256 ":" inner-digest ":" store ":" name
  ```

  Note that it includes the location of the store as well as the name to make sure that changes to either of those are reflected in the hash
  (e.g. you won't get `/nix/store/<digest>-name1` and `/nix/store/<digest>-name2`, or `/gnu/store/<digest>-name1`, with equal hash parts).

- `name` = the name of the store object.

- `store` = the [store directory](@docroot@/store/store-path.md#store-directory)

- `type` = one of:

  - ```ebnf
    "text" ( ":" store-path )*
    ```

    for encoded derivations written to the store.
    The optional trailing store paths are the references of the store object.

  - ```ebnf
    "source" ( ":" store-path )*
    ```

    For paths copied to the store and hashed via a [Nix Archive (NAR)] and [SHA-256][sha-256].
    Just like in the text case, we can have the store objects referenced by their paths.
    Additionally, we can have an optional `:self` label to denote self reference.

  - ```ebnf
    "output:" id
    ```

    For either the outputs built from derivations,
    paths copied to the store hashed that area single file hashed directly, or the via a hash algorithm other than [SHA-256][sha-256].
    (in that case "source" is used; this is only necessary for compatibility).

    `id` is the name of the output (usually, "out").
    For content-addressed store objects, `id`, is always "out".

- `inner-digest` = base-16 representation of a SHA-256 hash of `inner-pre`

- `inner-pre` = one of the following based on `type`:

  - if `type` = `"text:" ...`:

    the string written to the resulting store path.

  - if `type` = `"source:" ...`:

    the the hash of the [Nix Archive (NAR)] serialization of the [file system object](@docroot@/store/file-system-object.md) of the store object.

  - if `type` = `"output:" id`:

    - For input-addressed derivation outputs:

      the [ATerm](@docroot@/protocols/derivation-aterm.md) serialization of the derivation modulo fixed output derivations.

    - For content-addressed store paths:

      the string

      ```ebnf
      "fixed:out:" rec algo ":" hash ":"
      ```

      where

      - `rec` = one of:

        - `r:` hashes of the for [Nix Archive (NAR)] (arbitrary file system object) serialization

        - `` (empty string) for hashes of the flat (single file) serialization

      - `algo` = `md5`, `sha1` or `sha256`

      - `hash` = base-16 representation of the path or flat hash of the contents of the path (or expected contents of the path for fixed-output derivations).

      Note that `id` = `out`, regardless of the name part of the store path.
      Also note that NAR + SHA-256 must not use this case, and instead must use the `type` = `"source:" ...` case.

[Nix Archive (NAR)]: @docroot@/glossary.md#gloss-NAR
[sha-256]: https://en.m.wikipedia.org/wiki/SHA-256

## Historical Note

The `type` = `"source:" ...` and `type` = `"output:out"` grammars technically overlap in purpose,
in that both can represent data hashed by its SHA-256 NAR serialization.

The original reason for this way of computing names was to prevent name collisions (for security).
For instance, the thinking was that it shouldn't be feasible to come up with a derivation whose output path collides with the path for a copied source.
The former would have an `inner-pre` starting with `output:out:`, while the latter would have an `inner-pre` starting with `source:`.

Since `64519cfd657d024ae6e2bb74cb21ad21b886fd2a` (2008), however, it was decided that separting derivation-produced vs manually-hashed content-addressed data like this was not useful.
Now, data this is to be SHA-256 + NAR-serialization content-addressed always uses the `source:...` construction, regardless of how it was produced (manually or by derivation).
This allows freely switching between using [fixed-output derivations](@docroot@/glossary.md#gloss-fixed-output-derivation) for fetching, and fetching out-of-band and then manually adding.
It also removes the ambiguity from the grammar.
