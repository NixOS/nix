# Derivation "ATerm" file format

For historical reasons, [store derivations][store derivation] are stored on-disk in "Annotated Term" (ATerm) format
([guide](https://homepages.cwi.nl/~daybuild/daily-books/technology/aterm-guide/aterm-guide.html),
[paper](https://doi.org/10.1002/(SICI)1097-024X(200003)30:3%3C259::AID-SPE298%3E3.0.CO;2-Y)).

## The ATerm format used

Derivations are serialised in one of the following formats:

- ```
  Derive(...)
  ```

  For all stable derivations.

- ```
  DrvWithVersion(<version-string>, ...)
  ```

  The only `version-string`s that are in use today are for [experimental features](@docroot@/development/experimental-features.md):

  - `"xp-dyn-drv"` for the [`dynamic-derivations`](@docroot@/development/experimental-features.md#xp-feature-dynamic-derivations) experimental feature.

## Use for encoding to store object

When derivation is encoded to a [store object] we make the following choices:

- The store path name is the derivation name with `.drv` suffixed at the end

  Indeed, the ATerm format above does *not* contain the name of the derivation, on the assumption that a store path will also be provided out-of-band.

- The derivation is content-addressed using the ["Text" method] of content-addressing derivations

Currently we always encode derivations to store object using the ATerm format (and the previous two choices),
but we reserve the option to encode new sorts of derivations differently in the future.

[store derivation]: @docroot@/glossary.md#gloss-store-derivation
[store object]: @docroot@/glossary.md#gloss-store-object
["Text" method]: @docroot@/store/store-object/content-address.md#method-text
