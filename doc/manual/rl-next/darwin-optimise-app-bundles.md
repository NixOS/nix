---
synopsis: "Store optimisation on macOS skips app bundle files the OS refuses to hard-link"
prs: [16654]
---

macOS refuses to hard-link `PkgInfo` and `_CodeSignature/*` anywhere below a `*.app` directory, even for root.
Nix already skipped everything below `*.app/Contents/`, but iOS-style bundles keep these files at the bundle root, so [`nix store optimise`](@docroot@/command-ref/new-cli/nix3-store-optimise.md) aborted with `Operation not permitted`, and with [`auto-optimise-store`](@docroot@/command-ref/conf-file.md#conf-auto-optimise-store) so did any build producing such a bundle.
Files that cannot be hard-linked are now left as they are.
