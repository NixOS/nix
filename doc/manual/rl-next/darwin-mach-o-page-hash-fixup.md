---
synopsis: Fix darwin Mach-O code signature corruption from output rewriting
issues: [6065]
prs: [15638]
---

On darwin, the build daemon's scratch-path rewriting pass
(`RewritingSink`, applied in `registerOutputs` after the builder
exits) mutated bytes inside pages that `ld -adhoc_codesign` had
already covered with page hashes in `LC_CODE_SIGNATURE`. Affected
multi-output derivations produced binaries whose stored page hashes
no longer matched the on-disk bytes, and the macOS kernel SIGKILLed
them at first page-in with `cs_invalid_page`. This surfaced in
nixpkgs as [#507531](https://github.com/NixOS/nixpkgs/issues/507531)
(fish on `nixpkgs-darwin`).

The daemon now recomputes only the affected page hash slots in place
after `RewritingSink` runs. Every other byte of the Mach-O is
unchanged — including the `linker-signed` flag, the original 4 KiB
page size, and the identifier — so the fix-up is length-preserving
and the result is bit-identical to a clean build. A single call site
inside the shared `rewriteOutput` lambda covers both input-addressed
and content-addressing multi-output derivations.
