# Derivation "ATerm" file format

For historical reasons, [derivations](@docroot@/glossary.md#gloss-store-derivation) are stored on-disk in [ATerm](https://homepages.cwi.nl/~daybuild/daily-books/technology/aterm-guide/aterm-guide.html) format.

Derivations are serialised in one of the following formats:

- ```
  Derive(...)
  ```

  For all stable derivations.

- ```
  DrvWithVersion(<version-string>, ...)
  ```

  The only `version-string`s that are in use today are for [experimental features](@docroot@/contributing/experimental-features.md):

  - `"xp-dyn-drv"` for the [`dynamic-derivations`](@docroot@/contributing/experimental-features.md#xp-feature-dynamic-derivations) experimental feature.
