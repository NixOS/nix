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
after `RewritingSink` runs. The fix-up preserves every other byte,
including the `linker-signed` flag and the original page size,
so the rewritten binary is byte-identical to a clean build
(`LC_UUID` non-determinism from Apple's `ld` aside). Both
input-addressed and content-addressing
multi-output derivations are covered. Thin Mach-O, fat32, and fat64
containers are handled, including binaries with SHA-1 + SHA-256
alternate CodeDirectories. Non-Mach-O files are silently skipped.
Binaries carrying an embedded CMS signature (Developer ID / App
Store chain) are skipped with a warning, since recomputing the
CodeDirectory would invalidate the signer's cdhash commitment.
