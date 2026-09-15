---
synopsis: "`--dry-run` reports dynamic derivations and missing `.drv` files more accurately"
prs: []
issues: []
---

The report of what a build would do (`--dry-run`, and the "these derivations will be built" / "these paths will be fetched" messages) is now produced by the build scheduler itself, so it matches what would actually happen more closely.
Dynamic derivations are no longer skipped with a warning, and a derivation whose `.drv` file is not present is reported as needing to be fetched, rather than as unknown, when a substituter has it.
