---
synopsis: "Derivation metadata without affecting output paths"
prs: []
issues: []
---

Packages in the expression language often have valuable metadata that is intentionally omitted from the derivations so as not to cause unnecessary rebuilds.
Unfortunately, this omission made it hard to access this information in analyses and reports, e.g. Software Bill of Materials (SBOM).

The new [`derivation-meta`](@docroot@/development/experimental-features.md#xp-feature-derivation-meta) feature solves this problem, and makes it easy for package management systems and tooling to associate metadata with builds without disruption or loss of binary cache effectiveness.

Derivations using [structured attributes](@docroot@/store/derivation/index.md#structured-attrs) can now include a [`__meta`](@docroot@/language/advanced-attributes.md#adv-attr-meta) attribute for metadata such as package descriptions, licenses, and maintainer information, without affecting output paths or triggering rebuilds.

**User-facing workflow**

While the mechanism is general, the way most users will interact with this feature is to
1. Set `config.derivationMeta = true;` in your Nixpkgs call (pending e.g. [nixpkgs#466932](https://github.com/NixOS/nixpkgs/pull/466932))
2. Enable the [`derivation-meta`](@docroot@/development/experimental-features.md#xp-feature-derivation-meta) experimental feature on all builders, i.e. the local system and/or any remote builders.
3. Use your expressions normally with `derivationMeta` enabled.
4. Use your expressions or derivations with tooling that reads `__meta` from the derivations.

**Technical requirements**

E.g. if you don't use Nixpkgs, to use this feature, your derivations and Nix configuration must include:

1. Set [`__structuredAttrs`](@docroot@/language/advanced-attributes.md#adv-attr-structuredAttrs)` = true;`
2. Include `"derivation-meta"` in [`requiredSystemFeatures`](@docroot@/language/advanced-attributes.md#adv-attr-requiredSystemFeatures)
3. `requiredSystemFeatures` must be sorted lexicographically
4. Enable the [`derivation-meta`](@docroot@/development/experimental-features.md#xp-feature-derivation-meta) experimental feature in [configuration](@docroot@/command-ref/conf-file.md#conf-experimental-features)

Example:

```nix
derivation {
  name = "example";
  __structuredAttrs = true;
  requiredSystemFeatures = [ "derivation-meta" ];
  __meta = {
    description = "Example package";
    license = "MIT";
  };
  # ... other attributes ...
}
```

**Output hashing details**

The [`__meta`](@docroot@/language/advanced-attributes.md#adv-attr-meta) attribute and `derivation-meta` system feature are filtered from output path computation using [hash modulo](@docroot@/store/derivation/outputs/input-address.md#hash-quotient-drv). This means:

- Changing metadata does not invalidate binary caches.
- Enabling or disabling the feature does not affect output paths.
- Derivation paths (`.drv` files) still change when metadata changes, preserving the full derivation record.
- As usual, changes to meta attributes will affect derivations that explicitly depend on the respective values.

This way of hashing means that you can trivially produce multiple derivations that produce the same input-addressed output.
This is not a regression of Nix's hashing model, as it was already the case that the fetcher implementation details behind a fixed-output derivation were hidden from the output path, but included in the derivation path.
Derivation-meta simply leans into this one-to-many relationship.

Care has been taken so that derivations resolve to the same output hashes whether they do or do not use this feature .
While this can be thwarted using the last point of the list or by depending on a *`drvPath`* as opposed to a (normal) *output path*, we observe in practice that regardless of whether the feature is in use or not, derivations hash to the same outputs, sharing the build and the binary cache entries.

**Minor breaking change**

Nix does not allow `__meta` in derivations that do not also require the `derivation-meta` system feature, because such a derivation is most likely an incomplete derivation-meta derivation, or it is incompatible with the feature altogether, since that name has been repurposed.
