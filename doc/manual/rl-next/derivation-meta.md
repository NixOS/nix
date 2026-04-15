---
synopsis: "Derivation metadata without affecting output paths"
prs: []
issues: []
---

Packages in the expression language often have valuable metadata that is intentionally omitted from the derivations so as not to cause unnecessary rebuilds.
Unfortunately, this omission made it very hard to access this information.

The new [`derivation-meta`](@docroot@/development/experimental-features.md#xp-feature-derivation-meta) feature solves this problem, and makes it easy for package management systems and tooling to associate metadata with builds without disruption or loss of cache effectiveness.

Derivations using [structured attributes](@docroot@/store/derivation/index.md#structured-attrs) can now include a [`__meta`](@docroot@/language/advanced-attributes.md#adv-attr-meta) attribute for metadata such as package descriptions, licenses, and maintainer information, without affecting output paths or triggering rebuilds.

To use this feature, derivations must:

1. Set [`__structuredAttrs`](@docroot@/language/advanced-attributes.md#adv-attr-structuredAttrs) = true
2. Include `"derivation-meta"` in [`requiredSystemFeatures`](@docroot@/language/advanced-attributes.md#adv-attr-requiredSystemFeatures)
3. Enable the [`derivation-meta`](@docroot@/development/experimental-features.md#xp-feature-derivation-meta) experimental feature in [configuration](@docroot@/command-ref/conf-file.md#conf-experimental-features)

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

The [`__meta`](@docroot@/language/advanced-attributes.md#adv-attr-meta) attribute and `derivation-meta` system feature are filtered from output path computation using [hash modulo](@docroot@/store/derivation/outputs/input-address.md#hash-quotient-drv). This means:

- Changing metadata does not invalidate binary caches
- Enabling or disabling the feature does not affect output paths
- Derivation paths (`.drv` files) still change when metadata changes, preserving the full derivation record
- As usual, changes to meta attributes will affect derivations that explicitly depend on the respective values.

Care has been taken so that derivations resolve to the same output hashes whether they do or do not use this feature .
While this can be thwarted using the last point or by including a *`drvPath`* as opposed to a (normal) *output path*, we observe in practice that both configurations (`requiredSystemFeatures = ["derivation-meta"];` or without) are binary cache compatible, sharing the same build.
