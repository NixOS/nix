---
synopsis: Store object info JSON format now uses `null` rather than omitting fields.
prs: 9995
---

The [store object info JSON format](@docroot@/protocols/json/store-object-info.md), used for e.g. `nix path-info`, no longer omits fields to indicate absent information, but instead includes the fields with a `null` value.
For example, `"ca": null` is used to to indicate a store object that isn't content-addressed rather than omitting the `ca` field entirely.
This makes records of this sort more self-describing, and easier to consume programmatically.

We will follow this design principle going forward;
the [JSON guidelines](@docroot@/contributing/json-guideline.md) in the contributing section have been updated accordingly.
