---
synopsis: import-from-derivation builds the derivation in the build store
prs: 9661
---

When using `--eval-store`, `import`ing from a derivation will now result in the derivation being built on the build store, i.e. the store specified in the `store` Nix option.

Because the resulting Nix expression must be copied back to the eval store in order to be imported, this requires the eval store to trust the build store's signatures.
